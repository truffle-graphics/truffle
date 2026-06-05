#pragma once

#include "truffle/rhi/rhi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace truffle::render {

using MaterialId = std::uint64_t;

// ---------------------------------------------------------------------------
// Channel model
// ---------------------------------------------------------------------------

enum class ChannelKind : std::uint32_t {
    Transform,       // world-space 4x4 float matrix per instance (64 bytes)
    LocalTransform,  // local-space 4x4 float matrix (reserved for GPU hierarchy)
    Color,           // RGBA float4 per instance (16 bytes)
    Normal,          // float3 per instance (12 bytes)
    TexCoord,        // float2 per instance (8 bytes)
    ParentIndex,     // int32 per instance — parent node for GPU hierarchy
    CustomFloat4,    // user-declared float4 channel
};

enum class BindingModel : std::uint32_t {
    Separate,     // one buffer binding per channel (SoA — default)
    Interleaved,  // all channels packed into one binding (AoS)
};

enum class DrawKind : std::uint32_t {
    Direct,
    Indexed,
    Indirect,
    IndirectIndexed,
};

// ---------------------------------------------------------------------------
// ChannelDesc — one entry in an InstanceLayout
// ---------------------------------------------------------------------------

struct ChannelDesc {
    ChannelKind   kind    = ChannelKind::Transform;
    std::uint32_t binding = 0;  // index into RenderBatch::bindings[]
    std::uint32_t offset  = 0;  // byte offset within the binding stride
    std::uint32_t stride  = 64; // bytes between consecutive instances

    [[nodiscard]] bool operator==(const ChannelDesc& o) const noexcept {
        return kind == o.kind && binding == o.binding &&
               offset == o.offset && stride == o.stride;
    }
};

// ---------------------------------------------------------------------------
// InstanceLayout — runtime declaration of what data a batch carries
// ---------------------------------------------------------------------------

struct InstanceLayout {
    std::vector<ChannelDesc> channels;
    BindingModel             bindingModel = BindingModel::Separate;

    [[nodiscard]] bool operator==(const InstanceLayout& o) const noexcept {
        return bindingModel == o.bindingModel && channels == o.channels;
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        auto blend = [](std::size_t h, std::size_t v) noexcept -> std::size_t {
            return h ^ (v + 0x9e3779b9u + (h << 6u) + (h >> 2u));
        };
        std::size_t h = channels.size();
        for (const auto& ch : channels) {
            h = blend(h, static_cast<std::size_t>(ch.kind));
            h = blend(h, static_cast<std::size_t>(ch.binding));
            h = blend(h, static_cast<std::size_t>(ch.offset));
            h = blend(h, static_cast<std::size_t>(ch.stride));
        }
        h = blend(h, static_cast<std::size_t>(bindingModel));
        return h;
    }
};

// ---------------------------------------------------------------------------
// BufferView — non-owning reference into a GPU buffer range
// ---------------------------------------------------------------------------

struct BufferView {
    rhi::IBuffer* buffer = nullptr;
    std::size_t   offset = 0;
    std::size_t   size   = 0;
};

// ---------------------------------------------------------------------------
// RenderBatch — universal renderer input; produced by all three data lanes
// ---------------------------------------------------------------------------

struct RenderBatch {
    static constexpr std::uint32_t kMaxBindings = 8;

    InstanceLayout layout;
    std::uint32_t  instanceCount = 0;
    std::uint32_t  vertexCount   = 3;
    BufferView     bindings[kMaxBindings]{};
    DrawKind       kind        = DrawKind::Direct;
    MaterialId     material    = 0;
    std::size_t    variantHash = 0; // shader macro variant mutation key
    rhi::IndexFormat indexFormat = rhi::IndexFormat::uint32; // for DrawKind::Indexed

    // Optional: index buffer for DrawKind::Indexed
    BufferView indexBuffer;
    // Optional: indirect draw args buffer for DrawKind::Indirect*
    BufferView indirectBuffer;
    // Optional: per-batch uniform/constant data (VS + FS params)
    BufferView uniformBuffer;
};

} // namespace truffle::render
