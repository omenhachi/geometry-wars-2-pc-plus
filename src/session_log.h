#pragma once

#include <filesystem>
#include <optional>

namespace gw2 {

// Registers the clean-exit finalizer once. With Detailed logging off, a normal process exit then discards this
// session's log; with it on, the log is kept. Call after logging is initialized.
void InstallSessionLogCleanup();

// Discards this session's log when Detailed logging is off. Idempotent and never deletes a log that already
// contains a CRASH REPORT. Shuts logging down, so call it only once no other thread can log: the normal exit
// through atexit.
void FinalizeSessionLog();

// The same policy for the host window close, where guest threads still log: logging stays up, and on Windows,
// where the open log cannot be deleted, the next start removes it (RemoveDiscardedSessionLogs).
void DiscardSessionLogBeforeHardExit();
void RemoveDiscardedSessionLogs();

// The launcher and the game share one normal log. A relaunch hands the active log to the next process: Begin sets
// the inherited path, Cancel clears it when the relaunch fails, and the child takes it once the previous process
// has exited.
void BeginSessionLogHandoff();
void CancelSessionLogHandoff();
std::optional<std::filesystem::path> TakeInheritedSessionLog();

}  // namespace gw2
