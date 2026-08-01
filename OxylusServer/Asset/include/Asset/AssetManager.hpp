#pragma once

#include <expected>
#include <simdjson.h>

#include "Asset/AssetFile.hpp"
#include "Asset/AudioSource.hpp"
#include "Asset/Fwd.hpp"
#include "Asset/Material.hpp"
#include "Asset/Model.hpp"
#include "Asset/TextureLoadInfo.hpp"
#include "Core/UUID.hpp"
#include "Memory/ReadGuard.hpp"
#include "Memory/SlotMap.hpp"
#include "Utils/JsonWriter.hpp"

// Scene and LuaSystem are held only behind unique_ptr and ReadGuard, so a forward declaration is
// enough. Including Scene/Scene.hpp here made Scene -> Asset -> Renderer a header cycle and dragged
// the whole vuk surface into every consumer of the asset manager.

namespace ox {
struct Asset {
  UUID uuid = {};
  std::filesystem::path path = {};
  AssetType type = AssetType::None;
  union {
    ModelID model_id = ModelID::Invalid;
    TextureID texture_id;
    MaterialID material_id;
    SceneID scene_id;
    AudioID audio_id;
    ScriptID script_id;
  };

  // Reference count of loads
  u64 ref_count = 0;

  auto is_loaded() const -> bool { return model_id != ModelID::Invalid; }

  auto acquire_ref() -> void { ++std::atomic_ref(ref_count); }

  auto release_ref() -> bool { return --std::atomic_ref(ref_count) == 0; }
};

using AssetRegistry = ankerl::unordered_dense::map<UUID, Asset>;

class AssetManager {
public:
  constexpr static auto MODULE_NAME = "AssetManager";

  // Simulation-side: App::with<T>() and App::mod<T>() route this to Server.
  constexpr static bool SERVER_MODULE = true;

  // Declared here and defaulted in the .cpp: the scene and script slot maps hold unique_ptrs to
  // types this header only forward-declares, so the destructor must be instantiated where they are
  // complete.
  AssetManager();
  ~AssetManager();

  AssetManager(const AssetManager&) = delete;
  AssetManager& operator=(const AssetManager&) = delete;

  using LoadInfo = std::variant<TextureLoadInfo, Material>;

  // Materialising a model or a texture creates GPU resources, which only the presentation half can
  // do - but the registry that decides *when* to load is simulation-side. Rather than invert the
  // dependency, the client hands its loaders over at startup and the simulation calls through them.
  //
  // These are plain function pointers rather than member pointers because every target below is
  // written with an explicit object parameter, so its type is already a free-function type.
  //
  // A headless process installs nothing. Models and textures then simply do not load, which is the
  // honest behaviour for a process with no GPU - not an error to work around.
  struct Loaders {
    ModelID (*load_model)(AssetManager&, const std::filesystem::path&) = nullptr;
    TextureID (*load_texture)(AssetManager&, const std::filesystem::path&, TextureLoadInfo) = nullptr;
    glm::uvec2 (*texture_extent)(AssetManager&, const UUID&) = nullptr;
    bool (*write_gltf_meta)(AssetManager&, const std::filesystem::path&, JsonWriter&) = nullptr;
  };

  static auto install_loaders(const Loaders& loaders) -> void;

  static auto to_asset_file_type(const std::filesystem::path& path) -> AssetFileType;
  static auto to_asset_type_sv(AssetType type) -> std::string_view;
  static auto write_gltf_meta(AssetManager& self, const std::filesystem::path& path, JsonWriter& json) -> bool;

  struct AssetMetaFile {
    simdjson::padded_string contents;
    simdjson::ondemand::parser parser;
    simdjson::simdjson_result<simdjson::ondemand::document> doc;
  };

  auto init(this AssetManager& self) -> std::expected<void, std::string>;
  auto deinit(this AssetManager& self) -> std::expected<void, std::string>;

  auto get_registry_snapshot(this AssetManager& self) -> std::vector<Asset>;

  auto read_meta_file(this AssetManager& self, const std::filesystem::path& path) -> std::unique_ptr<AssetMetaFile>;
  auto read_meta_file_from_asset(this AssetManager& self, const std::filesystem::path& path)
    -> std::unique_ptr<AssetMetaFile>;

  auto create_asset(this AssetManager& self, AssetType type, const std::filesystem::path& path = {}) -> UUID;
  auto import_asset(this AssetManager& self, const std::filesystem::path& path) -> UUID;
  auto delete_asset(this AssetManager& self, const UUID& uuid) -> void;
  auto register_asset(this AssetManager& self, const std::filesystem::path& path) -> UUID;
  auto register_asset(this AssetManager& self, const UUID& uuid, AssetType type, const std::filesystem::path& path)
    -> bool;
  auto acquire_ref(this AssetManager& self, ReadGuard<Asset> asset) -> void;
  auto release_ref(this AssetManager& self, ReadGuard<Asset> asset) -> void;

  auto export_asset(this AssetManager& self, const UUID& uuid, const std::filesystem::path& path) -> bool;
  auto export_scene(this AssetManager& self, const UUID& uuid, JsonWriter& writer, const std::filesystem::path& path)
    -> bool;
  auto export_material(this AssetManager& self, const UUID& uuid, JsonWriter& writer, const std::filesystem::path& path)
    -> bool;
  auto export_script(this AssetManager& self, const UUID& uuid, JsonWriter& writer, const std::filesystem::path& path)
    -> bool;

