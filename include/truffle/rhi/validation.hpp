#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace truffle::rhi::validation {

[[nodiscard]] constexpr bool is_non_zero(Extent2D extent) noexcept {
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] constexpr bool extent_within(Extent2D extent,
                                           std::uint32_t maxDimension) noexcept {
    return is_non_zero(extent) && maxDimension != 0 &&
           extent.width <= maxDimension && extent.height <= maxDimension;
}

[[nodiscard]] constexpr bool frame_count_supported(
    std::uint32_t framesInFlight,
    const Capabilities& capabilities) noexcept {
    return framesInFlight != 0 &&
           framesInFlight <= capabilities.maxFramesInFlight;
}

[[nodiscard]] inline bool present_mode_supported(
    PresentMode presentMode,
    const Capabilities& capabilities) noexcept {
    return supports_present_mode(capabilities, presentMode);
}

[[nodiscard]] constexpr bool native_surface_handles_valid(
    const NativeSurface& native) noexcept {
    switch (native.kind) {
    case NativeSurfaceKind::headless:
        return native.handle == nullptr && native.display == nullptr;
    case NativeSurfaceKind::cocoa_layer:
        return native.handle != nullptr && native.display == nullptr;
    case NativeSurfaceKind::win32:
        return native.handle != nullptr;
    case NativeSurfaceKind::wayland:
    case NativeSurfaceKind::xcb:
        return native.handle != nullptr && native.display != nullptr;
    case NativeSurfaceKind::external:
        return native.handle != nullptr;
    }
    return false;
}

[[nodiscard]] inline bool native_surface_kind_supported(
    NativeSurfaceKind kind,
    const Capabilities& capabilities) noexcept {
    return supports_native_surface_kind(capabilities, kind);
}

[[nodiscard]] inline bool surface_supported(
    const SurfaceDesc& desc,
    const Capabilities& capabilities) noexcept {
    return extent_within(desc.initialExtent,
                         capabilities.limits.maxTextureDimension2D) &&
           native_surface_handles_valid(desc.native) &&
           native_surface_kind_supported(desc.native.kind, capabilities);
}

[[nodiscard]] inline bool shader_byte_format_supported(
    ShaderByteFormat format,
    const Capabilities& capabilities) noexcept {
    return supports_shader_byte_format(capabilities, format);
}

[[nodiscard]] inline bool shader_payload_valid(
    const ShaderDesc& desc) noexcept {
    if (desc.entryPoint.empty() || desc.bytecode.empty()) {
        return false;
    }

    if (desc.byteFormat == ShaderByteFormat::spirv_binary) {
        if (desc.bytecode.size() < 4 || (desc.bytecode.size() % 4u) != 0) {
            return false;
        }
        const auto b0 = static_cast<std::uint8_t>(desc.bytecode[0]);
        const auto b1 = static_cast<std::uint8_t>(desc.bytecode[1]);
        const auto b2 = static_cast<std::uint8_t>(desc.bytecode[2]);
        const auto b3 = static_cast<std::uint8_t>(desc.bytecode[3]);
        return b0 == 0x03u && b1 == 0x02u && b2 == 0x23u && b3 == 0x07u;
    }

    if (desc.byteFormat == ShaderByteFormat::dxil_binary) {
        if (desc.bytecode.size() < 4) {
            return false;
        }
        const auto c0 = static_cast<char>(desc.bytecode[0]);
        const auto c1 = static_cast<char>(desc.bytecode[1]);
        const auto c2 = static_cast<char>(desc.bytecode[2]);
        const auto c3 = static_cast<char>(desc.bytecode[3]);
        return (c0 == 'D' && c1 == 'X' && c2 == 'I' && c3 == 'L') ||
               (c0 == 'D' && c1 == 'X' && c2 == 'B' && c3 == 'C');
    }

    return true;
}

[[nodiscard]] inline bool shader_desc_supported(
    const ShaderDesc& desc,
    const Capabilities& capabilities) noexcept {
    return shader_byte_format_supported(desc.byteFormat, capabilities) &&
           shader_payload_valid(desc);
}

[[nodiscard]] constexpr bool shader_stage_visibility_valid(
    ShaderStageFlags visibility) noexcept {
    const auto raw = static_cast<std::uint32_t>(visibility);
    const auto allowed = static_cast<std::uint32_t>(ShaderStageFlags::all);
    return raw != 0 && (raw & ~allowed) == 0;
}

