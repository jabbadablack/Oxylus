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
cmake --build --preset nix-clang-debug -j 8 --target Oxylus    # single target
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

Configure options (`-D<opt>=<val>`): `OX_LUA_BINDINGS` (default ON), `OX_EDITOR` (default ON),
`OX_TESTS` (default OFF), `OX_PROFILE` (Tracy, default OFF), `OX_LLVMPIPE` (force software Vulkan
device), `OX_MARCH_NATIVE` (default ON; turn OFF for portable/CI binaries, which then use
`-march=x86-64-v3`).

Each mode defines `OX_DEBUG` / `OX_RELEASE` / `OX_DISTRIBUTION`. `Dist` is a custom build type
registered in `cmake/OxBuildTypes.cmake` — it is not one of CMake's built-ins.
Output lands in `build/<plat>/<arch>/<mode>/`, with resources and compiled shader packs copied next to
the binary; CMake's own scratch dirs live under `build/.cmake/<preset>/`.
`compile_commands.json` is auto-copied into `build/` for clangd.

The build lives in four files and deliberately stays that way — resist splitting it up again:

- `CMakeLists.txt` — registers the `Dist` build type (this has to happen before `project()`), then
  includes the three modules and adds the subdirectories.
- `cmake/OxBuild.cmake` — everything executed once to set up a configuration: compiler detection
  (`OX_COMPILER`), the `OX_*` options, C++ runtime selection, `Dist` flags, the
  `build/<plat>/<arch>/<mode>` layout plus the toolchain stamp, and the `ox_project_options`
  interface target carrying warnings.
- `cmake/OxHelpers.cmake` — the reusable functions: `ox_compile_shaders`, `ox_install_resources`,
  `ox_stage_runtime_deps`/`ox_set_rpath`, and `ox_make_dependencies_release_like`.
- `cmake/Dependencies.cmake` — the CPM bootstrap, every `CPMAddPackage`, the hand-written targets
  for libraries that ship no CMake (`ox_imgui`, `ox_imguizmo`, `ox_lua`, `ox_sol2`, `ox_loguru`,
  `ox_stb`, `ox_miniaudio`, `ox_zpp_bits`), and the prebuilt shader-slang import.

Targets link upstream names directly (`fmt::fmt`, `Jolt::Jolt`, `RmlUi::RmlUi`, `vuk`, …); there is
no indirection layer. If a dependency bump renames a target, CMake fails at generate time with the
missing name, and `Dependencies.cmake` is where you fix it.

**Dependencies are built release-like even in Debug**, matching xmake, where packages defaulted to
release builds unless they carried `debug = is_mode("debug")`. `ox_make_dependencies_release_like()`
(`cmake/OxHelpers.cmake`) walks every dependency target and adds `/O2` (or `-O2`) plus a private
`NDEBUG` in Debug configurations, while the engine itself keeps `OX_DEBUG` and no optimisation.

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

Tests are GoogleTest binaries under `Oxylus/tests/**/Test*.cpp`. Each file becomes its own target
(named after the file) via the loop in `Oxylus/tests/CMakeLists.txt`, and all are `EXCLUDE_FROM_ALL`.
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

Adding a test = dropping a new `Test*.cpp` under `Oxylus/tests/`; no CMake edit needed.

## Targets

- **Oxylus** (`Oxylus/`) — static engine library. Public headers in `include/`, implementation in
  `src/` mirroring the same directory names. Platform files are selected by filename: `src/OS/Win32*`,
  `src/OS/Linux*`, `src/OS/MacOS*` are removed for non-matching platforms.
- **ResourceCompiler** (`ResourceCompiler/`) — shared library wrapping slang; compiles `.slang`
  shaders into `.oxpack` archives.
- **rcli** — CLI front end for ResourceCompiler, invoked at build time by `ox_compile_shaders()`
  (`cmake/OxCompileShaders.cmake`). The custom command `DEPENDS rcli`, so it is always fully linked
  — and its slang runtime staged beside it — before any shader pack is compiled.
- **OxylusEditor** (`OxylusEditor/`) — ImGui editor executable; `App` + `DefaultModules` + `Editor`.

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

Systems and observers are all registered imperatively in `Scene::init` in `Oxylus/src/Scene/Scene.cpp`
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

Shaders are Slang (`Oxylus/src/Render/Shaders/`, editor-only ones under `Shaders/editor/`). They are
**not** compiled by name discovery: every shader program must be declared in a TOML manifest —
`OxylusEditor/Resources/engine.toml` and `editor.toml` — listing `name`, `path`, `entry_points`, and
`bindless`. `ox_compile_shaders()` (`cmake/OxCompileShaders.cmake`) feeds each TOML to `rcli`, which
produces `engine.oxpack` / `editor.oxpack` next to the binary. At runtime `Renderer::init` unpacks
`engine.oxpack` and calls `RenderContext::create_pipeline` for each entry. **Adding a shader means
editing the TOML**; every `.slang` file under `Oxylus/src/Render/Shaders/` is a build dependency of
both packs, so editing an imported module rebuilds them too.

`RendererInstance.hpp` defines the frame structure: a fixed `RenderStage` enum (Initialization,
Culling, VisBufferEncode/Decode, Forward2D, Lighting, PostProcessing, Atmosphere, Debug, FinalOutput)
into which callbacks are injected via `StageDependency{target_stage, Before/After, order}`.
`RenderStageContext` passes named `vuk::Value<Buffer>` / `vuk::Value<ImageAttachment>` resources
between stages, with a `SharedResources` tier that persists across stages. Pass implementations live
in `Oxylus/src/Render/Passes/`.

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

### Headers and includes

Oxylus is consumed as a **library**, so there is deliberately no precompiled header to hide include
cost — every include in `Oxylus/include/` is paid by every downstream TU that touches it. Treat
adding one to a public header as a real cost, and fix build times by fixing includes, never by
introducing a PCH.

- **Forward-declare in headers; include in the `.cpp`.** If a header only needs `T*`, `T&`, or a
  return type it never dereferences, declare `struct T;` instead of including it. Include the real
  header only where the definition is actually required.
- **`Fwd.hpp` per module** for things many headers need to name. `Networking/Fwd.hpp` is the model:
  strong ID enums, small POD structs, and opaque C typedefs (`typedef struct _ENetHost ENetHost;`)
  with no third-party include in sight. Add one when a module starts leaking its heavy header to
  name a handle.
- **Never pull a heavy third-party header into `Oxylus/include/`** — Jolt, vuk, ImGui, sol2, flecs,
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

