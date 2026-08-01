#pragma once

#include "Asset/Fwd.hpp"
#include "Core/UUID.hpp"
#include "Scene/SceneGPU.hpp"

namespace ox {
// One placed instance of a mesh in a scene. Deliberately logical - UUIDs and a transform slot, no
// device addresses - so it lives with the scene rather than with the model asset, and can cross
// the simulation/presentation boundary unchanged.
struct MeshInstance {
  UUID model_uuid = UUID(nullptr);
  usize mesh_node_index = 0;
  UUID material_uuid = UUID(nullptr);
  GPU::TransformID transform_id = GPU::TransformID::Invalid;
};
} // namespace ox