[[nodiscard]] constexpr bool pipeline_layout_binding_valid(
    const BindingLayoutDesc& binding,
    const Capabilities& capabilities) noexcept {
    const auto nativeSlot = binding.nativeSlot.value_or(binding.bindingIndex);
    if (binding.groupIndex >= capabilities.limits.maxBindGroups ||
        binding.bindingIndex >= capabilities.limits.maxResourceBindings ||
        nativeSlot >= capabilities.limits.maxResourceBindings ||
        binding.arrayCount == 0 ||
        binding.arrayCount > capabilities.limits.maxDescriptorArrayElements ||
        binding.arrayCount >
            capabilities.limits.maxResourceBindings - nativeSlot ||
        !shader_stage_visibility_valid(binding.visibility)) {
        return false;
    }

    if (binding.arrayCount > 1 && !supports_descriptor_arrays(capabilities)) {
        return false;
    }

    if (binding.dynamicIndexing &&
        (binding.arrayCount <= 1 ||
         !supports_dynamic_resource_indexing(capabilities))) {
        return false;
    }

    if (binding.bindless &&
        (!binding.dynamicIndexing ||
         !supports_bindless_resources(capabilities) ||
         binding.arrayCount > capabilities.limits.maxBindlessResources)) {
        return false;
    }

    if ((binding.type == BindingResourceType::uniform_buffer ||
          binding.type == BindingResourceType::storage_buffer) &&
        binding.minBindingSize > capabilities.limits.maxBufferSize) {
        return false;
    }

    if (binding.dynamicOffset &&
        (binding.type != BindingResourceType::uniform_buffer &&
         binding.type != BindingResourceType::storage_buffer)) {
        return false;
    }

    return true;
}

[[nodiscard]] constexpr std::uint32_t effective_native_binding_slot(
    const BindingLayoutDesc& binding) noexcept {
    return binding.nativeSlot.value_or(binding.bindingIndex);
}

[[nodiscard]] constexpr bool shader_stage_visibility_intersects(
    ShaderStageFlags lhs,
    ShaderStageFlags rhs) noexcept {
    return (lhs & rhs) != ShaderStageFlags::none;
}

[[nodiscard]] constexpr bool binding_native_slot_namespace_matches(
    BindingResourceType lhs,
    BindingResourceType rhs) noexcept {
    const auto lhsBuffer = lhs == BindingResourceType::uniform_buffer ||
                           lhs == BindingResourceType::storage_buffer;
    const auto rhsBuffer = rhs == BindingResourceType::uniform_buffer ||
                           rhs == BindingResourceType::storage_buffer;
    if (lhsBuffer || rhsBuffer) {
        return lhsBuffer && rhsBuffer;
    }

    const auto lhsTexture = lhs == BindingResourceType::sampled_texture ||
                            lhs == BindingResourceType::storage_texture;
    const auto rhsTexture = rhs == BindingResourceType::sampled_texture ||
                            rhs == BindingResourceType::storage_texture;
    if (lhsTexture || rhsTexture) {
        return lhsTexture && rhsTexture;
    }

    return lhs == BindingResourceType::sampler && rhs == BindingResourceType::sampler;
}

[[nodiscard]] constexpr bool binding_native_slot_ranges_overlap(
    const BindingLayoutDesc& lhs,
    const BindingLayoutDesc& rhs) noexcept {
    const auto lhsSlot = effective_native_binding_slot(lhs);
    const auto rhsSlot = effective_native_binding_slot(rhs);
    const auto lhsEnd = lhsSlot + lhs.arrayCount;
    const auto rhsEnd = rhsSlot + rhs.arrayCount;
    return lhsSlot < rhsEnd && rhsSlot < lhsEnd;
}

[[nodiscard]] constexpr bool binding_native_slots_overlap(
    const BindingLayoutDesc& lhs,
    const BindingLayoutDesc& rhs) noexcept {
    return binding_native_slot_namespace_matches(lhs.type, rhs.type) &&
           shader_stage_visibility_intersects(lhs.visibility, rhs.visibility) &&
           binding_native_slot_ranges_overlap(lhs, rhs);
}

