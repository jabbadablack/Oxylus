#include "Utils/Log.hpp"

#include <filesystem>
#include <fmt/format.h>

namespace ox {
void Log::init(int argc, char** argv) {
  ZoneScoped;
  if (!std::filesystem::exists("logs"))
    std::filesystem::create_directory("logs");

  loguru::g_stderr_verbosity = loguru::Verbosity_INFO;

  loguru::g_preamble_date = false;

  loguru::init(argc, argv, {.verbosity_flag = nullptr});

  // Named per executable. The client and the server run from the same directory, so a shared
  // "latest.log" opened with Truncate means whichever starts second wipes the other's log - and
  // the one that gets wiped is usually the one that crashed.
  const auto name = argc > 0 && argv != nullptr ? std::filesystem::path(argv[0]).stem().string() : std::string("ox");

  loguru::add_file(fmt::format("logs/{}_everything.log", name).c_str(), loguru::Append, loguru::Verbosity_MAX);
  loguru::add_file(fmt::format("logs/{}_latest.log", name).c_str(), loguru::Truncate, loguru::Verbosity_INFO);

  // Flush every message instead of on a timer. abort() - from a failed assert, or a debug-STL
  // bounds check - skips the flush entirely, so a buffered log loses exactly the lines that say
  // why the process died. That matters more now that a crash can happen in either of two
  // processes, and it costs nothing a crashing program can no longer afford.
  loguru::g_flush_interval_ms = 0;
}
void Log::shutdown() { loguru::shutdown(); }

void Log::add_callback(
  const char* id,
  loguru::log_handler_t callback,
  void* user_data,
  loguru::Verbosity verbosity,
  loguru::close_handler_t on_close,
  loguru::flush_handler_t on_flush
) {
  loguru::add_callback(id, callback, user_data, verbosity, on_close, on_flush);
}

void Log::remove_callback(const char* id) { loguru::remove_callback(id); }

void Log::set_verbose() { loguru::g_stderr_verbosity = loguru::Verbosity_MAX; }
} // namespace ox
