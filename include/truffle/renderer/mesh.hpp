#pragma once

#include "truffle/renderer/render_batch.hpp"

#include <span>
#include <vector>

namespace truffle::renderer {

struct MeshStream {
    std::span<const std::byte> bytes;
    std::uint32_t stride = 0;
};

struct MeshUpload {
    std::span<const MeshStream> streams;
    std::uint32_t vertexCount = 0;
    std::span<const std::byte> indices;
    rhi::IndexFormat indexFormat = rhi::IndexFormat::uint32;
};

// Immutable uploaded bytes, with no position/normal/transform semantics.
// Batches borrow this object's wrappers: create them after the final move and
// keep the Mesh alive and unmoved through render(). Recorded RHI payloads persist.
class Mesh {
public:
    [[nodiscard]] static base::Result<Mesh> upload(rhi::Device& device,
                                                  const MeshUpload& desc);
    [[nodiscard]] RenderBatch batch(MaterialId program,
                                     std::size_t variant = 0);

private:
    std::vector<rhi::Buffer> vertices_;
    rhi::Buffer indices_;
    std::uint32_t vertexCount_ = 0;
    std::uint32_t indexCount_ = 0;
    rhi::IndexFormat indexFormat_ = rhi::IndexFormat::uint32;
};

} // namespace truffle::renderer
