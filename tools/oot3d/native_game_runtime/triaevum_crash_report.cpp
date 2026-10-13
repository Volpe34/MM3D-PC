#include "triaevum_crash_report.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>
#define TRIAEVUM_CRASH_POSIX 1
#endif

namespace Oot3dNativeGame {
namespace {

// Written by the main thread, read by the signal handler / watchdog: fixed
// buffers, no allocation at crash time.
std::array<char, 512> gContext{};
std::array<char, 1024> gReportDirectory{};
std::array<char, 1024> gLogFile{};
std::array<char, 64> gTitle{};
std::atomic<uint64_t> gHeartbeats{0};
std::atomic<bool> gHeartbeatPaused{false};
std::atomic<bool> gReported{false};
std::chrono::steady_clock::time_point gStart;

void CopyInto(std::array<char, 512> &target, std::string_view text) noexcept {
  const size_t size = std::min(text.size(), target.size() - 1);
  std::memcpy(target.data(), text.data(), size);
  target[size] = '\0';
}

std::filesystem::path DesktopDirectory() {
  if (const char *configured = std::getenv("TRIAEVUM_CRASH_DIR")) {
    return configured;
  }
  const char *home = std::getenv("HOME");
  const std::filesystem::path homePath = home != nullptr ? home : ".";
  // XDG user dirs (localized desktop folder names, e.g. "Scrivania").
  std::ifstream dirs(homePath / ".config/user-dirs.dirs");
  std::string line;
  while (std::getline(dirs, line)) {
    if (line.rfind("XDG_DESKTOP_DIR=", 0) != 0) {
      continue;
    }
    std::string value = line.substr(16);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    if (value.rfind("$HOME", 0) == 0) {
      value = homePath.string() + value.substr(5);
    }
    if (std::filesystem::is_directory(value)) {
      return value;
    }
  }
  if (std::filesystem::is_directory(homePath / "Desktop")) {
    return homePath / "Desktop";
  }
  return homePath;
}

std::string Timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::array<char, 32> text{};
  std::strftime(text.data(), text.size(), "%Y-%m-%d_%H-%M-%S", &local);
  return text.data();
}

// The last lines of the run's log (play.sh tees the output to it).
std::string LogTail(size_t lines) {
  if (gLogFile[0] == '\0') {
    return "(no log file: start the game with play.sh to keep one)\n";
  }
  std::ifstream log(gLogFile.data(), std::ios::binary);
  if (!log) {
    return "(log file not readable)\n";
  }
  log.seekg(0, std::ios::end);
  const std::streamoff size = log.tellg();
  const std::streamoff start = std::max<std::streamoff>(0, size - 64 * 1024);
  log.seekg(start);
  std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
  size_t position = text.size();
  for (size_t count = 0; count <= lines && position > 0; ++count) {
    position = text.rfind('\n', position - 1);
    if (position == std::string::npos) {
      position = 0;
      break;
    }
  }
  return text.substr(position);
}

std::string Header(std::string_view kind, std::string_view message) {
  std::ostringstream out;
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - gStart)
                           .count();
  out << gTitle.data() << " (TriAevum PC port) - " << kind << "\n"
      << "time: " << Timestamp() << "  (after " << seconds / 60 << " min " << seconds % 60
      << " s of play)\n";
#if defined(TRIAEVUM_CRASH_POSIX)
  utsname system{};
  if (uname(&system) == 0) {
    out << "system: " << system.sysname << ' ' << system.release << ' ' << system.machine << '\n';
  }
#endif
  out << "game state: " << gContext.data() << "\n\n"
      << "what happened:\n" << message << "\n\n";
  return out.str();
}

std::string WriteReport(std::string_view kind, std::string_view message, std::string_view extra) {
  try {
    const std::filesystem::path directory = gReportDirectory.data();
    const std::filesystem::path path =
        directory / (std::string(gTitle.data()) + "_" + std::string(kind) + "_" + Timestamp() + ".txt");
    std::ofstream out(path);
    out << Header(kind, message) << extra
        << "last lines of the log (" << (gLogFile[0] ? gLogFile.data() : "-") << "):\n"
        << LogTail(200);
    out.close();
    std::fprintf(stderr, "TriAevum: %.*s report written to %s\n",
                 static_cast<int>(kind.size()), kind.data(), path.string().c_str());
    return path.string();
  } catch (...) {
    return {};
  }
}

#if defined(TRIAEVUM_CRASH_POSIX)
const char *SignalName(int signal) {
  switch (signal) {
  case SIGSEGV: return "SIGSEGV (invalid memory access)";
  case SIGABRT: return "SIGABRT (abort)";
  case SIGFPE: return "SIGFPE (arithmetic error)";
  case SIGILL: return "SIGILL (illegal instruction)";
  case SIGBUS: return "SIGBUS (bus error)";
  default: return "fatal signal";
  }
}

