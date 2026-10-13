#pragma once

// Crash, error and freeze reports written as text files for the player (by
// default to the desktop): what happened, where the game was, a native stack
// trace and the end of the run's log. Nothing is done per frame beyond
// updating a small context line.

#include <string>
#include <string_view>

namespace Oot3dNativeGame {

// Installs fatal-signal handlers and a freeze watchdog. Reports go to
// TRIAEVUM_CRASH_DIR, else the XDG desktop directory, else ~/Desktop.
void InstallTriAevumCrashReporter(std::string_view title);

// After a library installed its own fatal-signal handlers (the window's
// crash log): ours again (report, then end the process).
void ReinstallTriAevumCrashSignals();

// What the game is doing (one line, kept for the next report).
void SetTriAevumCrashContext(std::string_view context) noexcept;

// The main loop made progress (frame presented or guest frame run).
void TriAevumHeartbeat() noexcept;

// The main loop is legitimately idle (e.g. waiting for a dialog): no freeze
// report while set.
void SetTriAevumHeartbeatPaused(bool paused) noexcept;

// Writes a report for an error that ends the run; returns its path (empty if
// it could not be written).
std::string WriteTriAevumCrashReport(std::string_view kind, std::string_view message);

}  // namespace Oot3dNativeGame