  auto load_asset(this AssetManager& self, const UUID& uuid, LoadInfo explicit_load = {}, bool should_acquire = true)
    -> bool;
  auto unload_asset(this AssetManager& self, const UUID& uuid) -> void;

  auto is_loaded(this AssetManager& self, const UUID& uuid) -> bool;

  auto get_asset(this AssetManager& self, const UUID& uuid) -> ReadGuard<Asset>;

  auto get_model(this AssetManager& self, const UUID& uuid) -> ReadGuard<Model>;
  auto get_model(this AssetManager& self, ModelID model_id) -> ReadGuard<Model>;

  auto get_texture(this AssetManager& self, const UUID& uuid) -> ReadGuard<Texture>;
  auto get_texture(this AssetManager& self, TextureID texture_id) -> ReadGuard<Texture>;
  // Pixel dimensions as plain numbers. A texture's size is asset metadata, so simulation code can
  // ask for it without holding a Texture - which owns vuk images and cannot cross the boundary.
  auto get_texture_extent(this AssetManager& self, const UUID& uuid) -> glm::uvec2;

  auto get_null_material(this AssetManager& self) -> ReadGuard<Asset>;
  auto get_material(this AssetManager& self, const UUID& uuid) -> ReadGuard<Material>;
  auto get_material(this AssetManager& self, MaterialID material_id) -> ReadGuard<Material>;
  auto set_material_dirty(this AssetManager& self, MaterialID material_id) -> void;
  auto set_material_dirty(this AssetManager& self, const UUID& uuid) -> void;
  auto set_all_materials_dirty(this AssetManager& self) -> void;
  auto get_dirty_material_ids(this AssetManager& self) -> std::vector<MaterialID>;

  auto get_scene(this AssetManager& self, const UUID& uuid) -> ReadGuard<Scene>;
  auto get_scene(this AssetManager& self, SceneID scene_id) -> ReadGuard<Scene>;

  auto get_audio(this AssetManager& self, const UUID& uuid) -> ReadGuard<AudioSource>;
  auto get_audio(this AssetManager& self, AudioID audio_id) -> ReadGuard<AudioSource>;

  auto get_script(this AssetManager& self, const UUID& uuid) -> ReadGuard<LuaSystem>;
  auto get_script(this AssetManager& self, ScriptID script_id) -> ReadGuard<LuaSystem>;

  // The client-side half of the loader contract: these four are defined in presentation TUs and
  // reach the simulation only through install_loaders(). Public because the TU that installs them
  // has to be able to name them, not because callers should reach for them directly - go through
  // load_asset().
  auto load_model(this AssetManager& self, const std::filesystem::path& path) -> ModelID;
  auto unload_model(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  auto load_texture(this AssetManager& self, const std::filesystem::path& path, TextureLoadInfo info = {}) -> TextureID;
  auto unload_texture(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  // A SlotMap<Texture, TextureID>, type-erased. Texture owns vuk images, so naming the type here
  // would drag vuk into every consumer of the asset manager - and into the simulation half, which
  // must not link it at all. The client TU that creates the storage captures the deleter in the
  // control block, so the simulation owns the lifetime without linking anything from the renderer.
  // Same trick as Model::gpu_resources.
  //
  // Public, along with the mutex guarding it, because the only code that can meaningfully touch it
  // lives in a different target. Everyone else goes through get_texture().
  std::shared_ptr<void> texture_storage = {};
  std::shared_mutex textures_mutex = {};

private:
  auto load_material(this AssetManager& self, const std::filesystem::path& path, const Material& info = {})
    -> MaterialID;
  auto unload_material(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  auto load_scene(this AssetManager& self, const std::filesystem::path& path) -> SceneID;
  auto unload_scene(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  auto load_audio(this AssetManager& self, const std::filesystem::path& path) -> AudioID;
  auto unload_audio(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  auto load_script(this AssetManager& self, const std::filesystem::path& path) -> ScriptID;
  auto unload_script(this AssetManager& self, ReadGuard<Asset> asset) -> bool;

  AssetRegistry asset_registry = {};

  std::shared_mutex registry_mutex = {};
  std::shared_mutex models_mutex = {};
  std::shared_mutex materials_mutex = {};
  std::shared_mutex scenes_mutex = {};
  std::shared_mutex audio_mutex = {};
  std::shared_mutex scripts_mutex = {};

  std::vector<MaterialID> dirty_materials = {};

  SlotMap<Model, ModelID> model_map = {};
  SlotMap<Material, MaterialID> material_map = {};
  // No `= {}` on these two: a default member initializer would instantiate the slot map's default
  // constructor here, where Scene and LuaSystem are still incomplete. The defaulted AssetManager
  // constructor in the .cpp does it instead.
  SlotMap<std::unique_ptr<Scene>, SceneID> scene_map;
  SlotMap<AudioSource, AudioID> audio_map = {};
  SlotMap<std::unique_ptr<LuaSystem>, ScriptID> script_map;

  UUID null_material = {};

  static Loaders loaders_;
};

// Installs the presentation half's asset loaders into AssetManager. Declared here and defined in a
// client TU, then called from App::init - the same shape as bind_client_lua_bindings().
auto install_client_asset_loaders() -> void;
} // namespace ox
