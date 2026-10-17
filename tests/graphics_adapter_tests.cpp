// Graphics card tests: the setting values and, on Windows, the adapter list against the order ReXGlue's
// d3d12_adapter index refers to. No runtime and no game files.

#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

#include "graphics_adapters.h"

using namespace gw2;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

void TestValues() {
  std::printf("values\n");
  CHECK(!ParseGraphicsAdapter(kGraphicsAdapterAuto));
  CHECK(!ParseGraphicsAdapter(""));
  CHECK(!ParseGraphicsAdapter("-1"));
  CHECK(!ParseGraphicsAdapter("1x"));
  CHECK(!ParseGraphicsAdapter(" 1"));
  CHECK(!ParseGraphicsAdapter("Intel"));
  CHECK(ParseGraphicsAdapter("0") == 0);
  CHECK(ParseGraphicsAdapter("12") == 12);

  const std::vector<GraphicsAdapter> adapters = {{0, "NVIDIA GeForce RTX 4070"}, {1, "Intel UHD Graphics 730"}};
  CHECK(ResolveGraphicsAdapter(kGraphicsAdapterAuto, adapters) == -1);
  CHECK(ResolveGraphicsAdapter("0", adapters) == 0);
  CHECK(ResolveGraphicsAdapter("1", adapters) == 1);
  // A saved card that is gone falls back to Automatic.
  CHECK(ResolveGraphicsAdapter("2", adapters) == -1);
  CHECK(ResolveGraphicsAdapter("1", {}) == -1);
  // The value is the DXGI index, not the position in the list.
  const std::vector<GraphicsAdapter> gap = {{1, "A"}, {3, "B"}};
  CHECK(ResolveGraphicsAdapter("3", gap) == 3);
  CHECK(ResolveGraphicsAdapter("0", gap) == -1);
}

#if defined(_WIN32)
// ReXGlue opens d3d12_adapter = N as adapter N of EnumAdapters1 on a CreateDXGIFactory2 factory.
void TestEnumerationMatchesRuntime() {
  std::printf("enumeration against the runtime's adapter order\n");
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  for (const GraphicsAdapter& adapter : adapters) {
    std::printf("  %d  %s\n", adapter.index, adapter.name.c_str());
  }
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
  if (!factory) {
    return;
  }
  for (size_t i = 0; i < adapters.size(); ++i) {
    CHECK(i == 0 || adapters[i].index > adapters[i - 1].index);
    CHECK(!adapters[i].name.empty());
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    CHECK(factory->EnumAdapters1(UINT(adapters[i].index), &adapter) == S_OK);
    DXGI_ADAPTER_DESC1 desc;
    CHECK(adapter && SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE));
  }
  UINT hardware = 0;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index, adapter.Reset()) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
      ++hardware;
    }
  }
  CHECK(hardware == adapters.size());
}
#endif

}  // namespace

int main() {
  TestValues();
#if defined(_WIN32)
  TestEnumerationMatchesRuntime();
#else
  CHECK(ListGraphicsAdapters().empty());
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
