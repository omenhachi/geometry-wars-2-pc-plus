#include "launcher_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <string>

#include "trg/platform.h"

#include "dpi.h"
#include "launcher_placement.h"
#include "session_log.h"

// The NVIDIA Optimus and AMD switchable graphics drivers run programs exporting these on the discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace gw2 {
namespace {

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }
  MonitorInfo found;
  found.width = info.rcMonitor.right - info.rcMonitor.left;
  found.height = info.rcMonitor.bottom - info.rcMonitor.top;
  found.work_width = info.rcWork.right - info.rcWork.left;
  found.work_height = info.rcWork.bottom - info.rcWork.top;
  found.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
    found.refresh_hz = int(mode.dmDisplayFrequency);
  }
  UINT dpi_x = 0;
  UINT dpi_y = 0;
  if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
    found.scale = DpiScaleFromDpi(dpi_x);
  }
  reinterpret_cast<std::vector<MonitorInfo>*>(user)->push_back(found);
  return TRUE;
}

BOOL CALLBACK AddWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    reinterpret_cast<std::vector<Rect>*>(user)->push_back(
        {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
  }
  return TRUE;
}

}  // namespace

std::vector<Rect> WorkAreas() {
  std::vector<Rect> areas;
  EnumDisplayMonitors(nullptr, nullptr, AddWorkArea, reinterpret_cast<LPARAM>(&areas));
  return areas;
}

std::optional<WindowState> QueryWindowState(void* native_handle) {
  const HWND window = static_cast<HWND>(native_handle);
  RECT rect;
  if (!window || !IsWindow(window) || !GetWindowRect(window, &rect)) {
    return std::nullopt;
  }
  WindowState state;
  state.rect = {rect.left, rect.top, rect.right, rect.bottom};
  state.minimized = IsIconic(window) != FALSE;
  state.maximized = IsZoomed(window) != FALSE;
  if (state.minimized) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    state.maximized = GetWindowPlacement(window, &placement) && (placement.flags & WPF_RESTORETOMAXIMIZED);
  }
  return state;
}

void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement) {
  const HWND window = static_cast<HWND>(native_handle);
  if (!window || !IsWindow(window)) {
    return;
  }
  ShowWindow(window, SW_RESTORE);
  SetWindowPos(window, nullptr, placement.normal.left, placement.normal.top, placement.normal.width(),
               placement.normal.height(), SWP_NOZORDER | SWP_NOACTIVATE);
  if (placement.maximized) {
    ShowWindow(window, SW_MAXIMIZE);
  }
}

void ApplyWindowIcon() {}

void SetApplicationId() {}

void RunLauncherWithoutGraphics(const GraphicsFallbackInfo&) {}

namespace {

PreShowPlan g_plan;
WindowPlacement g_saved;
HHOOK g_hook = nullptr;
std::optional<PreShowResult> g_result;

LRESULT CALLBACK PreShowProc(int code, WPARAM wparam, LPARAM lparam) {
  const auto* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
  if (code == HC_ACTION && message->message == WM_SHOWWINDOW && message->wParam && message->lParam == 0 &&
      !GetParent(message->hwnd) && !g_result) {
    HHOOK hook = g_hook;
    g_hook = nullptr;
    const HWND window = message->hwnd;
    RECT before;
    GetWindowRect(window, &before);
    g_result = PreShowResult{{{before.left, before.top, before.right, before.bottom}, false}, g_saved};
    if (g_plan.normal) {
      SetWindowPos(window, nullptr, g_plan.normal->left, g_plan.normal->top, g_plan.normal->width(),
                   g_plan.normal->height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_plan.maximized) {
      ShowWindow(window, SW_MAXIMIZE);
    }
    UnhookWindowsHookEx(hook);
    return 0;
  }
  return CallNextHookEx(g_hook, code, wparam, lparam);
}

}  // namespace

void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved) {
  if (g_hook || !plan.Needed()) {
    return;
  }
  g_plan = plan;
  g_saved = saved;
  g_result.reset();
  g_hook = SetWindowsHookExW(WH_CALLWNDPROC, PreShowProc, nullptr, GetCurrentThreadId());
}

std::optional<PreShowResult> TakePreShowResult() {
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
  std::optional<PreShowResult> result = g_result;
  g_result.reset();
  return result;
}

std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

bool ShiftHeld() {
  return trg::ShiftHeld();
}

bool PackagePickerAvailable() {
  return true;
}

bool RelaunchSelf(std::wstring_view extra_args) {
  BeginSessionLogHandoff();
  std::wstring command_line = GetCommandLineW();
  command_line += L' ';
  command_line += extra_args;
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    CancelSessionLogHandoff();
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    CancelSessionLogHandoff();
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

UiFontFiles FindUiFonts() {
  wchar_t windows[MAX_PATH];
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  const std::filesystem::path fonts =
      std::filesystem::path(length ? std::wstring(windows, length) : std::wstring(L"C:\\Windows")) / L"Fonts";
  // Bahnschrift (Windows 10 and later) for labels, page names and titles: a squarer, signage-like face closer to
  // the game's vector lettering. Body text stays Segoe UI.
  std::error_code error;
  const std::filesystem::path display = fonts / "bahnschrift.ttf";
  if (std::filesystem::exists(display, error)) {
    return {fonts / "segoeui.ttf", display, display};
  }
  return {fonts / "segoeui.ttf", fonts / "seguisb.ttf", fonts / "segoeuib.ttf"};
}

const char* ArchitectureName() {
#if defined(_M_ARM64)
  return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return "x86-64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}

const char* GraphicsBackendName() {
  return "Direct3D 12";
}

}  // namespace gw2
