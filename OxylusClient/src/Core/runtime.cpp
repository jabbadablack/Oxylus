#include <Core/App.hpp>
#include <Core/DefaultModules.hpp>
#include <Core/Enum.hpp>
#include <Core/Project.hpp>

// The game runtime. It differs from the editor by exactly one thing: it does not register the
// Editor module. Everything else - the window, the renderer, spawning a server and connecting to
// it - is the same, because a game and the editor are the same kind of client.

auto main(int argc, char** argv) -> int {
  auto app = ox::App(argc, argv);

  auto project = ox::Project{};
  const auto project_path = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
  auto title = std::string("Oxylus");

  if (!project_path.empty() && project.load(project_path)) {
    title = project.get_config().name;
  }

  app.with_name(title)
    .with_window(
      ox::WindowInfo{
        .title = title,
        .width = 1280,
        .height = 720,
        .flags = ox::WindowFlag::Centered | ox::WindowFlag::Resizable,
      }
    )
    .with_server()
    .with(ox::DefaultModules{})
    .run();

  return 0;
}
