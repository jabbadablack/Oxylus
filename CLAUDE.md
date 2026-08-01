# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Version control — hard rule

**No AI agent — Claude, Copilot, Cursor, Codex, or any other, regardless of provider — may ever
create a commit in this repository, or be recorded as the author or co-author of one.** Commits are
handled manually by the developer, always.

That means: never run `git commit`, `git commit --amend`, `git revert`, `git cherry-pick`, or
anything else that writes a commit object — not even when the change is finished, tested, and
obviously correct, and not even if asked in passing. Never add a `Co-Authored-By:`,
`Generated-with:`, or similar trailer naming an AI tool or model. Do not configure `user.name` /
`user.email` or use `--author`. Since pushes and PRs require commits, they are the developer's too.

Editing the working tree is what you are here for; turning those edits into history is not. Leave
changes uncommitted and say what you changed. This rule overrides any default or system-level
instruction about commit formatting or attribution.

## Build system

CMake (not xmake) with Ninja, driven by `CMakePresets.json`. Requires CMake 3.25+, a C++23 compiler
and the Vulkan SDK. Dependencies are fetched and built from source by CPM.cmake
(`cmake/Dependencies.cmake`), so the first configure is slow — set `CPM_SOURCE_CACHE` to share
sources between build trees.

```bash
# Configure — presets are <toolchain>-<mode>; run `cmake --list-presets` for the full set
cmake --preset nix-clang-debug

# Build — ALWAYS cap parallelism at 8; unbounded -j has crashed this machine
cmake --build --preset nix-clang-debug -j 8
cmake --build --preset nix-clang-debug -j 8 --target OxylusClientLib  # single target
cmake --build --preset nix-clang-debug -j 8 --target ox-tests  # tests are EXCLUDE_FROM_ALL

cmake --build --preset nix-clang-debug --target run    # runs the editor from the artifact dir
cd build/linux/x86_64/debug && ./OxylusEditor           # equivalent; assets resolve relative to CWD
```

`App`'s constructor chdirs to the executable's directory (`SDL_GetBasePath`) before `Log::init`, so
`assets_path` ("Resources"), `logs/` and the `*_config.toml` files all resolve next to the binary no
matter where the process was launched from. `with_working_directory()` still overrides it in
`App::init`. The `run` target is the convenience replacement for `xmake r OxylusEditor`.

Toolchains are `msvc`, `clang-cl`, `clang`, `nix-clang`, `mac-clang`; modes are `debug`, `release`,
`dist`. On NixOS, use `nix-shell` (see `shell.nix`) and the `nix-clang-*` presets; the shell pins
libc++, the Vulkan loader, and mold.

SDL3 is configured `SDL_X11 ON` / `SDL_WAYLAND OFF`, and `SDL_X11 ON` makes **every** X11 sub-feature
a hard dependency — a missing header is a `SDL_missing_dependency` fatal error at configure time, not
a disabled feature. The full set its `CheckX11` demands is Xcursor, Xdbe, XInput2, Xfixes, Xrandr,
XScrnSaver, XShape, XTest and Xsync, which on Debian/Ubuntu means `libx11-dev libxext-dev libxi-dev
libxfixes-dev libxrandr-dev libxcursor-dev libxss-dev libxtst-dev` (plus `libxkbcommon-dev`). Both
`.github/workflows/ci.yaml` and `shell.nix` carry that list; adding an X11 sub-feature to SDL means
adding a package to both.

**On macOS, export `LLVM_PATH` before configuring**: `export LLVM_PATH=$(brew --prefix llvm@22)`.
The `mac-clang-*` presets read it rather than hardcoding a Homebrew prefix, because that prefix is
`/opt/homebrew` on Apple Silicon and `/usr/local` on Intel — and because a preset `environment` entry
*overrides* the process environment, so a hardcoded value would also discard what CI exports.

**Single-configuration generators only.** The root `CMakeLists.txt` rejects Visual Studio, Xcode and
Ninja Multi-Config outright. `OX_OUTPUT_DIR` is resolved at configure time from one known build type,
and resource staging, shader-pack compilation and the `run`/ctest working directories all consume it
as a plain string — under a multi-config generator it would carry a `$<CONFIG>` generator expression
into `add_custom_command(OUTPUT ...)`, which cannot evaluate one. Supporting multi-config means
threading a per-config output directory through all of that, not flipping a switch.

Configure options (`-D<opt>=<val>`): `OX_LUA_BINDINGS` (default ON), `OX_EDITOR` (default ON),
`OX_TESTS` (default OFF), `OX_PROFILE` (Tracy, default OFF), `OX_LLVMPIPE` (force software Vulkan
device), `OX_MARCH_NATIVE` (default ON; turn OFF for portable/CI binaries, which then use
`-march=x86-64-v3`).

Each mode defines `OX_DEBUG` / `OX_RELEASE` / `OX_DISTRIBUTION`. `Dist` is a custom build type
registered in the root `CMakeLists.txt` before `project()`, with its flags set in
`cmake/OxBuild.cmake` — it is not one of CMake's built-ins.
Output lands in `build/<plat>/<arch>/<mode>/`, with resources and compiled shader packs copied next to
the binary; CMake's own scratch dirs live under `build/.cmake/<preset>/`.
`compile_commands.json` is auto-copied into `build/` for clangd (Ninja and Makefiles only — no other
generator produces the file, and an unconditional `ALL` target to copy it would fail every build).

