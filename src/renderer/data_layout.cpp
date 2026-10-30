#include "truffle/renderer/data_layout.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace truffle::renderer {
namespace {

base::Status invalid(const char* message) {
    return base::Status::failure(base::StatusCode::invalid_argument, message);
}

std::uint32_t format_size(rhi::VertexFormat format) {
    switch (format) {
    case rhi::VertexFormat::float32: case rhi::VertexFormat::uint32: return 4;
    case rhi::VertexFormat::float32x2: case rhi::VertexFormat::uint32x2: return 8;
    case rhi::VertexFormat::float32x3: case rhi::VertexFormat::uint32x3: return 12;
    case rhi::VertexFormat::float32x4: case rhi::VertexFormat::uint32x4: return 16;
    }
    return 0;
}

bool matches(rhi::VertexFormat format, std::string_view type) {
    switch (format) {
    case rhi::VertexFormat::float32: return type == "float";
    case rhi::VertexFormat::float32x2: return type == "vec2" || type == "float2";
    case rhi::VertexFormat::float32x3: return type == "vec3" || type == "float3";
    case rhi::VertexFormat::float32x4: return type == "vec4" || type == "float4";
    case rhi::VertexFormat::uint32: return type == "uint";
    case rhi::VertexFormat::uint32x2: return type == "uvec2" || type == "uint2";
    case rhi::VertexFormat::uint32x3: return type == "uvec3" || type == "uint3";
    case rhi::VertexFormat::uint32x4: return type == "uvec4" || type == "uint4";
    }
    return false;
}

bool valid_view(const BufferView& view) {
    if (view.buffer == nullptr) {
        return view.offset == 0 && view.size == 0;
    }
    if (!view.buffer->valid()) {
        return false;
    }
    const auto size = view.buffer->desc().size;
    return view.size != 0 && view.offset <= size && view.size <= size - view.offset;
}

} // namespace

base::Status validate_vertex_layout(
    std::span<const rhi::VertexBufferLayoutDesc> buffers,
    std::span<const rhi::ShaderInterfaceVariable> interfaces) {
    if (buffers.size() > RenderBatch::kMaxBindings) {
        return invalid("Renderer: too many vertex streams");
    }
    std::vector<std::uint32_t> locations;
    for (const auto& buffer : buffers) {
        if (buffer.stride == 0 || buffer.attributes.empty() ||
            (buffer.stepMode != rhi::VertexStepMode::vertex &&
             buffer.stepMode != rhi::VertexStepMode::instance)) {
            return invalid("Renderer: invalid vertex stride, attributes or step mode");
        }
        for (const auto& attribute : buffer.attributes) {
            const auto size = format_size(attribute.format);
            if (size == 0 || attribute.offset % 4 != 0 || buffer.stride % 4 != 0 ||
                attribute.offset > buffer.stride || size > buffer.stride - attribute.offset ||
                std::find(locations.begin(), locations.end(), attribute.location) != locations.end()) {
                return invalid("Renderer: duplicate, misaligned or out-of-stride vertex attribute");
            }
            locations.push_back(attribute.location);
        }
    }
    for (const auto& input : interfaces) {
        if (input.stage != rhi::ShaderStage::vertex ||
            input.direction != rhi::ShaderInterfaceDirection::input) {
            continue;
        }
        bool found = false;
        for (const auto& buffer : buffers) {
            for (const auto& attribute : buffer.attributes) {
                found |= attribute.location == input.location && matches(attribute.format, input.type);
            }
        }
        if (!found) {
            return invalid("Renderer: reflected vertex input has no exact location/type match");
        }
    }
    return base::Status::success();
}

base::Status validate_batch_data(
    const RenderBatch& batch, std::span<const rhi::VertexBufferLayoutDesc> buffers) {
    if (auto status = validate_vertex_layout(buffers, {}); !status.ok()) {
        return status;
    }
    if (batch.instanceCount == 0 || batch.vertexCount == 0) {
        return invalid("Renderer: draw counts must be nonzero");
    }
    for (const auto& view : batch.bindings) {
        if (!valid_view(view)) {
            return invalid("Renderer: vertex buffer view is out of range");
        }
    }
    if (!valid_view(batch.indexBuffer) || !valid_view(batch.indirectBuffer) ||
        !valid_view(batch.uniformBuffer)) {
        return invalid("Renderer: index, indirect or uniform view is out of range");
    }
    const bool indexed = batch.kind == DrawKind::Indexed || batch.kind == DrawKind::IndirectIndexed;
    const bool indirect = batch.kind == DrawKind::Indirect || batch.kind == DrawKind::IndirectIndexed;
    if (batch.kind != DrawKind::Direct && batch.kind != DrawKind::Indexed && !indirect) {
        return invalid("Renderer: unknown draw kind");
    }
    if (indexed) {
        const auto size = batch.indexFormat == rhi::IndexFormat::uint16 ? 2u : 4u;
        if ((batch.indexFormat != rhi::IndexFormat::uint16 &&
             batch.indexFormat != rhi::IndexFormat::uint32) ||
            batch.indexBuffer.buffer == nullptr || batch.indexBuffer.offset % size != 0 ||
            (!indirect && batch.vertexCount > batch.indexBuffer.size / size)) {
            return invalid("Renderer: indexed draw requires a sufficient aligned index view");
        }
    }
    if (indirect && (batch.indirectBuffer.buffer == nullptr ||
                     batch.indirectBuffer.offset % 4 != 0 ||
                     batch.indirectBuffer.size < (indexed ? 20u : 16u))) {
        return invalid("Renderer: indirect draw requires an aligned argument view");
    }
    for (std::size_t slot = 0; slot < buffers.size(); ++slot) {
        const auto& layout = buffers[slot];
        const auto& view = batch.bindings[slot];
        const std::size_t count = layout.stepMode == rhi::VertexStepMode::instance
            ? batch.instanceCount : indexed ? batch.vertexElementCount : batch.vertexCount;
        // Indirect counts and arbitrary index contents cannot be inspected on CPU.
        // Still require a full element; producers own the GPU-side range contract.
        const auto required = indirect || count == 0 ? std::size_t{1} : count;
        if (view.buffer == nullptr || view.offset % 4 != 0 ||
            required > view.size / layout.stride) {
            return invalid("Renderer: missing or undersized required vertex/instance stream");
        }
    }
    return base::Status::success();
}

} // namespace truffle::renderer
