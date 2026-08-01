#pragma once

#include <filesystem>

#include "Asset/Fwd.hpp"
#include "Core/Types.hpp"

struct ma_sound;

namespace ox {
class AudioSource {
public:
  AudioSource() = default;
  ~AudioSource();

  auto load(const std::filesystem::path& path) -> bool;
  auto unload() -> void;
  auto get_source() -> ma_sound*;

private:
  ma_sound* _sound = nullptr;
};
} // namespace ox