void WriteAll(int fd, const char *text) {
  size_t size = std::strlen(text);
  while (size > 0) {
    const ssize_t written = write(fd, text, size);
    if (written <= 0) return;
    text += written;
    size -= static_cast<size_t>(written);
  }
}

void FatalSignal(int signal) {
  // Report once; the native stack trace first (async-signal-safe calls),
  // then the rest best-effort.
  if (!gReported.exchange(true)) {
    std::array<char, 1400> path{};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> stamp{};
    std::strftime(stamp.data(), stamp.size(), "%Y-%m-%d_%H-%M-%S", &local);
    std::snprintf(path.data(), path.size(), "%s/%s_crash_%s.txt", gReportDirectory.data(),
                  gTitle.data(), stamp.data());
    const int fd = open(path.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      WriteAll(fd, gTitle.data());
      WriteAll(fd, " (TriAevum PC port) - crash\nsignal: ");
      WriteAll(fd, SignalName(signal));
      WriteAll(fd, "\ngame state: ");
      WriteAll(fd, gContext.data());
      WriteAll(fd, "\n\nnative stack trace:\n");
      std::array<void *, 64> frames{};
      const int count = backtrace(frames.data(), static_cast<int>(frames.size()));
      backtrace_symbols_fd(frames.data(), count, fd);
      WriteAll(fd, "\n");
      const std::string tail = "last lines of the log:\n" + LogTail(200);
      WriteAll(fd, tail.c_str());
      close(fd);
      WriteAll(2, "TriAevum: crash report written to ");
      WriteAll(2, path.data());
      WriteAll(2, "\n");
    }
    std::array<void *, 64> frames{};
    const int count = backtrace(frames.data(), static_cast<int>(frames.size()));
    backtrace_symbols_fd(frames.data(), count, 2);
  }
  // Then the default action (the window library's own handler hangs the
  // process after a crash instead of ending it).
  std::signal(signal, SIG_DFL);
  std::raise(signal);
}

void InstallSignals() {
  for (const int signal : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
    struct sigaction action {};
    action.sa_handler = FatalSignal;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
  }
}
#endif

void Watchdog() {
  // A freeze: no progress for 20 s while not paused. Reported once per
  // freeze; the game keeps running (it may recover).
  uint64_t last = gHeartbeats.load();
  auto lastChange = std::chrono::steady_clock::now();
  bool reported = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const uint64_t now = gHeartbeats.load();
    if (now != last || gHeartbeatPaused.load()) {
      last = now;
      lastChange = std::chrono::steady_clock::now();
      reported = false;
      continue;
    }
    if (!reported && last != 0 &&
        std::chrono::steady_clock::now() - lastChange > std::chrono::seconds(20)) {
      reported = true;
      WriteReport("freeze", "The game stopped making progress for more than 20 seconds "
                            "(it may still recover).",
                  "");
    }
  }
}

}  // namespace

void InstallTriAevumCrashReporter(std::string_view title) {
  gStart = std::chrono::steady_clock::now();
  const size_t titleSize = std::min(title.size(), gTitle.size() - 1);
  std::memcpy(gTitle.data(), title.data(), titleSize);
  gTitle[titleSize] = '\0';
  const std::string directory = DesktopDirectory().string();
  std::snprintf(gReportDirectory.data(), gReportDirectory.size(), "%s", directory.c_str());
  if (const char *log = std::getenv("TRIAEVUM_LOG_FILE")) {
    std::snprintf(gLogFile.data(), gLogFile.size(), "%s", log);
  }
  CopyInto(gContext, "starting");
#if defined(TRIAEVUM_CRASH_POSIX)
  InstallSignals();
#endif
  if (std::getenv("TRIAEVUM_NO_WATCHDOG") == nullptr) {
    std::thread(Watchdog).detach();
  }
}

void ReinstallTriAevumCrashSignals() {
#if defined(TRIAEVUM_CRASH_POSIX)
  InstallSignals();
#endif
}

void SetTriAevumCrashContext(std::string_view context) noexcept { CopyInto(gContext, context); }

void TriAevumHeartbeat() noexcept { gHeartbeats.fetch_add(1, std::memory_order_relaxed); }

void SetTriAevumHeartbeatPaused(bool paused) noexcept { gHeartbeatPaused.store(paused); }

std::string WriteTriAevumCrashReport(std::string_view kind, std::string_view message) {
  if (gReported.exchange(true)) {
    return {};
  }
  return WriteReport(kind, message, "");
}

}  // namespace Oot3dNativeGame