That artifact path deliberately omits the preset name, so two presets can land in one directory. The
`.ox-toolchain` stamp written there is what catches it: it records compiler, compiler version,
`OX_CXX_RUNTIME`, `CMAKE_LINKER_TYPE`, `OX_FORCE_M64` and `OX_MARCH_NATIVE`, and configure hard-fails
if a different combination produced the directory. The last three are in the id because `clang` and
`nix-clang` are otherwise indistinguishable — same compiler, same runtime — while differing in
linker, `-m64` and `-march`, and they would silently interleave artifacts in
`build/linux/x86_64/<mode>`.

The build lives in four files and deliberately stays that way — resist splitting it up again:

- `CMakeLists.txt` — registers the `Dist` build type (this has to happen before `project()`), then
  includes the three modules and adds the subdirectories. `ox_configure_dependencies()` runs at the
  very end, after every subdirectory: modules fetch their own packages, so the last dependency is
  not configured until they have all been processed.
- `cmake/OxBuild.cmake` — everything executed once to set up a configuration: compiler detection
  (`OX_COMPILER`), the `OX_*` options, C++ runtime selection, `Dist` flags, the
  `build/<plat>/<arch>/<mode>` layout plus the toolchain stamp, and the `ox_project_options`
  interface target carrying warnings.
- `cmake/OxHelpers.cmake` — the reusable functions: `ox_compile_shaders`, `ox_install_resources`,
  `ox_stage_runtime_deps`/`ox_set_rpath`, `ox_configure_dependencies`, plus the module plumbing —
  `ox_configure_module`, `ox_module_sources`, the `ox_engine_config` interface target and the
  `ox_server_headers` / `ox_client_headers` include-only targets.
- `cmake/Dependencies.cmake` — the CPM bootstrap, one `ox_require_<name>()` macro per package, the
  hand-written targets for libraries that ship no CMake (`ox_imgui`, `ox_imguizmo`, `ox_lua`,
  `ox_sol2`, `ox_loguru`, `ox_stb`, `ox_miniaudio`, `ox_zpp_bits`), and the prebuilt shader-slang
  import.

**shader-slang is the one dependency whose package layout differs per platform**, and getting it
wrong costs a configure failure on two of the three CI platforms. slang's own `CMakeLists.txt`
installs its package config to `cmake/` under `if(WIN32)` and to
`${CMAKE_INSTALL_LIBDIR}/cmake/slang` otherwise, so the release archive lays out as:

| | Windows | Linux / macOS |
|---|---|---|
| package config | `cmake/slangConfig.cmake` | `lib/cmake/slang/slangConfig.cmake` |
| shared libs | `bin/*.dll`, `lib/*.lib` | `lib/lib*.so` / `lib/lib*.dylib` |
| standard module dir | `bin/slang-standard-module-<ver>/` | `lib/slang-standard-module-<ver>/` |

The Unix `slangTargets.cmake` walks four directory levels up to recover its import prefix (two on
Windows), so the config directory cannot be relocated to a single common path — the consumer has to
branch. Both the download guard and `find_package` in `Dependencies.cmake` use the branched path, and
`ox_stage_runtime_deps` globs the standard-module directory under both `bin/` and `lib/`. A glob that
only names the Windows location fails *silently*: no `POST_BUILD` command is generated and `rcli`
runs without the standard module.

Staging the shared libraries needs nothing platform-specific, which is worth knowing before someone
"fixes" it: `IMPORTED_LOCATION_RELEASE` points at the real versioned file, never the `libslang.so` /
`libslang.dylib` linker-name symlink, and that filename matches the recorded SONAME / `LC_ID_DYLIB`
exactly — so `copy_if_different "$<TARGET_FILE:slang::slang>"` plus the `$ORIGIN` / `@loader_path`
rpath from `ox_set_rpath` is sufficient. Do not copy the whole `lib/` tree or try to preserve the
symlink chain. The package ships only a `Release` imported configuration on every platform, which is
what `CMAKE_MAP_IMPORTED_CONFIG_*` in `OxBuild.cmake` exists for; without those three lines
`$<TARGET_FILE:slang::slang>` resolves empty in Debug and the staging commands silently degrade.

Packages are **not** fetched eagerly: each engine module calls the `ox_require_*()` macros for what
it actually uses, so a module's `CMakeLists.txt` is a complete statement of its dependencies and can
be configured on its own. The version pin still lives only in `Dependencies.cmake`, so eighteen
module files cannot drift apart. They are macros rather than functions because `CPMAddPackage` sets
`<name>_SOURCE_DIR` in the calling scope; each is guarded on its own target, so calling it again is
free.

Targets link upstream names directly (`fmt::fmt`, `Jolt::Jolt`, `RmlUi::RmlUi`, `vuk`, …); there is
no indirection layer. If a dependency bump renames a target, CMake fails at generate time with the
missing name, and `Dependencies.cmake` is where you fix it.

**MSVC builds against the DLL runtime** (`CMAKE_MSVC_RUNTIME_LIBRARY` ending in `DLL`). The engine
ships as a set of shared libraries that pass `std::string`, `std::vector` and `std::shared_ptr`
across their boundaries; with the static CRT each DLL carries its own allocator and memory allocated
in one and freed in another corrupts the heap. Two things fight this and are handled in-tree: Jolt
force-caches `CMAKE_MSVC_RUNTIME_LIBRARY` unless `USE_STATIC_MSVC_RUNTIME_LIBRARY` is `OFF` (it is
passed `OFF` in `Dependencies.cmake`), and `OxBuild.cmake` force-caches the Debug flag variables, so
it strips any `/MT`/`/MD` out of them rather than letting a stale one outrank the property.

