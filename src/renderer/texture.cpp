#include "truffle/renderer/texture.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace truffle::renderer {
namespace {

std::size_t texel_size(rhi::TextureFormat format) {
    switch (format) {
    case rhi::TextureFormat::r8_unorm: return 1;
    case rhi::TextureFormat::rg8_unorm: return 2;
    case rhi::TextureFormat::rgba8_unorm: case rhi::TextureFormat::rgba8_srgb:
    case rhi::TextureFormat::bgra8_unorm: case rhi::TextureFormat::bgra8_srgb: return 4;
    case rhi::TextureFormat::rgba16_float: return 8;
    case rhi::TextureFormat::rgba32_float: return 16;
    default: return 0;
    }
}

base::Status invalid(const char* message) {
    return base::Status::failure(base::StatusCode::invalid_argument, message);
}

// Repacks rows to a 256-byte pitch (the strictest common backend copy rule),
// copies them into the texture and waits, leaving it shader-readable.
base::Status copy_into(rhi::Device& device, rhi::Texture& texture, bool initialized,
                       rhi::Origin3D origin, rhi::Extent2D extent,
                       std::span<const std::byte> bytes, std::size_t bytesPerRow) {
    const auto desc = texture.desc();
    const auto texel = texel_size(desc.format);
    const auto row = static_cast<std::size_t>(extent.width) * texel;
    const auto pitch = bytesPerRow == 0 ? row : bytesPerRow;
    if (extent.width == 0 || extent.height == 0 || pitch < row ||
        origin.z != 0 || origin.x > desc.extent.width || origin.y > desc.extent.height ||
        extent.width > desc.extent.width - origin.x ||
        extent.height > desc.extent.height - origin.y ||
        bytes.size() < pitch * (extent.height - 1) + row) {
        return invalid("SampledTexture: region or byte layout out of range");
    }
    const auto stagingPitch = (row + 255) & ~std::size_t{255};
    std::vector<std::byte> packed(stagingPitch * extent.height);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        std::memcpy(packed.data() + y * stagingPitch, bytes.data() + y * pitch, row);
    }
    auto staging = device.create_buffer({
        .size = packed.size(), .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload, .debugName = "Renderer texture staging",
    });
    if (!staging.ok()) {
        return staging.status();
    }
    if (auto status = staging.value().write(0, packed); !status.ok()) {
        return status;
    }
    auto pool = device.create_command_pool(rhi::QueueKind::graphics);
    if (!pool.ok()) {
        return pool.status();
    }
    auto listResult = pool.value().allocate();
    if (!listResult.ok()) {
        return listResult.status();
    }
    auto list = std::move(listResult).value();
    if (auto status = list.begin(); !status.ok()) {
        return status;
    }
    rhi::BarrierBatch toCopy;
    toCopy.textures.push_back({
        .texture = &texture,
        .oldLayout = initialized ? rhi::TextureLayout::shader_read_only
                                 : rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::transfer_destination,
        .sourceStages = initialized ? rhi::PipelineStage::fragment_shader
                                    : rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = initialized ? rhi::Access::shader_read : rhi::Access::none,
        .destinationAccess = rhi::Access::transfer_write,
    });
    if (auto status = list.barrier(toCopy); !status.ok()) {
        return status;
    }
    auto copy = list.begin_copy();
    if (!copy.ok()) {
        return copy.status();
    }
    if (auto status = copy.value().copy_buffer_to_texture(
            staging.value(), texture,
            {.layout = {.bytesPerRow = stagingPitch, .rowsPerImage = extent.height},
             .texture = {.origin = origin, .extent = {extent.width, extent.height, 1}}});
        !status.ok()) {
        return status;
    }
    if (auto status = copy.value().end(); !status.ok()) {
        return status;
    }
    rhi::BarrierBatch toRead;
    toRead.textures.push_back({
        .texture = &texture,
        .oldLayout = rhi::TextureLayout::transfer_destination,
        .newLayout = rhi::TextureLayout::shader_read_only,
        .sourceStages = rhi::PipelineStage::copy,
        .destinationStages = rhi::PipelineStage::fragment_shader,
        .sourceAccess = rhi::Access::transfer_write,
        .destinationAccess = rhi::Access::shader_read,
    });
    if (auto status = list.barrier(toRead); !status.ok()) {
        return status;
    }
    if (auto status = list.end(); !status.ok()) {
        return status;
    }
    auto fence = device.create_fence();
    if (!fence.ok()) {
        return fence.status();
    }
    auto queue = device.queue(rhi::QueueKind::graphics);
    if (!queue.ok()) {
        return queue.status();
    }
    const std::array<rhi::CommandList*, 1> lists{&list};
    if (auto status = queue.value().submit(lists, &fence.value(), 1); !status.ok()) {
        return status;
    }
    return fence.value().wait(1, std::chrono::seconds(10));
}

} // namespace

