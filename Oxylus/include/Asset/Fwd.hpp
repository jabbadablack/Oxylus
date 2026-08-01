#pragma once

#include "Core/Types.hpp"

namespace ox {
enum class AssetType : u32 {
  None = 0,
  Shader,
  Model,
  Texture,
  Material,
  Font,
  Scene,
  Audio,
  Script,
};

enum class ModelID : u64 { Invalid = ~0_u64 };
enum class TextureID : u64 { Invalid = ~0_u64 };
enum class MaterialID : u64 { Invalid = ~0_u64 };
enum class SceneID : u64 { Invalid = ~0_u64 };
enum class AudioID : u64 { Invalid = ~0_u64 };
enum class ScriptID : u64 { Invalid = ~0_u64 };
enum class MeshInstanceID : u64 { Invalid = ~0_u64 };

struct Asset;
struct Model;
struct Material;
class AudioSource;
class Texture;
class Scene;
class LuaSystem;
} // namespace ox
