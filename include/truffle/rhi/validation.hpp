#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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
    if (binding.bindingIndex >= capabilities.limits.maxResourceBindings ||
        binding.arrayCount == 0 ||
        binding.arrayCount > capabilities.limits.maxDescriptorArrayElements ||
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

    return true;
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
            if (binding.bindingIndex == other.bindingIndex &&
                (binding.visibility & other.visibility) != ShaderStageFlags::none) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool pipeline_render_state_valid(
    const PipelineDesc& desc,
    const Capabilities& capabilities) noexcept {
    const auto* colorSupport =
        find_format_support(capabilities, desc.colorFormat);
    if (!colorSupport || !colorSupport->colorAttachment) {
        return false;
    }

    if (desc.depthTest || desc.depthWrite) {
        const auto* depthSupport =
            find_format_support(capabilities, TextureFormat::depth32_float);
        if (!depthSupport || !depthSupport->depthStencilAttachment) {
            return false;
        }
    }

    return true;
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
    return pipeline_layout_valid({
        .debugName = layout.debugName,
        .bindings = layout.bindings,
    }, capabilities);
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
