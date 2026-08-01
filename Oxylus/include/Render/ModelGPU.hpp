#pragma once

#include <vector>
#include <vuk/Buffer.hpp>

namespace ox {
// The device-side residency of a model: the buffers whose addresses are baked into its GPU::Mesh
// entries. Nothing ever reads this back - it exists purely so the allocations outlive the model.
//
// Model holds it as an opaque std::shared_ptr<void>. That keeps the lifetime with the model, where
// it belongs, while leaving the type on this side of the boundary: the deleter is captured in the
// control block by whichever client TU created it, so the simulation can destroy one without
// knowing what it is or linking anything from the renderer.
struct ModelGPU {
  std::vector<vuk::Unique<vuk::Buffer>> mesh_buffers = {};
};
} // namespace ox
