#pragma once

#include <filesystem>
#include <optional>

namespace gw2 {

// Registers the clean-exit finalizer, which discards this session's log when Detailed logging is off.
void InstallSessionLogCleanup();

// Never deletes a log that contains a CRASH REPORT. Shuts logging down, so call it only once no other thread can
// log: the normal exit through atexit.
void FinalizeSessionLog();

// The same policy for the window close, where guest threads still log: logging stays up, and on Windows, where the
// open log cannot be deleted, the next start removes it.
void DiscardSessionLogBeforeHardExit();
void RemoveDiscardedSessionLogs();

// A relaunch hands the active log to the next process, which takes it once the previous one has exited.
void BeginSessionLogHandoff();
void CancelSessionLogHandoff();
std::optional<std::filesystem::path> TakeInheritedSessionLog();

}  // namespace gw2