[[nodiscard]] inline bool pipeline_layout_valid(
    const PipelineLayoutDesc& layout,
    const Capabilities& capabilities) noexcept {
    for (std::size_t i = 0; i < layout.bindings.size(); ++i) {
        const auto& binding = layout.bindings[i];
        if (!pipeline_layout_binding_valid(binding, capabilities)) {
            return false;
        }
        for (std::size_t j = i + 1; j < layout.bindings.size(); ++j) {
            const auto& other = layout.bindings[j];
            if (binding.groupIndex == other.groupIndex &&
                binding.bindingIndex == other.bindingIndex) {
                return false;
            }
            if (binding_native_slots_overlap(binding, other)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] constexpr bool binding_layout_compatible(
    const BindingLayoutDesc& expected,
    const BindingLayoutDesc& actual) noexcept {
    return expected.bindingIndex == actual.bindingIndex &&
           expected.type == actual.type &&
           expected.visibility == actual.visibility &&
            expected.arrayCount == actual.arrayCount &&
            expected.minBindingSize == actual.minBindingSize &&
            expected.dynamicIndexing == actual.dynamicIndexing &&
            expected.bindless == actual.bindless &&
            expected.dynamicOffset == actual.dynamicOffset &&
            expected.nativeSlot == actual.nativeSlot;
}

[[nodiscard]] inline bool pipeline_layout_bind_group_compatible(
    const PipelineLayoutDesc& pipelineLayout,
    std::uint32_t groupIndex,
    const BindGroupLayoutDesc& bindGroupLayout) noexcept {
    std::size_t expectedCount = 0;
    for (const auto& binding : pipelineLayout.bindings) {
        if (binding.groupIndex == groupIndex) {
            ++expectedCount;
        }
    }
    if (expectedCount == 0 || bindGroupLayout.bindings.size() != expectedCount) {
        return false;
    }

    for (const auto& expected : pipelineLayout.bindings) {
        if (expected.groupIndex != groupIndex) {
            continue;
        }
        bool found = false;
        for (const auto& actual : bindGroupLayout.bindings) {
            if (binding_layout_compatible(expected, actual)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline bool pipeline_layout_required_groups_bound(
    const PipelineLayoutDesc& pipelineLayout,
    const std::vector<std::uint32_t>& boundGroups) noexcept {
    for (const auto& binding : pipelineLayout.bindings) {
        bool found = false;
        for (const auto groupIndex : boundGroups) {
            if (groupIndex == binding.groupIndex) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool fill_mode_valid(FillMode mode) noexcept {
    switch (mode) {
    case FillMode::solid:
    case FillMode::wireframe:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool cull_mode_valid(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none:
    case CullMode::front:
    case CullMode::back:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool front_face_valid(FrontFace face) noexcept {
    switch (face) {
    case FrontFace::counter_clockwise:
    case FrontFace::clockwise:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool depth_compare_op_valid(
    SamplerCompareOp op) noexcept {
    switch (op) {
    case SamplerCompareOp::never:
    case SamplerCompareOp::less:
    case SamplerCompareOp::equal:
    case SamplerCompareOp::less_equal:
    case SamplerCompareOp::greater:
    case SamplerCompareOp::not_equal:
    case SamplerCompareOp::greater_equal:
    case SamplerCompareOp::always:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool blend_factor_valid(BlendFactor factor) noexcept {
    switch (factor) {
    case BlendFactor::zero:
    case BlendFactor::one:
    case BlendFactor::source_color:
    case BlendFactor::one_minus_source_color:
    case BlendFactor::destination_color:
    case BlendFactor::one_minus_destination_color:
    case BlendFactor::source_alpha:
    case BlendFactor::one_minus_source_alpha:
    case BlendFactor::destination_alpha:
    case BlendFactor::one_minus_destination_alpha:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool blend_op_valid(BlendOp op) noexcept {
    switch (op) {
    case BlendOp::add:
    case BlendOp::subtract:
    case BlendOp::reverse_subtract:
    case BlendOp::min:
    case BlendOp::max:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool color_write_mask_valid(
    ColorWriteFlags mask) noexcept {
    const auto raw = static_cast<std::uint32_t>(mask);
    const auto allowed = static_cast<std::uint32_t>(ColorWriteFlags::all);
    return (raw & ~allowed) == 0;
}

[[nodiscard]] constexpr bool raster_state_valid(
    const RasterStateDesc& desc) noexcept {
    return fill_mode_valid(desc.fillMode) &&
           cull_mode_valid(desc.cullMode) &&
           front_face_valid(desc.frontFace);
}

[[nodiscard]] constexpr bool depth_stencil_state_valid(
    const DepthStencilStateDesc& desc) noexcept {
    return depth_compare_op_valid(desc.depthCompare) && !desc.stencilTest;
}

[[nodiscard]] constexpr bool color_blend_valid(
    const ColorBlendDesc& desc) noexcept {
    return blend_factor_valid(desc.srcColor) &&
           blend_factor_valid(desc.dstColor) &&
           blend_op_valid(desc.colorOp) &&
           blend_factor_valid(desc.srcAlpha) &&
           blend_factor_valid(desc.dstAlpha) &&
           blend_op_valid(desc.alphaOp) &&
           color_write_mask_valid(desc.writeMask);
}

[[nodiscard]] constexpr bool vertex_step_mode_valid(
    VertexStepMode mode) noexcept {
    switch (mode) {
    case VertexStepMode::vertex:
    case VertexStepMode::instance:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool vertex_format_valid(
    VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32:
    case VertexFormat::float32x2:
    case VertexFormat::float32x3:
    case VertexFormat::float32x4:
    case VertexFormat::uint32:
    case VertexFormat::uint32x2:
    case VertexFormat::uint32x3:
    case VertexFormat::uint32x4:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::size_t vertex_format_size(
    VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32:
    case VertexFormat::uint32:
        return 4;
    case VertexFormat::float32x2:
    case VertexFormat::uint32x2:
        return 8;
    case VertexFormat::float32x3:
    case VertexFormat::uint32x3:
        return 12;
    case VertexFormat::float32x4:
    case VertexFormat::uint32x4:
        return 16;
    }
    return 0;
}

[[nodiscard]] inline bool vertex_input_valid(
    const PipelineDesc& desc,
    const Capabilities& capabilities) noexcept {
    if (desc.vertexBuffers.size() > capabilities.limits.maxVertexBuffers) {
        return false;
    }
    if (desc.vertexAttributes.size() > capabilities.limits.maxVertexAttributes) {
        return false;
    }

    for (std::size_t i = 0; i < desc.vertexBuffers.size(); ++i) {
        const auto& buffer = desc.vertexBuffers[i];
        if (buffer.binding >= capabilities.limits.maxVertexBuffers ||
            buffer.stride == 0 ||
            buffer.stride > capabilities.limits.maxVertexBufferStride ||
            !vertex_step_mode_valid(buffer.stepMode)) {
            return false;
        }
        for (std::size_t j = i + 1; j < desc.vertexBuffers.size(); ++j) {
            if (buffer.binding == desc.vertexBuffers[j].binding) {
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < desc.vertexAttributes.size(); ++i) {
        const auto& attribute = desc.vertexAttributes[i];
        const auto size = vertex_format_size(attribute.format);
        if (attribute.location >= capabilities.limits.maxVertexAttributes ||
            size == 0 || !vertex_format_valid(attribute.format)) {
            return false;
        }

        const VertexBufferLayoutDesc* buffer = nullptr;
        for (const auto& candidate : desc.vertexBuffers) {
            if (candidate.binding == attribute.binding) {
                buffer = &candidate;
                break;
            }
        }
        if (!buffer ||
            attribute.offset > buffer->stride ||
            size > buffer->stride - attribute.offset) {
            return false;
        }

        for (std::size_t j = i + 1; j < desc.vertexAttributes.size(); ++j) {
            if (attribute.location == desc.vertexAttributes[j].location) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] inline bool pipeline_render_state_valid(
    const PipelineDesc& desc,
    const Capabilities& capabilities) noexcept {
    if (desc.colorFormat != TextureFormat::unknown) {
        const auto* colorSupport =
            find_format_support(capabilities, desc.colorFormat);
        if (!colorSupport || !colorSupport->colorAttachment) {
            return false;
        }
    } else if (desc.colorBlend.enabled) {
        return false;
    }

    if (!raster_state_valid(desc.rasterState) ||
        !depth_stencil_state_valid(desc.depthStencilState) ||
        !color_blend_valid(desc.colorBlend) ||
        !vertex_input_valid(desc, capabilities)) {
        return false;
    }

    if (desc.depthFormat != TextureFormat::unknown) {
        const auto* depthSupport =
            find_format_support(capabilities, desc.depthFormat);
        if (!depthSupport || !depthSupport->depthStencilAttachment) {
            return false;
        }
    } else if (desc.depthTest || desc.depthWrite) {
        return false;
    }

    return true;
}

[[nodiscard]] inline bool pipeline_render_pass_compatible(
    const PipelineDesc& desc,
    std::optional<TextureFormat> colorFormat,
    std::optional<TextureFormat> depthFormat) noexcept {
    if (colorFormat && desc.colorFormat != *colorFormat) {
        return false;
    }
    if (!colorFormat && depthFormat &&
        desc.colorFormat != TextureFormat::unknown) {
        return false;
    }

    if (depthFormat) {
        return desc.depthFormat == *depthFormat;
    }

    return desc.depthFormat == TextureFormat::unknown &&
           !desc.depthTest && !desc.depthWrite;
}

[[nodiscard]] inline bool swapchain_supported(
    const SwapchainDesc& desc,
    const Capabilities& capabilities) noexcept {
    const auto imageCount = effective_swapchain_image_count(desc);
    return extent_within(desc.extent, capabilities.limits.maxTextureDimension2D) &&
           desc.framesInFlight != 0 &&
           imageCount != 0 &&
           imageCount <= capabilities.maxFramesInFlight &&
           present_mode_supported(desc.presentMode, capabilities);
}

[[nodiscard]] inline bool memory_domain_supported(
    MemoryDomain domain,
    const Capabilities& capabilities) noexcept {
    if (domain == MemoryDomain::automatic) {
        return true;
    }

    for (const auto& heap : capabilities.memoryHeaps) {
        if (heap.kind == MemoryHeapKind::unified) {
            return true;
        }
        if (domain == MemoryDomain::device_local &&
            heap.kind == MemoryHeapKind::device_local) {
            return true;
        }
        if ((domain == MemoryDomain::upload || domain == MemoryDomain::readback) &&
            heap.kind == MemoryHeapKind::host_visible) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool buffer_memory_mappable(
    const BufferDesc& desc) noexcept {
    return desc.memory == MemoryDomain::automatic ||
           desc.memory == MemoryDomain::upload ||
           desc.memory == MemoryDomain::readback;
}

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1u)) == 0;
}

[[nodiscard]] constexpr bool texture_shape_valid(
    const TextureDesc& desc,
    std::uint32_t maxDimension) noexcept {
    return extent_within(desc.extent, maxDimension) &&
           desc.depth != 0 &&
           desc.depth <= maxDimension &&
           desc.mipLevels != 0 &&
           desc.arrayLayers != 0 &&
           desc.sampleCount != 0 &&
           is_power_of_two(desc.sampleCount) &&
           ((desc.dimension == TextureDimension::one_d &&
             desc.extent.height == 1 && desc.depth == 1) ||
            (desc.dimension == TextureDimension::two_d && desc.depth == 1) ||
            (desc.dimension == TextureDimension::three_d && desc.arrayLayers == 1) ||
            (desc.dimension == TextureDimension::cube &&
             desc.depth == 1 &&
             desc.extent.width == desc.extent.height &&
             desc.arrayLayers % 6u == 0));
}

[[nodiscard]] constexpr bool buffer_supports_usage(
    const BufferDesc& desc,
    BufferUsageFlags usage) noexcept {
    return usage == BufferUsageFlags::none ||
           has_flag(effective_buffer_usage(desc), usage);
}

[[nodiscard]] constexpr bool texture_supports_usage(
    const TextureDesc& desc,
    TextureUsageFlags usage) noexcept {
    return usage == TextureUsageFlags::none ||
           has_flag(effective_texture_usage(desc), usage);
}

[[nodiscard]] constexpr bool buffer_supports_state(
    const BufferDesc& desc,
    ResourceState state) noexcept {
    const auto usage = effective_buffer_usage(desc);
    switch (state) {
        case ResourceState::undefined: return true;
        case ResourceState::copy_source:
            return has_flag(usage, BufferUsageFlags::transfer_source);
        case ResourceState::copy_destination:
            return has_flag(usage, BufferUsageFlags::transfer_destination);
        case ResourceState::shader_read:
            return (usage & (BufferUsageFlags::uniform | BufferUsageFlags::storage)) !=
                   BufferUsageFlags::none;
        case ResourceState::storage_read_write:
            return has_flag(usage, BufferUsageFlags::storage);
        case ResourceState::color_attachment:
        case ResourceState::depth_attachment:
        case ResourceState::present:
            return false;
    }
    return false;
}

[[nodiscard]] constexpr bool texture_supports_state(
    const TextureDesc& desc,
    ResourceState state) noexcept {
    switch (state) {
        case ResourceState::undefined: return true;
        case ResourceState::copy_source:
            return texture_supports_usage(desc, TextureUsageFlags::transfer_source);
        case ResourceState::copy_destination:
            return texture_supports_usage(desc, TextureUsageFlags::transfer_destination);
        case ResourceState::shader_read:
            return texture_supports_usage(desc, TextureUsageFlags::sampled);
        case ResourceState::storage_read_write:
            return texture_supports_usage(desc, TextureUsageFlags::storage);
        case ResourceState::color_attachment:
            return texture_supports_usage(desc, TextureUsageFlags::color_attachment);
        case ResourceState::depth_attachment:
            return texture_supports_usage(desc, TextureUsageFlags::depth_stencil);
        case ResourceState::present:
            return texture_supports_usage(desc, TextureUsageFlags::color_attachment);
    }
    return false;
}

[[nodiscard]] inline bool texture_usage_supported_by_format(
    const Capabilities& capabilities,
    const TextureDesc& desc) noexcept {
    const auto* support = find_format_support(capabilities, desc.format);
    if (!support) {
        return false;
    }

    const auto usage = effective_texture_usage(desc);
    return (!has_flag(usage, TextureUsageFlags::sampled) || support->sampled) &&
           (!has_flag(usage, TextureUsageFlags::color_attachment) ||
            support->colorAttachment) &&
           (!has_flag(usage, TextureUsageFlags::depth_stencil) ||
            support->depthStencilAttachment) &&
           (!has_flag(usage, TextureUsageFlags::storage) ||
            support->storageTexture) &&
           (!has_flag(usage, TextureUsageFlags::transfer_source) ||
            support->transferSource) &&
           (!has_flag(usage, TextureUsageFlags::transfer_destination) ||
            support->transferDestination);
}

[[nodiscard]] constexpr bool align_up(std::size_t value,
                                      std::size_t alignment,
                                      std::size_t& out) noexcept {
    if (!is_power_of_two(alignment)) {
        return false;
    }

    const auto mask = alignment - 1u;
    if (value > std::numeric_limits<std::size_t>::max() - mask) {
        return false;
    }

    out = (value + mask) & ~mask;
    return true;
}

[[nodiscard]] constexpr bool range_fits(std::size_t offset,
                                        std::size_t size,
                                        std::size_t limit) noexcept {
    return size != 0 && offset <= limit && size <= limit - offset;
}

[[nodiscard]] constexpr bool view_range_fits(std::size_t offset,
                                             std::size_t size,
                                             std::size_t limit) noexcept {
    if (size == 0) {
        return offset < limit;
    }
    return range_fits(offset, size, limit);
}

[[nodiscard]] inline bool buffer_view_valid(const BufferViewDesc& view) noexcept {
    if (!view.buffer) {
        return false;
    }

    const auto& desc = view.buffer->desc();
    return view_range_fits(view.offset, view.size, desc.size) &&
           buffer_supports_usage(desc, view.requiredUsage);
}

[[nodiscard]] inline bool texture_view_valid(const TextureViewDesc& view) noexcept {
    if (!view.texture) {
        return false;
    }

    const auto& desc = view.texture->desc();
    if (view.format != desc.format || view.dimension != desc.dimension ||
        !texture_supports_usage(desc, view.requiredUsage)) {
        return false;
    }

    return view.range.mipLevelCount != 0 &&
           view.range.arrayLayerCount != 0 &&
           view.range.baseMipLevel < desc.mipLevels &&
           view.range.mipLevelCount <= desc.mipLevels - view.range.baseMipLevel &&
           view.range.baseArrayLayer < desc.arrayLayers &&
           view.range.arrayLayerCount <=
               desc.arrayLayers - view.range.baseArrayLayer;
}

[[nodiscard]] inline bool debug_label_valid(
    const DebugLabelDesc& label) noexcept {
    if (label.name.empty()) {
        return false;
    }
    if (!label.hasColor) {
        return true;
    }
    return label.red >= 0.0f && label.red <= 1.0f &&
           label.green >= 0.0f && label.green <= 1.0f &&
           label.blue >= 0.0f && label.blue <= 1.0f &&
           label.alpha >= 0.0f && label.alpha <= 1.0f;
}

[[nodiscard]] inline bool viewport_valid(float x,
                                         float y,
                                         float width,
                                         float height,
                                         float minDepth,
                                         float maxDepth) noexcept {
    return std::isfinite(x) &&
           std::isfinite(y) &&
           std::isfinite(width) &&
           std::isfinite(height) &&
           std::isfinite(minDepth) &&
           std::isfinite(maxDepth) &&
           width > 0.0f &&
           height > 0.0f &&
           minDepth >= 0.0f &&
           maxDepth <= 1.0f &&
           minDepth <= maxDepth;
}

[[nodiscard]] constexpr bool scissor_valid(std::uint32_t x,
                                           std::uint32_t y,
                                           std::uint32_t width,
                                           std::uint32_t height) noexcept {
    return width != 0 &&
           height != 0 &&
           x <= std::numeric_limits<std::uint32_t>::max() - width &&
           y <= std::numeric_limits<std::uint32_t>::max() - height;
}

[[nodiscard]] constexpr bool sampler_filter_valid(SamplerFilter filter) noexcept {
    switch (filter) {
    case SamplerFilter::nearest:
    case SamplerFilter::linear:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool sampler_mipmap_mode_valid(
    SamplerMipmapMode mode) noexcept {
    switch (mode) {
    case SamplerMipmapMode::nearest:
    case SamplerMipmapMode::linear:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool sampler_address_mode_valid(
    SamplerAddressMode mode) noexcept {
    switch (mode) {
    case SamplerAddressMode::repeat:
    case SamplerAddressMode::mirrored_repeat:
    case SamplerAddressMode::clamp_to_edge:
    case SamplerAddressMode::clamp_to_border:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool sampler_compare_op_valid(
    SamplerCompareOp op) noexcept {
    switch (op) {
    case SamplerCompareOp::never:
    case SamplerCompareOp::less:
    case SamplerCompareOp::equal:
    case SamplerCompareOp::less_equal:
    case SamplerCompareOp::greater:
    case SamplerCompareOp::not_equal:
    case SamplerCompareOp::greater_equal:
    case SamplerCompareOp::always:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool sampler_border_color_valid(
    SamplerBorderColor color) noexcept {
    switch (color) {
    case SamplerBorderColor::transparent_black:
    case SamplerBorderColor::opaque_black:
    case SamplerBorderColor::opaque_white:
        return true;
    }
    return false;
}

[[nodiscard]] inline bool sampler_desc_valid(
    const SamplerDesc& desc,
    const Capabilities& capabilities) noexcept {
    return (!desc.minFilter || sampler_filter_valid(*desc.minFilter)) &&
           (!desc.magFilter || sampler_filter_valid(*desc.magFilter)) &&
           (!desc.mipmapMode || sampler_mipmap_mode_valid(*desc.mipmapMode)) &&
           sampler_address_mode_valid(desc.addressModeU) &&
           sampler_address_mode_valid(desc.addressModeV) &&
           sampler_address_mode_valid(desc.addressModeW) &&
           sampler_compare_op_valid(desc.compareOp) &&
           sampler_border_color_valid(desc.borderColor) &&
           std::isfinite(desc.mipLodBias) &&
           std::isfinite(desc.minLod) &&
           std::isfinite(desc.maxLod) &&
           desc.minLod <= desc.maxLod &&
           desc.maxAnisotropy != 0 &&
           desc.maxAnisotropy <= capabilities.limits.maxSamplerAnisotropy;
}

[[nodiscard]] inline bool bind_group_layout_valid(
    const BindGroupLayoutDesc& layout,
    const Capabilities& capabilities) noexcept {
    if (!pipeline_layout_valid({
        .debugName = layout.debugName,
        .bindings = layout.bindings,
    }, capabilities)) {
        return false;
    }

    for (std::size_t i = 0; i < layout.bindings.size(); ++i) {
        const auto& binding = layout.bindings[i];
        for (std::size_t j = i + 1; j < layout.bindings.size(); ++j) {
            const auto& other = layout.bindings[j];
            if (binding.bindingIndex == other.bindingIndex) {
                return false;
            }
            if (binding_native_slots_overlap(binding, other)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] inline bool buffer_binding_valid(
    const BufferBindingDesc& binding,
    BindingResourceType type,
    std::size_t minBindingSize) noexcept {
    if (!binding.buffer) {
        return false;
    }
    const auto& bufferDesc = binding.buffer->desc();
    const auto requiredUsage =
        type == BindingResourceType::uniform_buffer
            ? BufferUsageFlags::uniform
            : BufferUsageFlags::storage;
    if (binding.offset >= bufferDesc.size) {
        return false;
    }
    const auto bindingSize = binding.size == 0
        ? bufferDesc.size - binding.offset
        : binding.size;
    return range_fits(binding.offset, bindingSize, bufferDesc.size) &&
           bindingSize >= minBindingSize &&
           buffer_supports_usage(bufferDesc, requiredUsage);
}

[[nodiscard]] inline bool buffer_binding_valid_with_dynamic_offset(
    const BufferBindingDesc& binding,
    BindingResourceType type,
    std::size_t minBindingSize,
    std::size_t dynamicOffset,
    const DeviceLimits& limits) noexcept {
    if (!binding.buffer ||
        dynamicOffset >
            std::numeric_limits<std::size_t>::max() - binding.offset) {
        return false;
    }

    auto adjusted = binding;
    adjusted.offset += dynamicOffset;
    const auto alignment = type == BindingResourceType::uniform_buffer
        ? limits.minUniformBufferOffsetAlignment
        : limits.minStorageBufferOffsetAlignment;
    if (alignment == 0 ||
        (dynamicOffset % alignment) != 0 ||
        (adjusted.offset % alignment) != 0) {
        return false;
    }
    return buffer_binding_valid(adjusted, type, minBindingSize);
}

[[nodiscard]] inline bool texture_binding_valid(
    const ITexture* texture,
    BindingResourceType type) noexcept {
    if (!texture) {
        return false;
    }
    const auto requiredUsage =
        type == BindingResourceType::sampled_texture
            ? TextureUsageFlags::sampled
            : TextureUsageFlags::storage;
    return texture_supports_usage(texture->desc(), requiredUsage);
}

[[nodiscard]] inline bool bind_group_entry_valid(
    const BindGroupEntry& entry,
    const BindingLayoutDesc& layoutBinding) noexcept {
    if (entry.bindingIndex != layoutBinding.bindingIndex ||
        entry.type != layoutBinding.type) {
        return false;
    }

    const auto arrayCount = static_cast<std::size_t>(layoutBinding.arrayCount);
    switch (entry.type) {
    case BindingResourceType::uniform_buffer:
    case BindingResourceType::storage_buffer: {
        if (entry.texture || entry.sampler || !entry.textures.empty() ||
            !entry.samplers.empty()) {
            return false;
        }
        if (!entry.buffers.empty()) {
            if (entry.buffer.buffer || entry.buffers.size() != arrayCount) {
                return false;
            }
            for (const auto& buffer : entry.buffers) {
                if (!buffer_binding_valid(buffer, entry.type,
                                          layoutBinding.minBindingSize)) {
                    return false;
                }
            }
            return true;
        }
        return arrayCount == 1 &&
               buffer_binding_valid(entry.buffer, entry.type,
                                    layoutBinding.minBindingSize);
    }
    case BindingResourceType::sampled_texture:
    case BindingResourceType::storage_texture: {
        if (entry.buffer.buffer || entry.sampler || !entry.buffers.empty() ||
            !entry.samplers.empty()) {
            return false;
        }
        if (!entry.textures.empty()) {
            if (entry.texture || entry.textures.size() != arrayCount) {
                return false;
            }
            for (const auto* texture : entry.textures) {
                if (!texture_binding_valid(texture, entry.type)) {
                    return false;
                }
            }
            return true;
        }
        return arrayCount == 1 && texture_binding_valid(entry.texture, entry.type);
    }
    case BindingResourceType::sampler:
        if (entry.buffer.buffer || entry.texture || !entry.buffers.empty() ||
            !entry.textures.empty()) {
            return false;
        }
        if (!entry.samplers.empty()) {
            if (entry.sampler || entry.samplers.size() != arrayCount) {
                return false;
            }
            for (const auto* sampler : entry.samplers) {
                if (!sampler) {
                    return false;
                }
            }
            return true;
        }
        return arrayCount == 1 && entry.sampler != nullptr;
    }

    return false;
}

[[nodiscard]] inline bool bind_group_desc_valid(
    const BindGroupDesc& desc) noexcept {
    if (!desc.layout) {
        return false;
    }

    const auto& layout = desc.layout->desc();
    if (desc.entries.size() != layout.bindings.size()) {
        return false;
    }

    for (std::size_t i = 0; i < desc.entries.size(); ++i) {
        const auto& entry = desc.entries[i];
        const BindingLayoutDesc* layoutBinding = nullptr;
        for (const auto& candidate : layout.bindings) {
            if (candidate.bindingIndex == entry.bindingIndex) {
                layoutBinding = &candidate;
                break;
            }
        }
        if (!layoutBinding || !bind_group_entry_valid(entry, *layoutBinding)) {
            return false;
        }
        for (std::size_t j = i + 1; j < desc.entries.size(); ++j) {
            if (entry.bindingIndex == desc.entries[j].bindingIndex) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] inline const BindingLayoutDesc* find_binding_layout(
    const BindGroupLayoutDesc& layout,
    std::uint32_t bindingIndex) noexcept {
    for (const auto& binding : layout.bindings) {
        if (binding.bindingIndex == bindingIndex) {
            return &binding;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const BindGroupEntry* find_bind_group_entry(
    const BindGroupDesc& desc,
    std::uint32_t bindingIndex) noexcept {
    for (const auto& entry : desc.entries) {
        if (entry.bindingIndex == bindingIndex) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const BufferBindingDesc* find_buffer_binding(
    const BindGroupEntry& entry,
    std::uint32_t arrayElement) noexcept {
    if (!entry.buffers.empty()) {
        if (arrayElement >= entry.buffers.size()) {
            return nullptr;
        }
        return &entry.buffers[arrayElement];
    }
    return arrayElement == 0 ? &entry.buffer : nullptr;
}

[[nodiscard]] inline bool bind_group_dynamic_offsets_valid(
    const BindGroupDesc& desc,
    const std::vector<BindGroupDynamicOffset>& dynamicOffsets,
    const DeviceLimits& limits) noexcept {
    if (!bind_group_desc_valid(desc)) {
        return false;
    }

    const auto& layout = desc.layout->desc();
    std::size_t expectedOffsetCount = 0;
    for (const auto& binding : layout.bindings) {
        if (binding.dynamicOffset) {
            expectedOffsetCount += binding.arrayCount;
        }
    }
    if (dynamicOffsets.size() != expectedOffsetCount) {
        return false;
    }

    for (std::size_t i = 0; i < dynamicOffsets.size(); ++i) {
        const auto& offset = dynamicOffsets[i];
        for (std::size_t j = i + 1; j < dynamicOffsets.size(); ++j) {
            if (offset.bindingIndex == dynamicOffsets[j].bindingIndex &&
                offset.arrayElement == dynamicOffsets[j].arrayElement) {
                return false;
            }
        }

        const auto* layoutBinding =
            find_binding_layout(layout, offset.bindingIndex);
        if (!layoutBinding ||
            !layoutBinding->dynamicOffset ||
            (layoutBinding->type != BindingResourceType::uniform_buffer &&
             layoutBinding->type != BindingResourceType::storage_buffer) ||
            offset.arrayElement >= layoutBinding->arrayCount) {
            return false;
        }

        const auto* entry = find_bind_group_entry(desc, offset.bindingIndex);
        if (!entry) {
            return false;
        }
        const auto* buffer = find_buffer_binding(*entry, offset.arrayElement);
        if (!buffer ||
            !buffer_binding_valid_with_dynamic_offset(
                *buffer,
                layoutBinding->type,
                layoutBinding->minBindingSize,
                offset.offset,
                limits)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline bool bind_group_dynamic_offsets_valid(
    const BindGroupDesc& desc,
    const std::vector<BindGroupDynamicOffset>& dynamicOffsets) noexcept {
    return bind_group_dynamic_offsets_valid(desc, dynamicOffsets, DeviceLimits{});
}

[[nodiscard]] inline bool buffer_barrier_valid(
    const BufferBarrierDesc& barrier) noexcept {
    if (!barrier.buffer) {
        return false;
    }
    const auto& desc = barrier.buffer->desc();
    return buffer_supports_state(desc, barrier.before) &&
           buffer_supports_state(desc, barrier.after);
}

[[nodiscard]] inline bool texture_barrier_valid(
    const TextureBarrierDesc& barrier) noexcept {
    if (!barrier.texture) {
        return false;
    }

    const auto& desc = barrier.texture->desc();
    return texture_supports_state(desc, barrier.before) &&
           texture_supports_state(desc, barrier.after) &&
           barrier.range.mipLevelCount != 0 &&
           barrier.range.arrayLayerCount != 0 &&
           barrier.range.baseMipLevel < desc.mipLevels &&
           barrier.range.mipLevelCount <=
               desc.mipLevels - barrier.range.baseMipLevel &&
           barrier.range.baseArrayLayer < desc.arrayLayers &&
           barrier.range.arrayLayerCount <=
               desc.arrayLayers - barrier.range.baseArrayLayer;
}

} // namespace truffle::rhi::validation
