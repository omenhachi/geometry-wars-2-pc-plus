// DEBUG_SAULO: temporary guest-fault locator. Remove with the other DEBUG_SAULO diagnostics.

#include <windows.h>

#include <cstdio>

#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>

namespace {

constexpr uintptr_t kGuestMemorySize = 0x100000000ull;
constexpr USHORT kMaxFrames = 48;
constexpr uint64_t kStackWindow = 0x1000;

uintptr_t GuestMemoryBase() {
  rex::Runtime* runtime = rex::Runtime::instance();
  return runtime ? reinterpret_cast<uintptr_t>(runtime->virtual_membase()) : 0;
}

void AppendLog(const char* text, int length) {
  const HANDLE file = CreateFileA("logs\\debug_saulo_fault.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
  CloseHandle(file);
}

template <typename... Args>
void AppendLine(const char* format, Args... args) {
  char line[512];
  const int length = std::snprintf(line, sizeof(line), format, args...);
  AppendLog(line, length);
}

void AppendExeFrames(const char* tag) {
  const auto exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  void* frames[kMaxFrames] = {};
  const USHORT count = CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    const auto pc = reinterpret_cast<uintptr_t>(frames[i]);
    if (pc >= exe_base && pc - exe_base < 0x10000000) {
      AppendLine("[DEBUG_SAULO][%s] index=%llu rva=0x%llX\n", tag, static_cast<uint64_t>(i),
                 static_cast<uint64_t>(pc - exe_base));
    }
  }
}

void DumpHostRegisters(const CONTEXT& regs) {
  AppendLine("[DEBUG_SAULO][Host] rbx=0x%llX rsi=0x%llX rdi=0x%llX rbp=0x%llX r12=0x%llX r13=0x%llX\n", regs.Rbx,
             regs.Rsi, regs.Rdi, regs.Rbp, regs.R12, regs.R13);
  AppendLine("[DEBUG_SAULO][Host] r14=0x%llX r15=0x%llX rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX\n", regs.R14,
             regs.R15, regs.Rcx, regs.Rdx, regs.R8, regs.R9);
}

void DumpGuestState(uintptr_t base) {
  rex::runtime::ThreadState* thread = rex::runtime::ThreadState::Get();
  if (!thread || !thread->context()) {
    return;
  }
  const PPCContext& ctx = *thread->context();
  AppendLine("[DEBUG_SAULO][Guest] ctx=0x%llX r1=0x%llX r3=0x%llX r30=0x%llX r31=0x%llX lr=0x%llX ctr=0x%llX\n",
             reinterpret_cast<uint64_t>(&ctx), ctx.r1.u64, ctx.r3.u64, ctx.r30.u64, ctx.r31.u64, ctx.lr,
             ctx.ctr.u64);
  uint64_t nonzero = 0;
  for (uint64_t offset = 0; offset < kStackWindow; offset += 8) {
    uint64_t value = 0;
    SIZE_T read = 0;
    const auto* address = reinterpret_cast<const void*>(base + ctx.r1.u32 + offset);
    if (ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &read) && read == sizeof(value) &&
        value != 0) {
      ++nonzero;
    }
  }
  AppendLine("[DEBUG_SAULO][Stack] nonzero qwords in r1..r1+0x1000: %llu\n", nonzero, 0, 0);
}

LONG CALLBACK LogGuestFault(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD& record = *info->ExceptionRecord;
  if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters < 2) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t base = GuestMemoryBase();
  if (!base) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const auto address = static_cast<uintptr_t>(record.ExceptionInformation[1]);
  if (address < base || address >= base + kGuestMemorySize) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const auto rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
  HMODULE exe = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(rip), &exe);
  AppendLine("[DEBUG_SAULO][Fault] access=0x%llX rip=0x%llX rva=0x%llX\n", static_cast<uint64_t>(address),
             static_cast<uint64_t>(rip), static_cast<uint64_t>(rip - reinterpret_cast<uintptr_t>(exe)));
  DumpHostRegisters(*info->ContextRecord);
  DumpGuestState(base);
  AppendExeFrames("Frame");
  return EXCEPTION_CONTINUE_SEARCH;
}

const bool kFaultLocatorInstalled = AddVectoredExceptionHandler(1, LogGuestFault) != nullptr;

}  // namespace