base::Result<SampledTexture> SampledTexture::upload(rhi::Device& device,
                                                    const TextureUpload& desc) {
    if (texel_size(desc.format) == 0 || desc.extent.width == 0 || desc.extent.height == 0) {
        return invalid("SampledTexture: nonzero extent and uncompressed color format required");
    }
    auto texture = device.create_texture({
        .extent = {desc.extent.width, desc.extent.height, 1}, .format = desc.format,
        .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_destination,
        .debugName = desc.debugName,
    });
    if (!texture.ok()) {
        return texture.status();
    }
    SampledTexture result;
    result.texture_ = std::move(texture).value();
    if (auto status = copy_into(device, result.texture_, false, {}, desc.extent,
                                desc.bytes, desc.bytesPerRow);
        !status.ok()) {
        return status;
    }
    auto view = device.create_texture_view(result.texture_, {.format = desc.format});
    if (!view.ok()) {
        return view.status();
    }
    result.view_ = std::move(view).value();
    return result;
}

base::Status SampledTexture::update(rhi::Device& device, rhi::Origin3D origin,
                                    rhi::Extent2D extent,
                                    std::span<const std::byte> bytes,
                                    std::size_t bytesPerRow) {
    if (!texture_.valid()) {
        return base::Status::failure(base::StatusCode::invalid_state,
                                     "SampledTexture: not uploaded");
    }
    return copy_into(device, texture_, true, origin, extent, bytes, bytesPerRow);
}

base::Result<TextureBindingLayout> TextureBindingLayout::create(
    rhi::Device& device, rhi::ShaderStageMask visibility) {
    auto group = device.create_bind_group_layout({
        .entries = {
            {.binding = 0, .type = rhi::BindingType::sampled_texture, .visibility = visibility},
            {.binding = 1, .type = rhi::BindingType::sampler, .visibility = visibility},
        },
        .debugName = "Renderer texture binding layout",
    });
    if (!group.ok()) {
        return group.status();
    }
    TextureBindingLayout result;
    result.group_ = std::move(group).value();
    auto pipeline = device.create_pipeline_layout({
        .bindGroupLayouts = {&result.group_},
        .debugName = "Renderer texture pipeline layout",
    });
    if (!pipeline.ok()) {
        return pipeline.status();
    }
    result.pipeline_ = std::move(pipeline).value();
    return result;
}

base::Result<TextureBinding> TextureBinding::create(rhi::Device& device,
                                                    TextureBindingLayout& layout,
                                                    SampledTexture& texture,
                                                    const rhi::SamplerDesc& sampler) {
    if (!layout.group_layout().valid() || !texture.view().valid()) {
        return invalid("TextureBinding: layout and texture must be created first");
    }
    TextureBinding result;
    auto created = device.create_sampler(sampler);
    if (!created.ok()) {
        return created.status();
    }
    result.sampler_ = std::move(created).value();
    auto arena = device.create_descriptor_arena({
        .maxBindGroups = 1, .maxDescriptors = 2,
        .debugName = "Renderer texture binding arena",
    });
    if (!arena.ok()) {
        return arena.status();
    }
    result.arena_ = std::move(arena).value();
    auto group = device.create_bind_group({
        .layout = &layout.group_layout(), .arena = &result.arena_,
        .entries = {{.binding = 0, .textureView = &texture.view()},
                    {.binding = 1, .sampler = &result.sampler_}},
        .debugName = "Renderer texture binding",
    });
    if (!group.ok()) {
        return group.status();
    }
    result.group_ = std::move(group).value();
    return result;
}

} // namespace truffle::renderer