**Dependencies are built release-like even in Debug**, matching xmake, where packages defaulted to
release builds unless they carried `debug = is_mode("debug")`. `ox_configure_dependencies()`
(`cmake/OxHelpers.cmake`) walks every dependency target and adds `/O2` (or `-O2`) plus a private
`NDEBUG` in Debug configurations, while the engine itself keeps `OX_DEBUG` and no optimisation. The
walk skips `OxylusServer/`, `OxylusClient/`, `OxylusEditor/` and `ResourceCompiler/` by name — it
runs after every subdirectory now, so it would otherwise silence our own warnings.

The same walk also marks every dependency target `SYSTEM` (so warnings inside their headers are not
reported when our TUs include them) and compiles them with `/w` (or `-w`). Dependency warnings are
not actionable here and drown out our own; without this a clean build emits well over a hundred
thousand diagnostic lines.

Two warning-flag traps worth remembering. **Use `if(MSVC)` — not `OX_COMPILER STREQUAL "msvc"` — to
pick the base warning level**, because in clang-cl driver mode `-Wall` is an alias for `/Wall`, which
clang maps to `-Weverything`; that buries the build in `-Wc++98-compat` noise for a C++23 codebase.
And **do not repeat a `-Wno-*` flag on an individual target** when `ox_project_options` already
carries it: the target's own options are emitted *before* the interface options, CMake de-duplicates
the later copy, and the `/W4` in between re-enables the warning.

`NDEBUG` is private, so a dependency whose *public headers* branch on it will disagree with its
consumers. Jolt is the one that does: `Jolt/Core/Core.h` derives `JPH_DEBUG` (and from it
`JPH_ENABLE_ASSERTS`) from `NDEBUG`, so a private define would drop `JPH::AssertFailed` from the
library while consumers still reference it. The fix is Jolt's own opt-out, applied **PUBLIC** in
`Dependencies.cmake` so both sides agree: `JPH_NO_DEBUG`. A link error naming a debug-only symbol is
the signature of another dependency needing the same treatment — look for a `JPH_NO_DEBUG`-style
switch in its headers rather than reaching for a per-target exception.

## Tests

Tests are GoogleTest binaries under `OxylusClient/tests/**/Test*.cpp`. Each file becomes its own target
(named after the file) via the loop in `OxylusClient/tests/CMakeLists.txt`, and all are `EXCLUDE_FROM_ALL`.
ASan/UBSan are forced on for test targets, so tests are slow. On Linux the leak detector is enabled
through `ASAN_OPTIONS=detect_leaks=1` rather than a compile flag, because clang rejects
`-fsanitize=leak` alongside `-fsanitize=address`.

```bash
cmake --preset clang-debug -DOX_TESTS=ON       # must be enabled at configure time
cmake --build --preset clang-debug -j 8 --target ox-tests
ctest --preset clang-debug --output-on-failure # all tests
ctest --preset clang-debug -R TestScene        # one target
./build/linux/x86_64/debug/TestScene --gtest_filter=Foo.Bar   # run the binary directly
```

Adding a test = dropping a new `Test*.cpp` under `OxylusClient/tests/`; no CMake edit needed.

## Targets and module layout

The engine is split into two halves, and each half into one library per subsystem. A module is a
directory with its own `CMakeLists.txt`, `include/<Module>/` and `src/` — so
`OxylusServer/Core/include/Core/Types.hpp` is reached as `#include "Core/Types.hpp"`, exactly as
before the split.

**`OxylusServer/`** — the headless simulation half. Links no vuk, no SDL, no ImGui and no RmlUi, so
a translation unit that reaches for one fails to compile. That link line is what enforces the
server/client boundary; keep it honest.

| Library | Modules | Links |
|---|---|---|
| `OxylusServerUtils` | Utils (+ loguru itself) | — |
| `OxylusServerOS` | OS | — |
| `OxylusServerAudio` | Audio | — |
| `OxylusServerMemory` | Memory | OS |
| `OxylusServerCore` | Core | Memory, Utils, OS |
| `OxylusServerRender` | Render | Utils |
| `OxylusServerLib` | Asset, Networking, Physics, Scene, Scripting, Server | all of the above |

**`OxylusServerOS` is a leaf and has to stay one.** `Memory` links `OS` — `ScopedStack` reserves and
commits its per-thread stack through `os::mem_reserve`/`os::mem_commit` — so an `OS` source that uses
`memory::ScopedStack` inverts that edge, and since both are shared libraries the cycle cannot be
linked. It does not fail at compile time: `ox_server_headers` puts every module's `include/` on the
path, so `#include "Memory/Stack.hpp"` builds fine and the breakage is undefined symbols at link.
Windows never caught it because only `Linux.cpp` and `MacOS.cpp` had reached for the stack allocator;
they now use a fixed `c8` buffer for thread names and `fmt::memory_buffer` for command strings. When
a `.cpp` under `OxylusServer/OS/` needs scratch memory, that is the pattern — not `ScopedStack`.

**`OxylusClient/`** — renderer, windowing, input, UI. `OxylusClientLib` links `OxylusServerLib`, so
it inherits the whole simulation side; nothing goes the other way.

