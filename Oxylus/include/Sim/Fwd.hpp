#pragma once

#include "Core/Types.hpp"

namespace ox {
// Identifies one frame of extracted simulation state. The client uses it to notice gaps: if it
// misses a frame it must re-upload everything rather than trust that frame's dirty lists.
enum class FrameID : u64 { Invalid = ~0_u64 };

// Identifies a view the client wants rendered. One per viewport, not one per camera.
enum class SimViewID : u8 { Invalid = 0xFF };

// An opaque flecs::entity_t. Crossing the boundary as a plain handle keeps flecs out of the
// client's vocabulary and out of any wire format. Zero is flecs-invalid.
enum class EntityHandle : u64 { Invalid = 0 };

// An index into the frame's transform array. Deliberately not GPU::TransformID: that packs a
// version and a slot index, and only the decoded index means anything to a renderer.
enum class RenderTransformIndex : u32 { Invalid = ~0_u32 };

// Where a view's camera comes from. Edit-mode viewports drive their own camera and send it down;
// play-mode viewports name a camera entity and let the simulation resolve it.
enum class SimCameraSource : u8 { ClientSupplied, SimEntity, Invalid = 0xFF };

struct ViewRequest;
struct ViewSnapshot;
struct FrameSnapshot;
class SceneExtractor;
} // namespace ox
