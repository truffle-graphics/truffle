#pragma once

#include "truffle/renderer/render_batch.hpp"

#include <span>

namespace truffle::renderer {

// Explicit locations are the portable link; reflected names are diagnostic.
// This validates vertex input, not members inside uniform/storage buffers.
[[nodiscard]] base::Status validate_vertex_layout(
    std::span<const rhi::VertexBufferLayoutDesc> buffers,
    std::span<const rhi::ShaderInterfaceVariable> interfaces);

// Checks declared byte views, required vertex/instance streams and draw ranges.
// Indexed GPU-resident content remains caller-owned; Mesh::upload checks CPU indices.
[[nodiscard]] base::Status validate_batch_data(
    const RenderBatch& batch,
    std::span<const rhi::VertexBufferLayoutDesc> buffers);

} // namespace truffle::renderer