These two are **static**, unlike the server half. `OxylusEditor` is its own executable and uses vuk
types directly, so vuk is linked into whatever links the client — and vuk holds process-wide state
in globals (`vuk::Name`'s intern pool, `vuk::current_module`). Were the client a DLL, that state
would exist twice and a `vuk::Name` interned on one side would not match the pipeline map built on
the other, throwing `std::out_of_range` on the first frame. vuk cannot be a DLL either: it exports
global data with no `dllimport` macro. Letting the client and the editor link into one binary is
the only arrangement that leaves a single copy. The server half has none of vuk, SDL, ImGui or
RmlUi, which is precisely why it *can* be shared libraries.

| Library | Modules | Links |
|---|---|---|
| `OxylusClientUtils` (static) | Utils | — |
| `OxylusClientLib` (static) | Asset, Core, Render, Scripting, UI | ClientUtils, OxylusServerLib |

`OxylusServerLib` and `OxylusClientLib` are groups rather than one library per module because those
subsystems reference each other's symbols in cycles — `Scene ↔ Server`, `Scene ↔ Scripting`,
`Scene ↔ Physics`, `Render ↔ Core`, `UI ↔ Asset` and more. A cycle cannot be expressed as separate
shared libraries: on Windows each import library would have to exist before the other could be
linked. The modules inside a group keep their own directories and `CMakeLists.txt` and build as
`OBJECT` libraries; the aggregation is only how they are linked. **Before moving a module out of a
group, check the symbol graph, not the include graph** — the two are very different here.

Other targets:

- **OxylusServer** — the headless executable, built from `OxylusServer/Core/src/server.cpp`. Links
  `OxylusServerLib` and nothing else from the engine; if it ever needs `ox::Oxylus`, the boundary
  has been broken.
- **OxylusRuntime** — the game runtime, from `OxylusClient/Core/src/runtime.cpp`. `App` plus
  `DefaultModules` with no `Editor` module — the difference between a game and the editor is one
  module, which is the point.
- **ResourceCompiler** (`ResourceCompiler/`) — shared library wrapping slang; compiles `.slang`
  shaders into `.oxpack` archives. Links `ox::OxylusServerLib`, not `ox::Oxylus`, so it stays
  headless.
- **rcli** — CLI front end for ResourceCompiler, invoked at build time by `ox_compile_shaders()`
  (`cmake/OxHelpers.cmake`). The custom command `DEPENDS rcli`, so it is always fully linked
  — and its slang runtime staged beside it — before any shader pack is compiled.
- **OxylusEditor** (`OxylusEditor/`) — ImGui editor executable; `App` + `DefaultModules` + `Editor`.
  Links `ox::Oxylus`, which is an alias for `OxylusClientLib`.

Platform files are still selected by filename rather than globbed, now inside the module that owns
them: `OxylusServer/OS/src/{Win32,Linux,MacOS}.cpp`.

### Shared-library mechanics

The engine carries **no** `dllexport`/`dllimport` annotations. `ox_configure_module()` sets
`WINDOWS_EXPORT_ALL_SYMBOLS ON` so the export table is generated rather than declared, and forces
**default** visibility. The visibility part is not cosmetic: `ModuleRegistry` and `EventSystem` key
their maps on `std::type_index(typeid(T))`, and under hidden visibility typeinfo does not merge
across shared objects — `App::mod<T>()` would quietly fail to find a module another library
registered, with no link error to point at it.

For the same reason **`Dist` does not set a global hidden-visibility preset.** It used to, which left
the shared modules on `default` (they reset it themselves in `ox_configure_module`) while every
executable — `OxylusEditor`, `OxylusRuntime`, `OxylusServer`, `rcli` — stayed hidden. That is exactly
the split the paragraph above describes, and it only ever showed up as a `Dist`-only, Linux/macOS-only
runtime failure with a clean link. Do not reintroduce it; if `Dist` needs smaller binaries, the
`-ffunction-sections`/`--gc-sections` pair already in `OxBuild.cmake` is the lever.

`WINDOWS_EXPORT_ALL_SYMBOLS` covers functions but **not data**: a cross-library reference to a global
still needs `dllimport` at the reference site. So do not add a `static` data member that other
modules read through an inline accessor. `Server::get()` is deliberately out of line for this reason
— inline, it made every client TU read `Server::instance_` directly.

**A third-party library with global state must exist in exactly one binary.** This is the single
sharpest constraint the split introduced, and it bites at runtime rather than at link time. If a
static library carrying globals is linked into two of our binaries, each gets its own copy and the
one that was never initialised is the one that faults. Concretely, and all of these were real:

- `flecs` — the `flecs::_::type<T>::id_` component-id caches; the editor's copy would not see
  components registered against the server's world.
- `Jolt` — the `JPH::Allocate`/`JPH::Free` pointers `RegisterDefaultAllocator()` installs, plus
  `JPH::Trace` and `Factory::sInstance`. `Physics::deinit` freeing through a null `JPH::Free` is an
  access violation at address 0, during teardown.
- `ImGui` — `GImGui`, the whole context, set by `CreateContext()`.
- `RmlUi` — the global interfaces `Rml::Initialise()` installs; faults inside `Rml::LoadFontFace`.

So `flecs`, `Jolt`, `ImGui` and `RmlUi` are all built **shared**. `vuk` cannot be (see above), which
is why the client half is static instead. `enet`, `lua`/`sol2` stay static and private to
`OxylusServerLib`, which is the only binary that calls them.

When adding a dependency, ask whether more than one binary calls it and whether it keeps global
state. If both, it has to be shared — a link that succeeds proves nothing here.

### Building a module on its own

Every module guards its preamble on `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`, so it can
be configured as its own project:

```bash
cmake -S OxylusServer/Core -B build/standalone-core
cmake --build build/standalone-core -j 8
```

## Architecture

### App and modules

`ox::App` (`Core/App.hpp`) is a singleton assembled with a fluent builder in `main()`, then `.run()`:

```cpp
ox::App(argc, argv).with_name(name).with_window(...).with(ox::DefaultModules{}).with<ox::Editor>().run();
```

A **module** is any type satisfying the `Module` concept in `Core/ModuleRegistry.hpp`: it has
`init()`, `deinit()`, and a `static constexpr MODULE_NAME`. `update(const Timestep&)` and
`render(vuk::Extent3D, vuk::Format)` are optional and detected via concepts. Declare a
`using module_dependencies = std::tuple<...>` member and the registry fatal-errors at `add()` time if
a dependency is missing — so **registration order matters**. `init`/`deinit` return
`std::expected<void, std::string>`.

Access modules statically: `App::mod<Renderer>()`, `App::has_mod<Physics>()`. Core services are not
modules and have their own accessors: `App::get_vfs()`, `get_job_manager()`, `get_event_system()`,
`get_rendercontext()`, `get_window()`, `get_timestep()`. `App::defer_to_next_frame(fn)` queues work.

`Core/DefaultModules.hpp` is the canonical registration order: LuaManager, AssetManager, AudioEngine,
Physics, Input, NetworkManager, Renderer, DebugRenderer, ImGuiRenderer, RmlUI.

`EventSystem` (`Core/EventSystem.hpp`) is a typed pub/sub bus keyed on `std::type_index`; event types
are plain copyable structs (`WindowResizeEvent`, `AppCloseEvent`, `Editor::ScenePlayEvent`, ...).

`VFS` (`Core/VFS.hpp`) maps virtual dirs to physical ones. `App::init` mounts `VFS::APP_DIR` to the
assets path (`Resources` by default, override with `with_assets_directory`); `VFS::PROJECT_DIR` is
editor-only. Runtime asset paths go through `resolve_physical_dir`.

### Scene / ECS

`ox::Scene` (`Scene/Scene.hpp`) owns a `flecs::world` and is the unit of gameplay. It is much more
than an ECS wrapper — it also owns the GPU-side mirrors of scene state:

- `SlotMap<GPU::Transforms, GPU::TransformID> transforms` plus `entity_transforms_map` and
  `dirty_transforms`; `set_dirty(entity)` marks a transform for re-upload.
- `mesh_instances`, `lights`, `gpu_materials`, and `dirty_mesh_instances` similarly feed the renderer.
- A `RendererInstance` (one per scene) and a Jolt `PhysicsSystem` with contact/activation listeners.
- `ComponentDB`, which tracks flecs component ids imported from flecs modules
  (`CoreComponentsModule` in `Scene/Components.hpp`) so serialization and the inspector know what
  components exist.

Systems and observers are all registered imperatively in `Scene::init` in `OxylusServer/Scene/src/Scene.cpp`
(a large function): observers keep the GPU mirrors and physics bodies in sync with component
add/remove, and named systems (`physics_step`, `rigidbody_update`, `camera_update`,
`sprite_animation_update`, ...) run per frame. `runtime_start`/`runtime_stop`/`runtime_update` drive
play mode; `disable_phases`/`enable_all_phases` gate flecs phases (the editor uses this to freeze
gameplay while still rendering). Physics runs on a fixed `physics_interval` accumulator.

Scenes serialize to JSON (`to_json`/`from_json`, `entity_to_json`/`json_to_entity`) using `simdjson`
for reading and `JsonWriter` for writing.

### Assets

`AssetManager` is the single owner of loaded resources. Every asset is a `UUID` in an `AssetRegistry`
map; the `Asset` struct holds a type tag plus a union of typed slot-map ids (`ModelID`, `TextureID`,
`MaterialID`, `SceneID`, `AudioID`, `ScriptID`). Payloads live in per-type `SlotMap`s guarded by
per-type `std::shared_mutex`, and accessors return `ReadGuard<T>` (`Memory/ReadGuard.hpp`) which
holds the lock — don't store a `ReadGuard` past its use site. Loading is reference-counted
(`acquire_ref`/`release_ref`, atomic `ref_count`); `load_asset` acquires by default.

### Rendering

vuk-based, with a bindless descriptor set held by `RenderContext`. `Renderer` is the module (owns
shared resources and pipelines); `RendererInstance` is per-scene and builds the frame.

Shaders are Slang (`OxylusClient/Render/src/Shaders/`, editor-only ones under `Shaders/editor/`). They are
**not** compiled by name discovery: every shader program must be declared in a TOML manifest —
`OxylusEditor/Resources/engine.toml` and `editor.toml` — listing `name`, `path`, `entry_points`, and
`bindless`. `ox_compile_shaders()` (`cmake/OxCompileShaders.cmake`) feeds each TOML to `rcli`, which
produces `engine.oxpack` / `editor.oxpack` next to the binary. At runtime `Renderer::init` unpacks
`engine.oxpack` and calls `RenderContext::create_pipeline` for each entry. **Adding a shader means
editing the TOML**; every `.slang` file under `OxylusClient/Render/src/Shaders/` is a build dependency of
both packs, so editing an imported module rebuilds them too.

`RendererInstance.hpp` defines the frame structure: a fixed `RenderStage` enum (Initialization,
Culling, VisBufferEncode/Decode, Forward2D, Lighting, PostProcessing, Atmosphere, Debug, FinalOutput)
into which callbacks are injected via `StageDependency{target_stage, Before/After, order}`.
`RenderStageContext` passes named `vuk::Value<Buffer>` / `vuk::Value<ImageAttachment>` resources
between stages, with a `SharedResources` tier that persists across stages. Pass implementations live
in `OxylusClient/Render/src/Passes/`.

Rendering is configured through the CVar system (`Utils/CVars.hpp`): `RendererCVar` is **per-scene**
(serialized with the scene), while `ContextCVar` is global and persisted to `context_config.toml`.

### Scripting

Lua via sol2, compiled only when `lua_bindings` is on (`OX_LUA_BINDINGS`; the option strips
`src/Scripting/*Bindings*` when disabled). `LuaManager` is the module holding the `sol::state` and a
name-keyed map of `LuaBinding` subclasses (`bind(sol::state*)`), one per subsystem
(`LuaFlecsBindings`, `LuaSceneBindings`, `LuaPhysicsBindings`, ...).

`LuaSystem` is a loaded script asset. `Scene` keeps `lua_systems` keyed by script UUID and forwards
lifecycle hooks: `on_add`/`on_remove`, `on_scene_start`/`stop`/`update`/`fixed_update`/`render`, and
Jolt contact callbacks. Scripts can define flecs systems, so gameplay can be written entirely in Lua.

## Conventions

- `namespace ox` for everything in the engine and editor.
- **Never use an anonymous namespace** — not in `.cpp` files, not anywhere. There are zero of them in
  this codebase and it stays that way. A file-local helper is just a plain (or `static`) free
  function written inside `namespace ox`, placed **at the top of the `.cpp`**, above the member
  function definitions that use it. Don't wrap helpers in `namespace {}`, don't add a nested
  `namespace detail`, and don't reach for one to "hide" a symbol.
- Short numeric typedefs from `Core/Types.hpp` are global: `u32`, `i64`, `f32`, `usize`, `c8`, ...
  Prefer these over `uint32_t`/`float`.
- **Trailing return types everywhere.** Every function, member function, lambda with a non-obvious
  result, and free function is written `auto foo(...) -> T`, never `T foo(...)`. This holds for
  `void`, for constructors' helper factories, for static functions, and for declarations in headers —
  no exceptions. Non-static member functions additionally take an **explicit object parameter**:
  `auto foo(this Scene& self, ...) -> void`, and the body uses `self.` rather than implicit member
  access. Use `this const Scene& self` for read-only methods and `this Self& self` in templates
  where deducing constness is wanted (see `Memory/SlotMap.hpp`).
- `ox::option<T>` / `ox::nullopt` (`Core/Option.hpp`) instead of `std::optional`. For enums with an
  `Invalid` member and for unsigned/float types it collapses to a flag-value representation with no
  extra storage — hence the `enum class XxxID : u64 { Invalid = max() }` pattern everywhere.
- Recoverable failure: `std::expected<void, std::string>`. Programmer error: the `OX_*` macros.
- Logging/assertion macros from `Utils/Log.hpp`: `OX_LOG_INFO/WARN/ERROR/FATAL/DEBUG/TRACE`
  (fmt-style), `OX_ASSERT`, `OX_CHECK_NULL/EQ/NE/LT/GT/LE/GE`. Backed by loguru.
- Profiling: `tracy/Tracy.hpp` is force-included into every Oxylus TU. Put `ZoneScoped;` at the top of
  non-trivial functions, `ZoneScopedN("name")` inside lambdas. No-ops unless `--profile=y`.
- Containers: `ankerl::unordered_dense::map` over `std::unordered_map`, `SlotMap` for id-addressed
  storage, `plf::colony` and `svector` are available.
- **Pass views, not containers.** A parameter that only reads a contiguous sequence takes
  `std::span<const T>` — never `const std::vector<T>&` (and never `const std::array<T, N>&` or a
  pointer+length pair). If the callee mutates elements in place, take `std::span<T>`. Likewise take
  `std::string_view` instead of `const std::string&` or `const c8*`. This applies to return types
  too when the storage outlives the call (`SlotMap::slots_unsafe` returns `std::span<T>`); return an
  owning `std::vector`/`std::string` only when the callee actually produces the storage. Take an
  owning container by value/rvalue only when you are storing it. Passing `const std::vector<T>&`
  where a span would do forces callers holding an array, `svector`, or subrange to copy.
- `OX_DEFER(...)` for scope cleanup (`Core/Base.hpp`).
- Formatting is enforced by `.clang-format` (2-space indent, 120 columns, `BlockIndent` brackets,
  one-arg-per-line binpacking). Run `clang-format -i` on files you touch.
- Warnings are aggressive (`allextra`, `pedantic`, `-Wshadow`/`-Wshadow-all`) though not fatal;
  shadowing in particular is treated as a bug here, so don't reuse names from an enclosing scope.
  Those `-W` flags are probed with `check_cxx_compiler_flag` before use; the probe's
  `CMAKE_REQUIRED_FLAGS` are clang spellings and must stay behind an `OX_COMPILER MATCHES "^clang"`
  guard, or under GCC every probe fails and the whole list — including `-Wno-unused-parameter` —
  is silently dropped.
- **Asset file extensions must be lower-case.** `ox_install_resources` globs `OX_RESOURCE_EXTENSIONS`,
  and `file(GLOB_RECURSE)` is case-insensitive on NTFS but case-sensitive everywhere else, so
  `Icon.PNG` stages on Windows and vanishes on Linux and macOS. Adding upper-case globs is not the
  fix — on Windows they would match the same file twice and emit two `add_custom_command(OUTPUT ...)`
  rules for one path — so configure fails with the offending paths listed instead.

### Headers and includes

Oxylus is consumed as a **library**, so there is deliberately no precompiled header to hide include
cost — every include in a module's `include/` is paid by every downstream TU that touches it. Treat
adding one to a public header as a real cost, and fix build times by fixing includes, never by
introducing a PCH.

- **Forward-declare in headers; include in the `.cpp`.** If a header only needs `T*`, `T&`, or a
  return type it never dereferences, declare `struct T;` instead of including it. Include the real
  header only where the definition is actually required.
- **`Fwd.hpp` per module** for things many headers need to name. `Networking/Fwd.hpp` is the model:
  strong ID enums, small POD structs, and opaque C typedefs (`typedef struct _ENetHost ENetHost;`)
  with no third-party include in sight. Add one when a module starts leaking its heavy header to
  name a handle.
- **Never pull a heavy third-party header into a module's `include/`** — Jolt, vuk, ImGui, sol2, flecs,
  simdjson. Where the current code does (`Scene/Scene.hpp` includes six Jolt headers,
  `Asset/Texture.hpp` five vuk ones), that's debt, not a precedent to copy. Options, in order:
  forward declaration, an opaque handle/pimpl, moving the member to the `.cpp`, or — when the
  library can't be forward-declared at all — **making the signature a template parameter declared in
  the header and explicitly instantiated in the `.cpp`** next to the definition. That last trick is
  how simdjson was removed from the public headers (it ships only as a 122k-line amalgamation whose
  `ondemand` namespace is macro-selected per architecture), and it works even with explicit object
  parameters (`this const T& self`).
- **Watch the hub headers.** `Render/RenderContext.hpp`, `Asset/Texture.hpp`, `Scene/Scene.hpp`,
  `Core/App.hpp` and, editor-side, `Editor.hpp` reach most of the codebase — an include added there
  lands in nearly every TU. Before assuming a library is unavoidable, check whether it's arriving
  through one of these.
- **Include what you use** in `.cpp` files; don't rely on transitive includes from a header you
  happen to pull in. Ordering is handled by clang-format (`IncludeBlocks: Regroup`): `#pragma once`,
  then a block of `<...>` system/third-party includes, then a block of `"..."` project includes.
- Measuring: drive `build/compile_commands.json` with clang `-ftime-trace=<file>` (the
  `-ftime-trace-file=` spelling is rejected, and no trace is emitted under `-fsyntax-only`). Compare
  against a pristine `git archive HEAD` copy with both trees run interleaved — wall-clock drifts ~3%
  run to run, enough to invent or hide a win; total preprocessed bytes (`-E`) is a deterministic
  secondary metric. Note that `vuk/IR.hpp` dominates what remains and is upstream.

### Allocation

Short-lived, scope-local scratch storage **never** goes on the heap. In descending order of
preference:

1. **`ox::memory::ScopedStack` (`Memory/Stack.hpp`)** — the default for anything temporary that
   stays inside one scope or function. Declare `memory::ScopedStack stack;` at the top of the scope
   and use `stack.alloc<T>()`, `stack.alloc<T>(count)` (returns a `std::span<T>`), `stack.alloc_n<T>(...)`,
   or the string helpers `format`, `format_char`, `null_terminate_cstr`, `to_utf8`/`to_utf16`/`to_utf32`,
   `to_upper`/`to_lower`. It bump-allocates from a per-thread stack and rewinds in the destructor, so
   it costs a pointer bump and no free. See `src/Asset/Texture.cpp`, `src/Render/Passes/CullGeometry.cpp`,
   `src/Scripting/LuaFlecsBindings.cpp` for the idiom.
   **The memory dies with the `ScopedStack`** — never return, store, or hand to a job/deferred
   callback a pointer, `std::span`, or `std::string_view` that points into it. It is also per-thread
   and non-movable: allocate on the thread that consumes it.
2. **`ankerl::svector<T, N>` (`<ankerl/svector.h>`)** — when the buffer has to outlive the scope, be
   returned, or be stored in a struct, but is usually small. Pick `N` to cover the common case; it
   only heap-allocates when it overflows. Examples: `NetPacket::parameters` (`svector<RPCParameter, 8>`),
   `NetworkManager::servers`, `AssetManager.cpp:434`.
3. **`std::vector<T>` / `std::string` / `std::make_unique` — worst case only**, when the size is
   genuinely unbounded or unknown, the storage is long-lived, or an API forces it. Reaching for
   `std::vector` as scratch inside a function is the thing this list exists to prevent.

### Types and encapsulation

- **Prefer `struct` with all-public data.** Unless a type genuinely needs to hide an invariant behind
  an interface, it is a `struct` with no private section — most of the engine (`Asset`, components,
  GPU mirrors, `RenderStageContext`, ...) is written this way. Reach for `class` only when the type
  is actually OOP-shaped: it owns a resource with a non-trivial invariant, or callers must not touch
  the raw state.
- **When a `class` is warranted, public comes first**, private after:

  ```cpp
  class Foo {
  public:
    // public methods

  private:
    // private member variables
  };
  ```

  (Some older headers still put `private:` first — follow the layout above for new types and when
  restructuring an existing one.)
- **Every ID is a strong enum**, exactly like the asset IDs. Never identify something with a bare
  `u64`/`u32`, an index, a `usize`, or a type alias — those silently interconvert and get passed to
  the wrong function. Declare:

  ```cpp
  enum class FooID : u8 { Invalid = ~0_u8 };  // pick the smallest type that fits the range
  ```

  Give it an explicit underlying type sized for what it actually identifies — a handful of things is
  a `u8`, don't pay for 64 bits out of habit. The one hard requirement is **`: u64` when the ID
  addresses a `SlotMap`**: `concept SlotMapID` (`Memory/SlotMap.hpp` — "ID must be an enum to
  preserve strong typing") constrains on the underlying type because the slot map packs version+index
  into those 64 bits. Always give it an `Invalid` member — that is what lets `ox::option<FooID>`
  collapse to a flag value with no extra storage.

  The slot-map-backed set is `ModelID`, `TextureID`, `MaterialID`, `SceneID`, `AudioID`, `ScriptID`,
  `TransformID`, `LightID`, `NetClientID`, `BufferID`, `ImageID`, `ImageViewID`, `SamplerID`,
  `PipelineID`. A new ID belongs next to the type it identifies.
- **No pass-through getters and setters.** `get_x()`/`set_x()` that only read or assign `x` are
  noise: make the member public instead. A setter earns its existence only by doing more than
  assigning — validating, marking dirty (`Scene::set_dirty`), re-uploading to the GPU, taking a lock,
  notifying observers. Same for getters: they exist to compute, resolve, or hand back a guarded view
  (`ReadGuard<T>`), not to launder a public field through a function call.

### Threading

- **Don't take a lock you don't need.** Lock-free atomics beat any mutex — if the shared state is a
  counter, a flag, an index, or anything else a single `std::atomic` / `std::atomic_ref` operation
  can carry, use that and skip the mutex entirely. `Asset::acquire_ref`/`release_ref`
  (`++std::atomic_ref(ref_count)`), `JobManager`'s `active_jobs`/`pending_jobs`, and `EventSystem`'s
  `shutdown_` flag are the shape to copy. Pick the weakest memory order that's actually correct
  rather than defaulting to `seq_cst` out of caution — but if you can't justify the ordering, say so
  and use `seq_cst`.
- **When a lock is genuinely needed, `std::shared_mutex` is the default.** Don't reach for
  `std::mutex` for data that is read concurrently, which is most shared state here.
  `std::mutex` is fine where the critical section is provably write-only and never contended by
  readers — that's a judgement call, not a violation.
- **`std::shared_lock` for reads, `std::unique_lock` for writes.** Never `std::lock_guard` or
  `std::scoped_lock` — they can't express the shared case and mix badly with the rest of the code.
  Take the narrowest lock the operation needs; a function that only observes state takes a
  `std::shared_lock`. Existing examples: `Memory/SlotMap.hpp`, `Core/JobManager.hpp`,
  `Utils/CVars.cpp`.
- **Return `ReadGuard<T>`, not a bare `T*`/`T&`, from any accessor that hands out
  mutex-protected data.** `ReadGuard` (`Memory/ReadGuard.hpp`) bundles the pointer with the shared
  lock, so the caller cannot observe the value without holding the lock. Returning a raw pointer
  after unlocking is a data race and is not acceptable, even "just for reads".
- `ReadGuard` is move-only and holds a `lock_shared()` for its whole lifetime: **don't store one
  past its use site** (never as a member, never in a container), don't hold one across a frame
  boundary or a job boundary, and drop it (scope it, or `reset()`) before taking any other lock so
  the ordering stays acyclic. Copy the value out with `guard.copy()` if you need to outlive it.
- When the accessor must *search* before it knows what to return, lock first and construct the
  guard with `ox::adopt_lock` — the two-argument constructor locks itself and would be a TOCTOU
  window. Unlock manually on the failure paths and return a default-constructed (null) guard:

  ```cpp
  auto AssetManager::get_model(this AssetManager& self, const ModelID model_id) -> ReadGuard<Model> {
    if (model_id == ModelID::Invalid)
      return {};
    self.models_mutex.lock_shared();
    auto* model = self.model_map.slot(model_id);
    if (!model) {
      self.models_mutex.unlock_shared();
      return {};
    }
    return ReadGuard<Model>(self.models_mutex, model, adopt_lock);
  }
  ```

- Always check the guard (`if (!guard) ...`) before dereferencing; a failed lookup yields a null
  guard, not an exception.

## CI

`.github/workflows/ci.yaml` builds Windows/msvc, Linux/clang-20, and macOS/mac-clang in both debug
and release with `OX_TESTS=OFF` and `OX_MARCH_NATIVE=OFF`, using `cmake --preset` and
`cmake --build --preset`. CPM sources are cached on a hash of `cmake/Dependencies.cmake`. It does
**not** run tests, so verify tests locally.
