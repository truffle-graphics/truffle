#include "truffle/renderer/mesh.hpp"

#include <cstring>
#include <limits>

namespace truffle::renderer {
namespace {

base::Status invalid(const char* message) {
    return base::Status::failure(base::StatusCode::invalid_argument, message);
}

base::Result<rhi::Buffer> upload_buffer(rhi::Device& device,
                                       std::span<const std::byte> bytes,
                                       rhi::BufferUsage usage) {
    auto result = device.create_buffer({
        .size = bytes.size(), .usage = usage, .memory = rhi::MemoryDomain::upload,
        .debugName = "Renderer mesh bytes",
    });
    if (!result.ok()) {
        return result.status();
    }
    auto buffer = std::move(result).value();
    if (auto status = buffer.write(0, bytes); !status.ok()) {
        return status;
    }
    return buffer;
}

} // namespace

base::Result<Mesh> Mesh::upload(rhi::Device& device, const MeshUpload& desc) {
    if (desc.vertexCount == 0 || desc.streams.empty() ||
        desc.streams.size() > RenderBatch::kMaxBindings ||
        (desc.indexFormat != rhi::IndexFormat::uint16 &&
         desc.indexFormat != rhi::IndexFormat::uint32)) {
        return invalid("Mesh: nonempty vertices/streams and a valid index format required");
    }
    for (const auto& stream : desc.streams) {
        if (stream.stride == 0 || stream.bytes.size() % stream.stride != 0 ||
            stream.bytes.size() / stream.stride != desc.vertexCount) {
            return invalid("Mesh: stream size does not match vertex count and stride");
        }
    }
    const std::size_t indexSize = desc.indexFormat == rhi::IndexFormat::uint16 ? 2 : 4;
    if (desc.indices.size() % indexSize != 0 ||
        desc.indices.size() / indexSize > std::numeric_limits<std::uint32_t>::max()) {
        return invalid("Mesh: invalid index byte count");
    }
    for (std::size_t offset = 0; offset < desc.indices.size(); offset += indexSize) {
        std::uint32_t index = 0;
        if (indexSize == 2) {
            std::uint16_t value = 0;
            std::memcpy(&value, desc.indices.data() + offset, sizeof(value));
            index = value;
        } else {
            std::memcpy(&index, desc.indices.data() + offset, sizeof(index));
        }
        if (index >= desc.vertexCount) {
            return invalid("Mesh: index exceeds uploaded vertex count");
        }
    }
    Mesh mesh;
    mesh.vertexCount_ = desc.vertexCount;
    mesh.indexCount_ = static_cast<std::uint32_t>(desc.indices.size() / indexSize);
    mesh.indexFormat_ = desc.indexFormat;
    mesh.vertices_.reserve(desc.streams.size());
    for (const auto& stream : desc.streams) {
        auto buffer = upload_buffer(device, stream.bytes, rhi::BufferUsage::vertex);
        if (!buffer.ok()) {
            return buffer.status();
        }
        mesh.vertices_.push_back(std::move(buffer).value());
    }
    if (!desc.indices.empty()) {
        auto buffer = upload_buffer(device, desc.indices, rhi::BufferUsage::index);
        if (!buffer.ok()) {
            return buffer.status();
        }
        mesh.indices_ = std::move(buffer).value();
    }
    return mesh;
}

RenderBatch Mesh::batch(MaterialId program, std::size_t variant) {
    RenderBatch result;
    result.material = program;
    result.variantHash = variant;
    result.instanceCount = 1;
    result.vertexCount = indexCount_ != 0 ? indexCount_ : vertexCount_;
    result.vertexElementCount = vertexCount_;
    result.kind = indexCount_ != 0 ? DrawKind::Indexed : DrawKind::Direct;
    result.indexFormat = indexFormat_;
    for (std::size_t index = 0; index < vertices_.size(); ++index) {
        result.bindings[index] = {&vertices_[index], 0, vertices_[index].desc().size};
    }
    if (indices_.valid()) {
        result.indexBuffer = {&indices_, 0, indices_.desc().size};
    }
    return result;
}

} // namespace truffle::renderer
