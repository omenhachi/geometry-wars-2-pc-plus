#pragma once

struct PPCFuncMapping;

namespace gw2 {

// Registers the generated guest-to-host function table, so a native crash address can name the recompiled function.
void InstallGuestFunctionTable(const PPCFuncMapping* mappings);

}  // namespace gw2
