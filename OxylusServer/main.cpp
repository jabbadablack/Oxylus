#include <Asset/AssetManager.hpp>
#include <Audio/AudioEngine.hpp>
#include <Core/AppCommandLineArgs.hpp>
#include <Networking/NetworkManager.hpp>
#include <Physics/Physics.hpp>
#include <Scripting/LuaManager.hpp>
#include <Server/Server.hpp>
#include <Utils/Log.hpp>
#include <fmt/format.h>

using namespace ox;

auto print_help() -> void {
  fmt::println("### Oxylus Server ###");
  fmt::println("  --port <n>            Port to listen on (default 7777).");
  fmt::println("  --scene \"path\"        Scene to load at startup.");
  fmt::println("  --assets \"path\"       Assets directory (default \"Resources\").");
  fmt::println("  --tick-rate <n>       Simulation ticks per second (default 60).");
  fmt::println("  --ticks <n>           Run this many ticks then exit. Omit to run until stopped.");
  fmt::println("  --help                Show this message.");
}

// Returns `fallback` when the flag is absent or has no value after it. Deliberately not
// ox::option: option::value() forwards `self` as a non-const rvalue, so it does not compile on a
// const option - worth fixing in Core/Option.hpp, but not from here.
auto arg_value(const AppCommandLineArgs& args, std::string_view name, std::string fallback = {}) -> std::string {
  auto index = args.get_index(name);
  if (!index.has_value()) {
    return fallback;
  }

  auto value = args.get(index.value() + 1);
  if (!value.has_value()) {
    return fallback;
  }

  return value->arg_str;
}

auto main(int argc, char** argv) -> int {
  const auto args = AppCommandLineArgs(argc, argv);

  if (args.contains("--help")) {
    print_help();
    return 0;
  }

  Log::init(argc, argv);

  const auto assets = arg_value(args, "--assets", "Resources");
  const auto tick_rate = std::stod(arg_value(args, "--tick-rate", "60"));
  const auto tick_limit = std::stoll(arg_value(args, "--ticks", "0"));

  auto server = Server(assets);
  server.add<LuaManager>();
  server.add<AssetManager>();
  server.add<AudioEngine>();
  server.add<Physics>();
  server.add<NetworkManager>();

  if (const auto result = server.init(); !result.has_value()) {
    OX_LOG_FATAL("Failed to start the server: {}", result.error());
    return 1;
  }

  server.set_tick_rate(tick_rate);

  if (const auto scene_path = arg_value(args, "--scene"); !scene_path.empty()) {
    if (!server.load_scene(scene_path)) {
      OX_LOG_FATAL("Could not load the requested scene; refusing to run an empty server.");
      static_cast<void>(server.deinit());
      return 1;
    }
  }

  OX_LOG_INFO("Server running at {} tick/s with {} scene(s).", tick_rate, server.scenes().size());

  // No networking yet: this loop exists to prove a real process ticks a world with no GPU. The
  // NetServer that makes it reachable arrives in the next phase.
  for (auto tick = 0_i64; tick_limit == 0 || tick < tick_limit; ++tick) {
    server.tick_owned();
  }

  OX_LOG_INFO("Server stopping.");

  if (const auto result = server.deinit(); !result.has_value()) {
    OX_LOG_ERROR("Failed to stop cleanly: {}", result.error());
    return 1;
  }

  return 0;
}
