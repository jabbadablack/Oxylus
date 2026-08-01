#pragma once

#include "Core/Types.hpp"

namespace ox {
// Identifies a view the client wants rendered. One per viewport, not one per camera.
enum class SimViewID : u8 { Invalid = 0xFF };

// An opaque flecs::entity_t. Crossing the boundary as a plain handle keeps flecs out of the
// client's vocabulary and out of any wire format. Zero is flecs-invalid.
enum class EntityHandle : u64 { Invalid = 0 };

// Where a view's camera comes from. Edit-mode viewports drive their own camera and send it down;
// play-mode viewports name a camera entity and let the simulation resolve it.
enum class SimCameraSource : u8 { ClientSupplied, SimEntity, Invalid = 0xFF };
} // namespace ox
