#include "test_support.hpp"

#include "truffle/core/config.hpp"
#include "truffle/core/handle.hpp"
#include "truffle/core/status.hpp"
#include "truffle/rhi/validation.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

struct TestHandleTag {};

class TestBuffer final : public truffle::rhi::IBuffer {
public:
    explicit TestBuffer(truffle::rhi::BufferDesc desc) : desc_(desc) {}
    const truffle::rhi::BufferDesc& desc() const noexcept override { return desc_; }

private:
    truffle::rhi::BufferDesc desc_;
};

class TestTexture final : public truffle::rhi::ITexture {
public:
    explicit TestTexture(truffle::rhi::TextureDesc desc) : desc_(desc) {}
    const truffle::rhi::TextureDesc& desc() const noexcept override { return desc_; }

private:
    truffle::rhi::TextureDesc desc_;
};

class TestSampler final : public truffle::rhi::ISampler {};

class TestBindGroupLayout final : public truffle::rhi::IBindGroupLayout {
public:
    explicit TestBindGroupLayout(truffle::rhi::BindGroupLayoutDesc desc)
        : desc_(desc) {}
    const truffle::rhi::BindGroupLayoutDesc& desc() const noexcept override {
        return desc_;
    }

private:
    truffle::rhi::BindGroupLayoutDesc desc_;
};

} // namespace

int main() {
    const auto ok = truffle::core::Status::success();
    TRUFFLE_CHECK(ok.ok());

    const auto failure = truffle::core::Status::failure(
        truffle::core::StatusCode::invalid_argument, "bad argument");
    TRUFFLE_CHECK(!failure.ok());
    TRUFFLE_CHECK(failure.code == truffle::core::StatusCode::invalid_argument);
    TRUFFLE_CHECK(failure.message == "bad argument");

    truffle::core::Result<int> valueResult{42};
    TRUFFLE_CHECK(valueResult.ok());
    TRUFFLE_CHECK(valueResult.value() == 42);

    truffle::core::Result<int> errorResult{failure};
    TRUFFLE_CHECK(!errorResult.ok());
    TRUFFLE_CHECK(errorResult.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    using TestHandle = truffle::core::Handle<TestHandleTag>;
    constexpr TestHandle defaultHandle;
    static_assert(!defaultHandle.valid());
    static_assert(defaultHandle.value() == TestHandle::invalid_value);

    constexpr TestHandle explicitHandle{7};
    static_assert(explicitHandle.valid());
    static_assert(explicitHandle.value() == 7);
    static_assert(explicitHandle == TestHandle{7});

    const truffle::core::RuntimeConfig config;
    TRUFFLE_CHECK(config.application_name == "Truffle Consumer");
    TRUFFLE_CHECK(config.enable_validation);

    TRUFFLE_CHECK(truffle::rhi::validation::is_non_zero({1, 1}));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_non_zero({0, 1}));
    TRUFFLE_CHECK(truffle::rhi::validation::extent_within({8, 8}, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::extent_within({17, 8}, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::extent_within({8, 8}, 0));
    TRUFFLE_CHECK(truffle::rhi::validation::is_power_of_two(1));
    TRUFFLE_CHECK(truffle::rhi::validation::is_power_of_two(64));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_power_of_two(0));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_power_of_two(3));

    std::size_t aligned = 0;
    TRUFFLE_CHECK(truffle::rhi::validation::align_up(17, 8, aligned));
    TRUFFLE_CHECK(aligned == 24);
    TRUFFLE_CHECK(!truffle::rhi::validation::align_up(17, 0, aligned));
    TRUFFLE_CHECK(!truffle::rhi::validation::align_up(17, 3, aligned));

    TRUFFLE_CHECK(truffle::rhi::validation::range_fits(8, 4, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::range_fits(8, 9, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::range_fits(
        static_cast<std::size_t>(-4), 8, static_cast<std::size_t>(-1)));
    TRUFFLE_CHECK(truffle::rhi::validation::debug_label_valid({
        .name = "core-label",
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::debug_label_valid({
        .name = "core-label-with-color",
        .hasColor = true,
        .red = 0.1f,
        .green = 0.2f,
        .blue = 0.3f,
        .alpha = 1.0f,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::debug_label_valid({}));
    TRUFFLE_CHECK(!truffle::rhi::validation::debug_label_valid({
        .name = "bad-color",
        .hasColor = true,
        .red = 1.1f,
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::viewport_valid(
        0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f));
    TRUFFLE_CHECK(!truffle::rhi::validation::viewport_valid(
        0.0f, 0.0f, 0.0f, 480.0f, 0.0f, 1.0f));
    TRUFFLE_CHECK(!truffle::rhi::validation::viewport_valid(
        0.0f, 0.0f, 640.0f, 480.0f, 0.75f, 0.25f));
    TRUFFLE_CHECK(!truffle::rhi::validation::viewport_valid(
        0.0f, 0.0f, 640.0f, 480.0f, -0.1f, 1.0f));
    TRUFFLE_CHECK(truffle::rhi::validation::scissor_valid(0, 0, 640, 480));
    TRUFFLE_CHECK(!truffle::rhi::validation::scissor_valid(0, 0, 0, 480));
    TRUFFLE_CHECK(!truffle::rhi::validation::scissor_valid(
        std::numeric_limits<std::uint32_t>::max(), 0, 1, 1));

    truffle::rhi::BufferDesc legacyStorage;
    legacyStorage.usage = truffle::rhi::BufferUsage::storage;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(legacyStorage),
        truffle::rhi::BufferUsageFlags::storage));

    truffle::rhi::BufferDesc multiUseBuffer;
    multiUseBuffer.usageFlags = truffle::rhi::BufferUsageFlags::vertex |
                                truffle::rhi::BufferUsageFlags::uniform;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(multiUseBuffer),
        truffle::rhi::BufferUsageFlags::vertex));
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(multiUseBuffer),
        truffle::rhi::BufferUsageFlags::uniform));
    TRUFFLE_CHECK(truffle::rhi::validation::buffer_supports_state(
        multiUseBuffer, truffle::rhi::ResourceState::shader_read));
    TRUFFLE_CHECK(!truffle::rhi::validation::buffer_supports_state(
        multiUseBuffer, truffle::rhi::ResourceState::copy_destination));

    truffle::rhi::TextureDesc colorTexture;
    colorTexture.format = truffle::rhi::TextureFormat::rgba8_unorm;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_texture_usage(colorTexture),
        truffle::rhi::TextureUsageFlags::color_attachment));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_supports_state(
        colorTexture, truffle::rhi::ResourceState::color_attachment));
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_supports_state(
        colorTexture, truffle::rhi::ResourceState::copy_destination));

    truffle::rhi::TextureDesc depthTexture;
    depthTexture.format = truffle::rhi::TextureFormat::depth32_float;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_texture_usage(depthTexture),
        truffle::rhi::TextureUsageFlags::depth_stencil));
    TRUFFLE_CHECK(
        truffle::rhi::texture_format_has_depth_aspect(depthTexture.format));
    TRUFFLE_CHECK(
        !truffle::rhi::texture_format_has_stencil_aspect(depthTexture.format));
    truffle::rhi::TextureDesc depthStencilTextureDesc;
    depthStencilTextureDesc.format =
        truffle::rhi::TextureFormat::depth32_float_stencil8;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_texture_usage(depthStencilTextureDesc),
        truffle::rhi::TextureUsageFlags::depth_stencil));
    TRUFFLE_CHECK(truffle::rhi::texture_format_has_depth_aspect(
        depthStencilTextureDesc.format));
    TRUFFLE_CHECK(truffle::rhi::texture_format_has_stencil_aspect(
        depthStencilTextureDesc.format));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));
    depthTexture.mipLevels = 0;
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));
    depthTexture.mipLevels = 1;
    depthTexture.dimension = truffle::rhi::TextureDimension::cube;
    depthTexture.arrayLayers = 5;
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));

    truffle::rhi::Capabilities capabilities;
    capabilities.maxFramesInFlight = 2;
    capabilities.queues = {.graphics = true, .compute = true};
    capabilities.limits.maxTextureDimension2D = 64;
    capabilities.limits.maxDescriptorArrayElements = 4;
    capabilities.limits.maxSamplerAnisotropy = 8;
    capabilities.features.descriptorArrays = true;
    capabilities.descriptorPolicy = {
        .mappingModel =
            truffle::rhi::NativeDescriptorMappingModel::direct_slots,
        .allocationModel =
            truffle::rhi::NativeDescriptorAllocationModel::inline_direct,
        .updateModel =
            truffle::rhi::NativeDescriptorUpdateModel::direct_write,
        .budgetModel =
            truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans,
        .flattenedNativeBindings = true,
    };
    capabilities.memoryHeaps = {{
        .kind = truffle::rhi::MemoryHeapKind::unified,
    }};
    capabilities.formats = {
        {
            .format = truffle::rhi::TextureFormat::rgba8_unorm,
            .sampled = true,
            .colorAttachment = true,
        },
        {
            .format = truffle::rhi::TextureFormat::depth32_float,
            .sampled = true,
            .depthStencilAttachment = true,
        },
        {
            .format = truffle::rhi::TextureFormat::depth32_float_stencil8,
            .sampled = true,
            .depthStencilAttachment = true,
        },
    };
    capabilities.surfaceKinds = {truffle::rhi::NativeSurfaceKind::headless};
    capabilities.shaderFormats = {
        truffle::rhi::ShaderByteFormat::contract,
        truffle::rhi::ShaderByteFormat::spirv_binary,
    };
    TRUFFLE_CHECK(truffle::rhi::validation::frame_count_supported(2, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::frame_count_supported(0, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::frame_count_supported(3, capabilities));
    TRUFFLE_CHECK(truffle::rhi::supports_queue(
        capabilities, truffle::rhi::QueueKind::graphics));
    TRUFFLE_CHECK(!truffle::rhi::supports_queue(
        capabilities, truffle::rhi::QueueKind::transfer));
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        capabilities, truffle::rhi::TextureFormat::rgba8_unorm));
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        capabilities, truffle::rhi::TextureFormat::depth32_float));
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        capabilities, truffle::rhi::TextureFormat::depth32_float_stencil8));
    TRUFFLE_CHECK(truffle::rhi::validation::memory_domain_supported(
        truffle::rhi::MemoryDomain::upload, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::buffer_memory_mappable({
        .size = 16,
        .memory = truffle::rhi::MemoryDomain::automatic,
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::buffer_memory_mappable({
        .size = 16,
        .memory = truffle::rhi::MemoryDomain::upload,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::buffer_memory_mappable({
        .size = 16,
        .memory = truffle::rhi::MemoryDomain::device_local,
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_usage_supported_by_format(
        capabilities, colorTexture));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_usage_supported_by_format(
        capabilities, depthTexture));
    auto unsupportedDepthUsage = depthTexture;
    unsupportedDepthUsage.usageFlags = truffle::rhi::TextureUsageFlags::color_attachment;
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_usage_supported_by_format(
        capabilities, unsupportedDepthUsage));

    TRUFFLE_CHECK(truffle::rhi::supports_native_surface_kind(
        capabilities, truffle::rhi::NativeSurfaceKind::headless));
    TRUFFLE_CHECK(!truffle::rhi::supports_native_surface_kind(
        capabilities, truffle::rhi::NativeSurfaceKind::win32));
    TRUFFLE_CHECK(truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::headless,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::headless,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::cocoa_layer,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::xcb,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::surface_supported({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {32, 32},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::surface_supported({
        .native = {
            .kind = truffle::rhi::NativeSurfaceKind::win32,
            .handle = reinterpret_cast<void*>(0x1),
        },
        .initialExtent = {32, 32},
    }, capabilities));
    TRUFFLE_CHECK(truffle::rhi::supports_shader_byte_format(
        capabilities, truffle::rhi::ShaderByteFormat::unknown));
    TRUFFLE_CHECK(truffle::rhi::supports_shader_byte_format(
        capabilities, truffle::rhi::ShaderByteFormat::contract));
    TRUFFLE_CHECK(!truffle::rhi::supports_shader_byte_format(
        capabilities, truffle::rhi::ShaderByteFormat::msl_source));
    truffle::rhi::SamplerDesc legacyNearestSampler;
    legacyNearestSampler.linear_filtering = false;
    TRUFFLE_CHECK(truffle::rhi::effective_min_filter(legacyNearestSampler) ==
                  truffle::rhi::SamplerFilter::nearest);
    TRUFFLE_CHECK(truffle::rhi::effective_mag_filter(legacyNearestSampler) ==
                  truffle::rhi::SamplerFilter::nearest);
    TRUFFLE_CHECK(truffle::rhi::effective_mipmap_mode(legacyNearestSampler) ==
                  truffle::rhi::SamplerMipmapMode::nearest);
    TRUFFLE_CHECK(truffle::rhi::validation::sampler_desc_valid(
        legacyNearestSampler, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::sampler_desc_valid({
        .minFilter = truffle::rhi::SamplerFilter::nearest,
        .magFilter = truffle::rhi::SamplerFilter::linear,
        .mipmapMode = truffle::rhi::SamplerMipmapMode::nearest,
        .addressModeU = truffle::rhi::SamplerAddressMode::repeat,
        .addressModeV = truffle::rhi::SamplerAddressMode::mirrored_repeat,
        .addressModeW = truffle::rhi::SamplerAddressMode::clamp_to_border,
        .minLod = 0.0f,
        .maxLod = 4.0f,
        .maxAnisotropy = 8,
        .compareEnabled = true,
        .compareOp = truffle::rhi::SamplerCompareOp::less_equal,
        .borderColor = truffle::rhi::SamplerBorderColor::opaque_white,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::sampler_desc_valid({
        .maxAnisotropy = 0,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::sampler_desc_valid({
        .maxAnisotropy = 9,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::sampler_desc_valid({
        .minLod = 2.0f,
        .maxLod = 1.0f,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::sampler_desc_valid({
        .minFilter = static_cast<truffle::rhi::SamplerFilter>(99),
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::sampler_desc_valid({
        .addressModeU = static_cast<truffle::rhi::SamplerAddressMode>(99),
    }, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::shader_desc_supported({
        .stage = truffle::rhi::ShaderStage::vertex,
        .byteFormat = truffle::rhi::ShaderByteFormat::contract,
        .entryPoint = "main",
        .bytecode = {std::byte{0x1}},
    }, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::shader_payload_valid({
        .stage = truffle::rhi::ShaderStage::vertex,
        .byteFormat = truffle::rhi::ShaderByteFormat::spirv_binary,
        .entryPoint = "main",
        .bytecode = {
            std::byte{0x03},
            std::byte{0x02},
            std::byte{0x23},
            std::byte{0x07},
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::shader_payload_valid({
        .stage = truffle::rhi::ShaderStage::vertex,
        .byteFormat = truffle::rhi::ShaderByteFormat::spirv_binary,
        .entryPoint = "main",
        .bytecode = {std::byte{0x1}, std::byte{0x2}, std::byte{0x3}},
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::shader_desc_supported({
        .stage = truffle::rhi::ShaderStage::vertex,
        .byteFormat = truffle::rhi::ShaderByteFormat::msl_source,
        .entryPoint = "main",
        .bytecode = {std::byte{0x1}},
    }, capabilities));
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::ShaderStageFlags::graphics,
        truffle::rhi::ShaderStageFlags::vertex));
    TRUFFLE_CHECK(truffle::rhi::shader_stage_flag(
        truffle::rhi::ShaderStage::compute) ==
                  truffle::rhi::ShaderStageFlags::compute);
    const truffle::rhi::PipelineLayoutDesc groupedPipelineLayout{
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 64,
                .groupIndex = 0,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
                .groupIndex = 1,
            },
        },
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_valid(
        groupedPipelineLayout, capabilities));
    const truffle::rhi::BindGroupLayoutDesc footprintLayout{
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .arrayCount = 1,
                .dynamicOffset = true,
                .nativeSlot = 4,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::compute,
                .arrayCount = 2,
                .dynamicOffset = true,
                .nativeSlot = 5,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
                .nativeSlot = 7,
            },
            {
                .bindingIndex = 3,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .nativeSlot = 3,
            },
        },
    };
    const auto footprint =
        truffle::rhi::bind_group_descriptor_footprint(footprintLayout);
    TRUFFLE_CHECK(footprint.bindingCount == 4);
    TRUFFLE_CHECK(footprint.descriptorCount == 6);
    TRUFFLE_CHECK(footprint.dynamicOffsetCount == 3);
    TRUFFLE_CHECK(footprint.bufferDescriptorCount == 3);
    TRUFFLE_CHECK(footprint.textureDescriptorCount == 2);
    TRUFFLE_CHECK(footprint.samplerDescriptorCount == 1);
    TRUFFLE_CHECK(footprint.bufferSlots.firstSlot == 4);
    TRUFFLE_CHECK(footprint.bufferSlots.slotCount == 3);
    TRUFFLE_CHECK(footprint.textureSlots.firstSlot == 7);
    TRUFFLE_CHECK(footprint.textureSlots.slotCount == 2);
    TRUFFLE_CHECK(footprint.samplerSlots.firstSlot == 3);
    TRUFFLE_CHECK(footprint.samplerSlots.slotCount == 1);
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .visibility = truffle::rhi::ShaderStageFlags::none,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 99,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = capabilities.limits.maxResourceBindings - 1,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .arrayCount = 2,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .groupIndex = capabilities.limits.maxBindGroups,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {
            {
                .bindingIndex = 1,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
            },
            {
                .bindingIndex = 1,
                .visibility = truffle::rhi::ShaderStageFlags::graphics,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 0,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 1,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 0,
                .nativeSlot = 0,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 1,
                .nativeSlot = 2,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 0,
                .nativeSlot = 0,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .groupIndex = 1,
                .nativeSlot = 0,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .arrayCount = 2,
            .nativeSlot = capabilities.limits.maxResourceBindings - 1,
        }},
    }, capabilities));
    TRUFFLE_CHECK(
        truffle::rhi::validation::effective_native_binding_slot({
            .bindingIndex = 3,
            .nativeSlot = 7,
        }) == 7);
    TRUFFLE_CHECK(
        truffle::rhi::validation::effective_native_binding_slot({
            .bindingIndex = 3,
        }) == 3);
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::storage_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
            },
        },
    }, capabilities));
    const truffle::rhi::BindGroupLayoutDesc group0Layout{
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .minBindingSize = 64,
        }},
    };
    const truffle::rhi::BindGroupLayoutDesc group1Layout{
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampler,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 2,
        }},
    };
    const auto extractedGroup0Layout =
        truffle::rhi::pipeline_layout_bind_group_layout(groupedPipelineLayout, 0);
    TRUFFLE_CHECK(extractedGroup0Layout.has_value());
    TRUFFLE_CHECK(extractedGroup0Layout->bindings.size() == 1);
    TRUFFLE_CHECK(extractedGroup0Layout->bindings[0].groupIndex == 0);
    TRUFFLE_CHECK(extractedGroup0Layout->bindings[0].bindingIndex == 0);
    TRUFFLE_CHECK(extractedGroup0Layout->bindings[0].minBindingSize == 64);
    const auto extractedGroup1Layout =
        truffle::rhi::pipeline_layout_bind_group_layout(groupedPipelineLayout, 1);
    TRUFFLE_CHECK(extractedGroup1Layout.has_value());
    TRUFFLE_CHECK(extractedGroup1Layout->bindings.size() == 1);
    TRUFFLE_CHECK(extractedGroup1Layout->bindings[0].groupIndex == 0);
    TRUFFLE_CHECK(extractedGroup1Layout->bindings[0].arrayCount == 2);
    TRUFFLE_CHECK(!truffle::rhi::pipeline_layout_bind_group_layout(
        groupedPipelineLayout, 2).has_value());
    TRUFFLE_CHECK(truffle::rhi::bind_group_layout_compatible(
        *extractedGroup0Layout, group0Layout));
    TRUFFLE_CHECK(truffle::rhi::bind_group_layout_compatible(
        group0Layout, *extractedGroup0Layout));
    TRUFFLE_CHECK(!truffle::rhi::bind_group_layout_compatible(
        *extractedGroup0Layout, group1Layout));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 0, group0Layout));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 1, group1Layout));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 2, group0Layout));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 1, group0Layout));
    const auto groupedPipelineBudget =
        truffle::rhi::pipeline_layout_descriptor_budget(
            groupedPipelineLayout, capabilities);
    TRUFFLE_CHECK(groupedPipelineBudget.bindGroupCount == 2);
    TRUFFLE_CHECK(groupedPipelineBudget.maxBudgetPerBindGroup.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(groupedPipelineBudget.maxBudgetPerBindGroup.totalUnits == 2);
    TRUFFLE_CHECK(groupedPipelineBudget.maxBudgetPerBindGroup.bufferUnits == 1);
    TRUFFLE_CHECK(groupedPipelineBudget.maxBudgetPerBindGroup.textureUnits == 0);
    TRUFFLE_CHECK(groupedPipelineBudget.maxBudgetPerBindGroup.samplerUnits == 2);
    TRUFFLE_CHECK(groupedPipelineBudget.totalBudget.totalUnits == 3);
    TRUFFLE_CHECK(groupedPipelineBudget.totalBudget.bufferUnits == 1);
    TRUFFLE_CHECK(groupedPipelineBudget.totalBudget.textureUnits == 0);
    TRUFFLE_CHECK(groupedPipelineBudget.totalBudget.samplerUnits == 2);
    const truffle::rhi::PipelineLayoutDesc explicitNativeSlotLayout{
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .minBindingSize = 64,
            .nativeSlot = 4,
        }},
    };
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        explicitNativeSlotLayout, 0, group0Layout));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        explicitNativeSlotLayout,
        0,
        {
            .bindings = {{
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 64,
                .nativeSlot = 4,
            }},
        }));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_required_groups_bound(
        groupedPipelineLayout, {}));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_required_groups_bound(
        groupedPipelineLayout, {0}));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_required_groups_bound(
        groupedPipelineLayout, {1, 0}));
    auto noDescriptorArrayCaps = capabilities;
    noDescriptorArrayCaps.features.descriptorArrays = false;
    noDescriptorArrayCaps.limits.maxDescriptorArrayElements = 1;
    TRUFFLE_CHECK(!truffle::rhi::supports_descriptor_arrays(
        noDescriptorArrayCaps));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 2,
        }},
    }, noDescriptorArrayCaps));
    TRUFFLE_CHECK(!truffle::rhi::supports_dynamic_resource_indexing(
        capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 2,
            .dynamicIndexing = true,
        }},
    }, capabilities));
    auto dynamicIndexingCaps = capabilities;
    dynamicIndexingCaps.features.dynamicResourceIndexing = true;
    TRUFFLE_CHECK(truffle::rhi::supports_dynamic_resource_indexing(
        dynamicIndexingCaps));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 2,
            .dynamicIndexing = true,
        }},
    }, dynamicIndexingCaps));
    TRUFFLE_CHECK(!truffle::rhi::supports_bindless_resources(
        dynamicIndexingCaps));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 2,
            .dynamicIndexing = true,
            .bindless = true,
        }},
    }, dynamicIndexingCaps));
    auto singleSlotBindlessCaps = dynamicIndexingCaps;
    singleSlotBindlessCaps.features.bindlessResources = true;
    singleSlotBindlessCaps.limits.maxBindlessResources = 1;
    TRUFFLE_CHECK(!truffle::rhi::supports_bindless_resources(
        singleSlotBindlessCaps));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 1,
            .dynamicIndexing = true,
            .bindless = true,
        }},
    }, singleSlotBindlessCaps));
    auto bindlessCaps = dynamicIndexingCaps;
    bindlessCaps.features.bindlessResources = true;
    bindlessCaps.limits.maxBindlessResources = 3;
    TRUFFLE_CHECK(truffle::rhi::supports_bindless_resources(bindlessCaps));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_flattens_native_bindings(capabilities));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_preserves_group_bindings(capabilities));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_supports_direct_updates(capabilities));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_updates_via_allocation_copies(
            capabilities));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_rebuilds_allocations_for_updates(
            capabilities));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_uses_native_slot_budgets(capabilities));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_uses_descriptor_count_budgets(
            capabilities));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_uses_bind_group_budgets(capabilities));
    auto groupedDescriptorCaps = capabilities;
    groupedDescriptorCaps.descriptorPolicy = {
        .mappingModel =
            truffle::rhi::NativeDescriptorMappingModel::descriptor_sets,
        .allocationModel =
            truffle::rhi::NativeDescriptorAllocationModel::bind_group_owned,
        .updateModel =
            truffle::rhi::NativeDescriptorUpdateModel::copy_into_allocation,
        .budgetModel =
            truffle::rhi::NativeDescriptorBudgetModel::descriptor_count,
        .flattenedNativeBindings = false,
    };
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_flattens_native_bindings(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_preserves_group_bindings(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_supports_direct_updates(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_updates_via_allocation_copies(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_rebuilds_allocations_for_updates(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_uses_native_slot_budgets(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_uses_descriptor_count_budgets(
            groupedDescriptorCaps));
    TRUFFLE_CHECK(
        !truffle::rhi::descriptor_policy_uses_bind_group_budgets(
            groupedDescriptorCaps));
    const truffle::rhi::BindGroupLayoutDesc gappedBudgetLayout{
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .nativeSlot = 0,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::compute,
                .nativeSlot = 2,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .nativeSlot = 4,
            },
        },
    };
    const auto slotBudget = truffle::rhi::bind_group_descriptor_budget(
        gappedBudgetLayout, capabilities);
    TRUFFLE_CHECK(slotBudget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(slotBudget.totalUnits == 4);
    TRUFFLE_CHECK(slotBudget.bufferUnits == 3);
    TRUFFLE_CHECK(slotBudget.textureUnits == 1);
    TRUFFLE_CHECK(slotBudget.samplerUnits == 0);
    const auto descriptorCountBudget =
        truffle::rhi::bind_group_descriptor_budget(
            gappedBudgetLayout, groupedDescriptorCaps);
    TRUFFLE_CHECK(descriptorCountBudget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::descriptor_count);
    TRUFFLE_CHECK(descriptorCountBudget.totalUnits == 3);
    TRUFFLE_CHECK(descriptorCountBudget.bufferUnits == 2);
    TRUFFLE_CHECK(descriptorCountBudget.textureUnits == 1);
    TRUFFLE_CHECK(descriptorCountBudget.samplerUnits == 0);
    auto bindGroupBudgetCaps = capabilities;
    bindGroupBudgetCaps.descriptorPolicy.budgetModel =
        truffle::rhi::NativeDescriptorBudgetModel::bind_group_count;
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_uses_bind_group_budgets(
            bindGroupBudgetCaps));
    const auto bindGroupBudget = truffle::rhi::bind_group_descriptor_budget(
        gappedBudgetLayout, bindGroupBudgetCaps);
    TRUFFLE_CHECK(bindGroupBudget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::bind_group_count);
    TRUFFLE_CHECK(bindGroupBudget.totalUnits == 1);
    TRUFFLE_CHECK(bindGroupBudget.bufferUnits == 0);
    TRUFFLE_CHECK(bindGroupBudget.textureUnits == 0);
    TRUFFLE_CHECK(bindGroupBudget.samplerUnits == 0);
    const auto scaledBindGroupBudget =
        truffle::rhi::scale_bind_group_descriptor_budget(bindGroupBudget, 7);
    TRUFFLE_CHECK(scaledBindGroupBudget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::bind_group_count);
    TRUFFLE_CHECK(scaledBindGroupBudget.totalUnits == 7);
    TRUFFLE_CHECK(scaledBindGroupBudget.bufferUnits == 0);
    TRUFFLE_CHECK(scaledBindGroupBudget.textureUnits == 0);
    TRUFFLE_CHECK(scaledBindGroupBudget.samplerUnits == 0);
    const auto saturatedBudget =
        truffle::rhi::scale_bind_group_descriptor_budget(slotBudget,
                                                         std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudget.totalUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudget.bufferUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudget.textureUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudget.samplerUnits == 0);
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 3,
            .dynamicIndexing = true,
            .bindless = true,
        }},
    }, bindlessCaps));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .arrayCount = 4,
            .dynamicIndexing = true,
            .bindless = true,
        }},
    }, bindlessCaps));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .rasterState = {
            .fillMode = truffle::rhi::FillMode::wireframe,
            .cullMode = truffle::rhi::CullMode::front,
            .frontFace = truffle::rhi::FrontFace::clockwise,
            .depthClip = false,
        },
        .depthStencilState = {
            .depthCompare = truffle::rhi::SamplerCompareOp::greater_equal,
        },
        .colorBlend = {
            .enabled = true,
            .srcColor = truffle::rhi::BlendFactor::source_alpha,
            .dstColor = truffle::rhi::BlendFactor::one_minus_source_alpha,
            .srcAlpha = truffle::rhi::BlendFactor::one,
            .dstAlpha = truffle::rhi::BlendFactor::one_minus_source_alpha,
            .writeMask = truffle::rhi::ColorWriteFlags::red |
                         truffle::rhi::ColorWriteFlags::green |
                         truffle::rhi::ColorWriteFlags::blue,
        },
        .vertexBuffers = {{
            .binding = 0,
            .stride = 32,
            .stepMode = truffle::rhi::VertexStepMode::instance,
        }},
        .vertexAttributes = {{
            .location = 0,
            .binding = 0,
            .format = truffle::rhi::VertexFormat::float32x4,
            .offset = 0,
        }},
    }, capabilities));
    const truffle::rhi::PipelineDesc depthPipelineDesc{
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthTest = true,
        .depthWrite = true,
        .depthStencilState = {
            .depthCompare = truffle::rhi::SamplerCompareOp::greater_equal,
        },
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_state_valid(
        depthPipelineDesc, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_pass_compatible(
        depthPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float}));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_pass_compatible(
        depthPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::nullopt));
    const truffle::rhi::PipelineDesc noDepthPipelineDesc{
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_pass_compatible(
        noDepthPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::nullopt));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_pass_compatible(
        noDepthPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float}));
    const truffle::rhi::PipelineDesc colorlessDepthPipelineDesc{
        .colorFormat = truffle::rhi::TextureFormat::unknown,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthTest = true,
        .depthWrite = true,
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_state_valid(
        colorlessDepthPipelineDesc, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_pass_compatible(
        colorlessDepthPipelineDesc,
        std::nullopt,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float}));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_pass_compatible(
        depthPipelineDesc,
        std::nullopt,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float}));
    const truffle::rhi::PipelineDesc depthStencilPipelineDesc{
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .depthTest = true,
        .depthWrite = true,
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_state_valid(
        depthStencilPipelineDesc, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_pass_compatible(
        depthStencilPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float_stencil8}));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_pass_compatible(
        depthStencilPipelineDesc,
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::rgba8_unorm},
        std::optional<truffle::rhi::TextureFormat>{
            truffle::rhi::TextureFormat::depth32_float}));
    const truffle::rhi::PipelineDesc stencilPipelineDesc{
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .depthTest = true,
        .depthWrite = true,
        .depthStencilState = {
            .depthCompare = truffle::rhi::SamplerCompareOp::greater_equal,
            .stencilTest = true,
            .frontFaceStencil = {
                .compareOp = truffle::rhi::SamplerCompareOp::equal,
                .failOp = truffle::rhi::StencilOp::keep,
                .depthFailOp = truffle::rhi::StencilOp::increment_clamp,
                .passOp = truffle::rhi::StencilOp::replace,
                .readMask = 0x0fu,
                .writeMask = 0xf0u,
            },
            .backFaceStencil = {
                .compareOp = truffle::rhi::SamplerCompareOp::not_equal,
                .failOp = truffle::rhi::StencilOp::zero,
                .depthFailOp = truffle::rhi::StencilOp::decrement_wrap,
                .passOp = truffle::rhi::StencilOp::invert,
                .readMask = 0xffu,
                .writeMask = 0x3fu,
            },
        },
    };
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_render_state_valid(
        stencilPipelineDesc, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::depth32_float,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = true,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::rgba8_unorm,
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .rasterState = {
            .fillMode = static_cast<truffle::rhi::FillMode>(99),
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .depthStencilState = {
            .stencilTest = true,
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthStencilState = {
            .stencilTest = true,
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .depthStencilState = {
            .stencilTest = true,
            .frontFaceStencil = {
                .passOp = static_cast<truffle::rhi::StencilOp>(99),
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .colorBlend = {
            .writeMask = static_cast<truffle::rhi::ColorWriteFlags>(1u << 8u),
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .vertexBuffers = {{
            .binding = 0,
            .stride = 0,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .vertexBuffers = {{
            .binding = 0,
            .stride = 8,
        }},
        .vertexAttributes = {{
            .location = 0,
            .binding = 0,
            .format = truffle::rhi::VertexFormat::float32x4,
            .offset = 0,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .vertexAttributes = {{
            .location = 0,
            .binding = 0,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .vertexBuffers = {{
            .binding = 0,
            .stride = 16,
        }},
        .vertexAttributes = {
            {
                .location = 0,
                .binding = 0,
            },
            {
                .location = 0,
                .binding = 0,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_state_valid({
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .vertexBuffers = {{
            .binding = 0,
            .stride = capabilities.limits.maxVertexBufferStride + 1,
        }},
    }, capabilities));

    const truffle::rhi::BindGroupLayoutDesc bindGroupLayoutDesc{
        .debugName = "core_bind_group_layout",
        .cacheKey = 0xB100u,
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
            },
        },
    };
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_layout_valid(
        bindGroupLayoutDesc, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .visibility = truffle::rhi::ShaderStageFlags::none,
        }},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_layout_valid({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
            },
        },
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_layout_valid({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampler,
            .visibility = truffle::rhi::ShaderStageFlags::fragment,
            .dynamicOffset = true,
        }},
    }, capabilities));

    TestBuffer uniformBuffer{{
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
    }};
    TestBuffer secondUniformBuffer{{
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
    }};
    TestBuffer smallUniformBuffer{{
        .size = 8,
        .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
    }};
    TestTexture sampledTexture{{
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::sampled,
    }};
    TestTexture secondSampledTexture{{
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::sampled,
    }};
    TestTexture colorOnlyTexture{{
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::color_attachment,
    }};
    TestTexture depthOnlyAttachment{{
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::depth32_float,
        .usageFlags = truffle::rhi::TextureUsageFlags::depth_stencil,
    }};
    TestTexture depthStencilAttachment{{
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .usageFlags = truffle::rhi::TextureUsageFlags::depth_stencil,
    }};
    TestSampler sampler;
    TestSampler secondSampler;
    TestBindGroupLayout bindGroupLayout{bindGroupLayoutDesc};
    TRUFFLE_CHECK(bindGroupLayout.cache_key() == 0xB100u);
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_allocation_policy_valid(
        truffle::rhi::BindGroupAllocationPolicy::persistent));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_allocation_policy_valid(
        truffle::rhi::BindGroupAllocationPolicy::transient_frame));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_allocation_policy_valid(
        static_cast<truffle::rhi::BindGroupAllocationPolicy>(99)));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_reuse_hint_valid(
        truffle::rhi::BindGroupReuseHint::stable));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_reuse_hint_valid(
        truffle::rhi::BindGroupReuseHint::update_in_place));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_reuse_hint_valid(
        truffle::rhi::BindGroupReuseHint::rebuild));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_reuse_hint_valid(
        static_cast<truffle::rhi::BindGroupReuseHint>(99)));

    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid({
        .cacheKey = 0xB101u,
        .layout = &bindGroupLayout,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid({
        .layout = &bindGroupLayout,
        .allocationPolicy =
            truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        .reuseHint = truffle::rhi::BindGroupReuseHint::rebuild,
        .allocationFrameIndex = 1,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    }, capabilities));
    const truffle::rhi::BindGroupDesc updateHintBindGroupDesc{
        .cacheKey = 0xB102u,
        .layout = &bindGroupLayout,
        .reuseHint = truffle::rhi::BindGroupReuseHint::update_in_place,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    };
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid(
        updateHintBindGroupDesc));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_prefers_descriptor_cache(updateHintBindGroupDesc));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_prefers_descriptor_rewrite(
            updateHintBindGroupDesc));
    TRUFFLE_CHECK(
        !truffle::rhi::bind_group_prefers_arena_recycling(
            updateHintBindGroupDesc));
    const auto updateHintStrategy = truffle::rhi::bind_group_descriptor_strategy(
        updateHintBindGroupDesc, capabilities);
    TRUFFLE_CHECK(updateHintStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::persistent);
    TRUFFLE_CHECK(updateHintStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!updateHintStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(updateHintStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(!updateHintStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(updateHintStrategy.frameSlotCount == 1);
    TRUFFLE_CHECK(updateHintStrategy.recycleFrameLag == 0);
    TRUFFLE_CHECK(updateHintStrategy.budget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(updateHintStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(updateHintStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::manual);
    TRUFFLE_CHECK(updateHintStrategy.mappingModel ==
                  capabilities.descriptorPolicy.mappingModel);
    TRUFFLE_CHECK(updateHintStrategy.allocationModel ==
                  capabilities.descriptorPolicy.allocationModel);
    TRUFFLE_CHECK(updateHintStrategy.updateModel ==
                  capabilities.descriptorPolicy.updateModel);
    TRUFFLE_CHECK(updateHintStrategy.flattenedNativeBindings ==
                  capabilities.descriptorPolicy.flattenedNativeBindings);
    TRUFFLE_CHECK(!updateHintStrategy.rebuildAllocationOnUpdate);
    const auto updateHintArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(updateHintStrategy, 4);
    TRUFFLE_CHECK(updateHintArenaPlan.bindGroupCount == 4);
    TRUFFLE_CHECK(updateHintArenaPlan.reservationMultiplier == 1);
    TRUFFLE_CHECK(updateHintArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!updateHintArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(updateHintArenaPlan.cacheEntryCount == 4);
    TRUFFLE_CHECK(updateHintArenaPlan.reservationEntryCount == 4);
    TRUFFLE_CHECK(updateHintArenaPlan.budgetPerEntry.totalUnits == 3);
    TRUFFLE_CHECK(updateHintArenaPlan.cacheBudget.totalUnits == 12);
    TRUFFLE_CHECK(updateHintArenaPlan.reservationBudget.totalUnits == 12);
    const auto frameCachedStrategy = truffle::rhi::bind_group_descriptor_strategy(
        {
            .cacheKey = 0xB103u,
            .layout = &bindGroupLayout,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
            .allocationFrameIndex = 1,
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .buffer = {.buffer = &uniformBuffer, .size = 16},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .texture = &sampledTexture,
                },
                {
                    .bindingIndex = 2,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .sampler = &sampler,
                },
            },
        },
        capabilities);
    TRUFFLE_CHECK(frameCachedStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::per_frame);
    TRUFFLE_CHECK(!frameCachedStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!frameCachedStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(frameCachedStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(frameCachedStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(frameCachedStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(frameCachedStrategy.frameSlotCount ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameCachedStrategy.recycleFrameLag ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameCachedStrategy.budget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(frameCachedStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(frameCachedStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::frame_retire);
    const auto frameArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(frameCachedStrategy, 2);
    TRUFFLE_CHECK(frameArenaPlan.bindGroupCount == 2);
    TRUFFLE_CHECK(frameArenaPlan.reservationMultiplier ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(frameArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(frameArenaPlan.cacheEntryCount ==
                  2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.reservationEntryCount ==
                  2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.cacheBudget.totalUnits ==
                  6 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.reservationBudget.totalUnits ==
                  6 * capabilities.maxFramesInFlight);
    const auto rebuildStrategy = truffle::rhi::bind_group_descriptor_strategy(
        {
            .cacheKey = 0xB104u,
            .layout = &bindGroupLayout,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
            .reuseHint = truffle::rhi::BindGroupReuseHint::rebuild,
            .allocationFrameIndex = 1,
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .buffer = {.buffer = &uniformBuffer, .size = 16},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .texture = &sampledTexture,
                },
                {
                    .bindingIndex = 2,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .sampler = &sampler,
                },
            },
        },
        capabilities);
    TRUFFLE_CHECK(rebuildStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::none);
    TRUFFLE_CHECK(!rebuildStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!rebuildStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(rebuildStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(!rebuildStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(rebuildStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(rebuildStrategy.frameSlotCount ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildStrategy.recycleFrameLag ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildStrategy.budget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(rebuildStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(rebuildStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::immediate);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_lifetime_class(
                      rebuildStrategy) ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::immediate);
    const auto rebuildArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(rebuildStrategy, 5);
    TRUFFLE_CHECK(rebuildArenaPlan.bindGroupCount == 5);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationMultiplier ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(!rebuildArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!rebuildArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(rebuildArenaPlan.cacheEntryCount == 0);
    TRUFFLE_CHECK(rebuildArenaPlan.cacheBudget.totalUnits == 0);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationEntryCount ==
                  5 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationBudget.totalUnits ==
                  15 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_arena_pool_class(
                      rebuildStrategy) ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      uncached_reservation);
    truffle::rhi::SharedPipelineLayoutDescriptorArenaSummary rebuildFamilySummary;
    rebuildFamilySummary.layoutCount = 1;
    rebuildFamilySummary.requestCount = 1;
    rebuildFamilySummary.plannedGroupCount = 1;
    rebuildFamilySummary.familyCount = 1;
    rebuildFamilySummary.families.push_back({
        .layout = {},
        .strategy = rebuildStrategy,
        .arenaPlan = rebuildArenaPlan,
        .requestCount = 1,
    });
    const auto rebuildPartitionSummary =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_partition_summary(
            rebuildFamilySummary);
    TRUFFLE_CHECK(rebuildPartitionSummary.partitionCount == 1);
    TRUFFLE_CHECK(rebuildPartitionSummary.uncachedReservationPartitionCount == 1);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencyCount == 1);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyScopedLiveObjectCount == 0);
    TRUFFLE_CHECK(rebuildPartitionSummary.partitionScopedLiveObjectCount == 1);
    TRUFFLE_CHECK(rebuildPartitionSummary.partitions[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      uncached_reservation);
    TRUFFLE_CHECK(rebuildPartitionSummary.partitions[0].entryCount ==
                  rebuildArenaPlan.reservationEntryCount);
    TRUFFLE_CHECK(rebuildPartitionSummary.partitions[0].totalBudget.totalUnits ==
                  rebuildArenaPlan.reservationBudget.totalUnits);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies.size() == 1);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].familyIndex == 0);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].partitionIndex == 0);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      uncached_reservation);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::immediate);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::partition);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0]
                      .sharesPartitionCapacity);
    TRUFFLE_CHECK(!rebuildPartitionSummary.familyResidencies[0]
                       .usesDescriptorCache);
    TRUFFLE_CHECK(!rebuildPartitionSummary.familyResidencies[0]
                       .partitionsCachePerFrame);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].requiresFrameIndex);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].frameSlotCount ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].recycleFrameLag ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0]
                      .reservationMultiplier ==
                  rebuildArenaPlan.reservationMultiplier);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].bindGroupCount == 5);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].entryCount ==
                  rebuildArenaPlan.reservationEntryCount);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0].evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::immediate);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0]
                      .budgetPerEntry.totalUnits == 3);
    TRUFFLE_CHECK(rebuildPartitionSummary.familyResidencies[0]
                      .totalBudget.totalUnits ==
                  rebuildArenaPlan.reservationBudget.totalUnits);
    TRUFFLE_CHECK(!rebuildPartitionSummary.familyResidencies[0]
                       .partitionHasMixedCacheKeyUsability);
    TRUFFLE_CHECK(!rebuildPartitionSummary.familyResidencies[0]
                       .partitionHasMixedUpdateBehavior);
    TRUFFLE_CHECK(!rebuildPartitionSummary.familyResidencies[0]
                       .partitionHasMixedNativeUpdateModels);
    const auto rebuildCohortSummary =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_cohort_summary(
            rebuildPartitionSummary);
    TRUFFLE_CHECK(rebuildCohortSummary.partitionCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.familyResidencyCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.cohortCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.liveObjectCohortCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.capacityOnlyCohortCount == 0);
    TRUFFLE_CHECK(rebuildCohortSummary.mixedCacheKeyCohortCount == 0);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts.size() == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      live_objects);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      uncached_reservation);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::immediate);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].familyCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].requestCount == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].bindGroupCount == 5);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].entryCount ==
                  rebuildArenaPlan.reservationEntryCount);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].familyIndices.size() == 1);
    TRUFFLE_CHECK(rebuildCohortSummary.cohorts[0].familyIndices[0] == 0);
    auto groupedStrategyCaps = capabilities;
    groupedStrategyCaps.descriptorPolicy = {
        .mappingModel =
            truffle::rhi::NativeDescriptorMappingModel::descriptor_sets,
        .allocationModel =
            truffle::rhi::NativeDescriptorAllocationModel::bind_group_owned,
        .updateModel =
            truffle::rhi::NativeDescriptorUpdateModel::copy_into_allocation,
        .budgetModel =
            truffle::rhi::NativeDescriptorBudgetModel::descriptor_count,
        .flattenedNativeBindings = false,
    };
    const auto groupedStrategy = truffle::rhi::bind_group_descriptor_strategy(
        updateHintBindGroupDesc, groupedStrategyCaps);
    TRUFFLE_CHECK(groupedStrategy.mappingModel ==
                  truffle::rhi::NativeDescriptorMappingModel::descriptor_sets);
    TRUFFLE_CHECK(groupedStrategy.allocationModel ==
                  truffle::rhi::NativeDescriptorAllocationModel::bind_group_owned);
    TRUFFLE_CHECK(groupedStrategy.updateModel ==
                  truffle::rhi::NativeDescriptorUpdateModel::copy_into_allocation);
    TRUFFLE_CHECK(groupedStrategy.budget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::descriptor_count);
    TRUFFLE_CHECK(groupedStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(!groupedStrategy.flattenedNativeBindings);
    TRUFFLE_CHECK(groupedStrategy.rewriteDescriptors);
    const auto groupedArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(groupedStrategy, 3);
    const truffle::rhi::BindGroupDescriptorArenaPlan mixedAggregatePlans[] = {
        updateHintArenaPlan,
        groupedArenaPlan,
    };
    const auto mixedAggregateTotals =
        truffle::rhi::bind_group_descriptor_arena_totals(mixedAggregatePlans);
    TRUFFLE_CHECK(mixedAggregateTotals.planCount == 2);
    TRUFFLE_CHECK(mixedAggregateTotals.bindGroupCount == 7);
    TRUFFLE_CHECK(mixedAggregateTotals.cachedBindGroupCount == 7);
    TRUFFLE_CHECK(mixedAggregateTotals.uncachedBindGroupCount == 0);
    TRUFFLE_CHECK(mixedAggregateTotals.usesDescriptorCache);
    TRUFFLE_CHECK(!mixedAggregateTotals.partitionsCachePerFrame);
    TRUFFLE_CHECK(mixedAggregateTotals.mixedBudgetModels);
    TRUFFLE_CHECK(mixedAggregateTotals.budgetModel ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(mixedAggregateTotals.cacheBudget.totalUnits == 21);
    auto rebuildUpdateCaps = capabilities;
    rebuildUpdateCaps.descriptorPolicy.updateModel =
        truffle::rhi::NativeDescriptorUpdateModel::rebuild_allocation;
    TRUFFLE_CHECK(
        truffle::rhi::descriptor_policy_rebuilds_allocations_for_updates(
            rebuildUpdateCaps));
    const auto rebuildUpdateStrategy =
        truffle::rhi::bind_group_descriptor_strategy(
            updateHintBindGroupDesc, rebuildUpdateCaps);
    TRUFFLE_CHECK(rebuildUpdateStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::persistent);
    TRUFFLE_CHECK(!rebuildUpdateStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(rebuildUpdateStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(rebuildUpdateStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(!rebuildUpdateStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(rebuildUpdateStrategy.frameSlotCount == 1);
    TRUFFLE_CHECK(rebuildUpdateStrategy.recycleFrameLag == 0);
    TRUFFLE_CHECK(rebuildUpdateStrategy.budget.model ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(rebuildUpdateStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(rebuildUpdateStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::manual);
    TRUFFLE_CHECK(rebuildUpdateStrategy.updateModel ==
                  truffle::rhi::NativeDescriptorUpdateModel::rebuild_allocation);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_lifetime_class(
                      rebuildUpdateStrategy) ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    const auto rebuildUpdateArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(
            updateHintBindGroupDesc, rebuildUpdateCaps, 3);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.bindGroupCount == 3);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.reservationMultiplier == 1);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!rebuildUpdateArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.cacheEntryCount == 3);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.reservationEntryCount == 3);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.cacheBudget.totalUnits == 9);
    TRUFFLE_CHECK(rebuildUpdateArenaPlan.reservationBudget.totalUnits == 9);
    TRUFFLE_CHECK(
        !truffle::rhi::bind_group_descriptor_strategy_partition_compatible(
            updateHintStrategy, rebuildUpdateStrategy));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_strategy_partition_reusable(
            updateHintStrategy, rebuildUpdateStrategy));
    truffle::rhi::SharedPipelineLayoutDescriptorArenaSummary updateReuseFamilySummary;
    updateReuseFamilySummary.layoutCount = 2;
    updateReuseFamilySummary.requestCount = 2;
    updateReuseFamilySummary.plannedGroupCount = 2;
    updateReuseFamilySummary.familyCount = 2;
    updateReuseFamilySummary.families.push_back({
        .layout = {},
        .strategy = updateHintStrategy,
        .arenaPlan = updateHintArenaPlan,
        .requestCount = 1,
    });
    updateReuseFamilySummary.families.push_back({
        .layout = {},
        .strategy = rebuildUpdateStrategy,
        .arenaPlan = rebuildUpdateArenaPlan,
        .requestCount = 1,
    });
    const auto updateReusePartitionSummary =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_partition_summary(
            updateReuseFamilySummary);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.persistentCachePartitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.perFrameCachePartitionCount == 0);
    TRUFFLE_CHECK(updateReusePartitionSummary.uncachedReservationPartitionCount ==
                  0);
    TRUFFLE_CHECK(updateReusePartitionSummary.mixedCacheKeyPartitionCount == 0);
    TRUFFLE_CHECK(updateReusePartitionSummary.mixedUpdatePartitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencyCount == 2);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyScopedLiveObjectCount == 2);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitionScopedLiveObjectCount == 0);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions.size() == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].familyCount == 2);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].requestCount == 2);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].bindGroupCount == 7);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].entryCount == 7);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].cacheKeyUsableFamilyCount == 2);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].rewriteDescriptorFamilyCount ==
        1);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0]
                      .rebuildAllocationOnUpdateFamilyCount == 1);
    TRUFFLE_CHECK(!updateReusePartitionSummary.partitions[0]
                       .mixedCacheKeyUsability);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].mixedUpdateBehavior);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].mixedNativeUpdateModels);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].strategy.cacheKeyUsable);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].strategy.rewriteDescriptors);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0]
                      .strategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].totalBudget.totalUnits == 21);
    TRUFFLE_CHECK(updateReusePartitionSummary.partitions[0].familyIndices.size() ==
                  2);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].familyIndices[0] == 0);
    TRUFFLE_CHECK(
        updateReusePartitionSummary.partitions[0].familyIndices[1] == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies.size() == 2);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0].familyIndex == 0);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0].partitionIndex ==
                  0);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .sharesPartitionCapacity);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .usesDescriptorCache);
    TRUFFLE_CHECK(!updateReusePartitionSummary.familyResidencies[0]
                       .partitionsCachePerFrame);
    TRUFFLE_CHECK(!updateReusePartitionSummary.familyResidencies[0]
                       .requiresFrameIndex);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0].entryCount == 4);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .totalBudget.totalUnits == 12);
    TRUFFLE_CHECK(!updateReusePartitionSummary.familyResidencies[0]
                       .partitionHasMixedCacheKeyUsability);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .partitionHasMixedUpdateBehavior);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[0]
                      .partitionHasMixedNativeUpdateModels);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1].familyIndex == 1);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1].partitionIndex ==
                  0);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1]
                      .liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1]
                      .sharesPartitionCapacity);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1]
                      .usesDescriptorCache);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1].entryCount == 3);
    TRUFFLE_CHECK(updateReusePartitionSummary.familyResidencies[1]
                      .totalBudget.totalUnits == 9);
    const auto updateReuseCohorts =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_cohort_summary(
            updateReusePartitionSummary);
    TRUFFLE_CHECK(updateReuseCohorts.partitionCount == 1);
    TRUFFLE_CHECK(updateReuseCohorts.familyResidencyCount == 2);
    TRUFFLE_CHECK(updateReuseCohorts.cohortCount == 2);
    TRUFFLE_CHECK(updateReuseCohorts.liveObjectCohortCount == 0);
    TRUFFLE_CHECK(updateReuseCohorts.capacityOnlyCohortCount == 2);
    TRUFFLE_CHECK(updateReuseCohorts.mixedCacheKeyCohortCount == 0);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts.size() == 2);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].rewriteDescriptors);
    TRUFFLE_CHECK(!updateReuseCohorts.cohorts[0].rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].familyCount == 1);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].bindGroupCount == 4);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].entryCount == 4);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].familyIndices[0] == 0);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(!updateReuseCohorts.cohorts[1].rewriteDescriptors);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].familyCount == 1);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].bindGroupCount == 3);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].entryCount == 3);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].familyIndices[0] == 1);
    truffle::rhi::SharedPipelineLayoutDescriptorArenaPlan updateReusePlan;
    updateReusePlan.families = updateReuseFamilySummary;
    updateReusePlan.partitions = updateReusePartitionSummary;
    updateReusePlan.cohorts = updateReuseCohorts;
    const auto updateReuseMaterialization =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_materialization_summary(
            updateReusePlan);
    TRUFFLE_CHECK(updateReuseMaterialization.complete);
    TRUFFLE_CHECK(updateReuseMaterialization.partitionCount == 1);
    TRUFFLE_CHECK(updateReuseMaterialization.cohortCount == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.arenaCount == 1);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializationCount == 2);
    TRUFFLE_CHECK(
        updateReuseMaterialization.liveObjectReuseMaterializationCount == 0);
    TRUFFLE_CHECK(
        updateReuseMaterialization.capacityOnlyReuseMaterializationCount == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas.size() == 1);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].partitionIndex == 0);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].familyCount == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].cohortCount == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].requestCount == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].bindGroupCapacity == 7);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].entryCapacity == 7);
    TRUFFLE_CHECK(!updateReuseMaterialization.arenas[0]
                       .supportsPartitionWideLiveObjectReuse);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].mixedUpdateBehavior);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].mixedNativeUpdateModels);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].totalBudget.totalUnits == 21);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].cohortIndices.size() == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations.size() == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0].cohortIndex ==
                  0);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0]
                      .partitionIndex == 0);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0]
                      .rewriteDescriptors);
    TRUFFLE_CHECK(!updateReuseMaterialization.reuseMaterializations[0]
                       .rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(!updateReuseMaterialization.reuseMaterializations[0]
                       .supportsLiveObjectReuse);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0]
                      .bindGroupCapacity == 4);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[1].cohortIndex ==
                  1);
    TRUFFLE_CHECK(!updateReuseMaterialization.reuseMaterializations[1]
                       .rewriteDescriptors);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[1]
                      .rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[1]
                      .bindGroupCapacity == 3);
    const truffle::rhi::BindGroupDescriptorArenaPlan aggregatePlans[] = {
        updateHintArenaPlan,
        frameArenaPlan,
        rebuildArenaPlan,
        rebuildUpdateArenaPlan,
    };
    const auto aggregateTotals =
        truffle::rhi::bind_group_descriptor_arena_totals(aggregatePlans);
    TRUFFLE_CHECK(aggregateTotals.planCount == 4);
    TRUFFLE_CHECK(aggregateTotals.bindGroupCount == 14);
    TRUFFLE_CHECK(aggregateTotals.cachedBindGroupCount == 9);
    TRUFFLE_CHECK(aggregateTotals.uncachedBindGroupCount == 5);
    TRUFFLE_CHECK(aggregateTotals.cacheEntryCount ==
                  7 + 2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.reservationEntryCount ==
                  7 + 7 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.persistentCacheEntryCount == 7);
    TRUFFLE_CHECK(aggregateTotals.perFrameCacheEntryCount ==
                  2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.uncachedReservationEntryCount ==
                  5 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.maxReservationMultiplier ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.usesDescriptorCache);
    TRUFFLE_CHECK(aggregateTotals.partitionsCachePerFrame);
    TRUFFLE_CHECK(!aggregateTotals.mixedBudgetModels);
    TRUFFLE_CHECK(aggregateTotals.budgetModel ==
                  truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
    TRUFFLE_CHECK(aggregateTotals.maxBudgetPerEntry.totalUnits == 3);
    TRUFFLE_CHECK(aggregateTotals.cacheBudget.totalUnits ==
                  21 + 6 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.reservationBudget.totalUnits ==
                  21 + 21 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.persistentCacheBudget.totalUnits == 21);
    TRUFFLE_CHECK(aggregateTotals.perFrameCacheBudget.totalUnits ==
                  6 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.uncachedReservationBudget.totalUnits ==
                  15 * capabilities.maxFramesInFlight);
    const auto groupedLayoutArenaPlan =
        truffle::rhi::pipeline_layout_bind_group_arena_plan(
            groupedPipelineLayout,
            capabilities,
            {
                .groupIndex = 1,
                .bindGroupCount = 2,
                .cacheKey = 0xB105u,
                .allocationPolicy =
                    truffle::rhi::BindGroupAllocationPolicy::transient_frame,
            });
    TRUFFLE_CHECK(groupedLayoutArenaPlan.has_value());
    TRUFFLE_CHECK(groupedLayoutArenaPlan->groupIndex == 1);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->layout.bindings.size() == 1);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->strategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::per_frame);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->strategy.budget.totalUnits == 2);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->arenaPlan.bindGroupCount == 2);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->arenaPlan.reservationMultiplier ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->arenaPlan.cacheEntryCount ==
                  2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutArenaPlan->arenaPlan.cacheBudget.totalUnits ==
                  4 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(!truffle::rhi::pipeline_layout_bind_group_arena_plan(
        groupedPipelineLayout,
        capabilities,
        {
            .groupIndex = 3,
            .bindGroupCount = 1,
        }).has_value());
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest groupedLayoutRequests[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 4,
            .cacheKey = 0xB106u,
        },
        {
            .groupIndex = 1,
            .bindGroupCount = 2,
            .cacheKey = 0xB107u,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
        {
            .groupIndex = 3,
            .bindGroupCount = 1,
        },
    };
    const auto groupedLayoutSummary =
        truffle::rhi::pipeline_layout_descriptor_arena_summary(
            groupedPipelineLayout, capabilities, groupedLayoutRequests);
    TRUFFLE_CHECK(groupedLayoutSummary.requestCount == 3);
    TRUFFLE_CHECK(groupedLayoutSummary.plannedGroupCount == 2);
    TRUFFLE_CHECK(groupedLayoutSummary.missingGroupCount == 1);
    TRUFFLE_CHECK(!groupedLayoutSummary.complete);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.planCount == 2);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.bindGroupCount == 6);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.cachedBindGroupCount == 6);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.uncachedBindGroupCount == 0);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.cacheEntryCount ==
                  4 + 2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.reservationEntryCount ==
                  4 + 2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.persistentCacheEntryCount == 4);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.perFrameCacheEntryCount ==
                  2 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.usesDescriptorCache);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.partitionsCachePerFrame);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.maxBudgetPerEntry.totalUnits == 2);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.cacheBudget.totalUnits ==
                  4 + 4 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.reservationBudget.totalUnits ==
                  4 + 4 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.persistentCacheBudget.totalUnits ==
                  4);
    TRUFFLE_CHECK(groupedLayoutSummary.totals.perFrameCacheBudget.totalUnits ==
                  4 * capabilities.maxFramesInFlight);
    const truffle::rhi::PipelineLayoutDesc sharedPoolLayoutA{
        .debugName = "shared_pool_layout_a",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 64,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .groupIndex = 1,
                .arrayCount = 2,
            },
        },
    };
    const truffle::rhi::PipelineLayoutDesc sharedPoolLayoutB{
        .debugName = "shared_pool_layout_b",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 64,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .groupIndex = 2,
                .arrayCount = 2,
            },
        },
    };
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolRequestsA[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 2,
            .cacheKey = 0xB108u,
        },
        {
            .groupIndex = 1,
            .bindGroupCount = 1,
            .cacheKey = 0xB109u,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
    };
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolRequestsB[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 3,
            .cacheKey = 0xB10Au,
        },
        {
            .groupIndex = 2,
            .bindGroupCount = 4,
            .cacheKey = 0xB10Bu,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
    };
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolRequestsC[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 5,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
        {
            .groupIndex = 3,
            .bindGroupCount = 1,
        },
    };
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolRequestsMissingLayout[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 1,
        },
    };
    const truffle::rhi::PipelineLayoutDescriptorArenaBatchRequest sharedPoolBatches[] = {
        {
            .layout = &sharedPoolLayoutA,
            .requests = sharedPoolRequestsA,
        },
        {
            .layout = &sharedPoolLayoutB,
            .requests = sharedPoolRequestsB,
        },
        {
            .layout = &sharedPoolLayoutA,
            .requests = sharedPoolRequestsC,
        },
        {
            .requests = sharedPoolRequestsMissingLayout,
        },
    };
    const auto sharedPoolSummary =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_summary(
            capabilities, sharedPoolBatches);
    TRUFFLE_CHECK(sharedPoolSummary.layoutCount == 4);
    TRUFFLE_CHECK(sharedPoolSummary.requestCount == 7);
    TRUFFLE_CHECK(sharedPoolSummary.plannedGroupCount == 5);
    TRUFFLE_CHECK(sharedPoolSummary.missingLayoutCount == 1);
    TRUFFLE_CHECK(sharedPoolSummary.missingGroupCount == 1);
    TRUFFLE_CHECK(sharedPoolSummary.familyCount == 3);
    TRUFFLE_CHECK(sharedPoolSummary.mergedGroupCount == 2);
    TRUFFLE_CHECK(sharedPoolSummary.strategySplitGroupCount == 1);
    TRUFFLE_CHECK(!sharedPoolSummary.complete);
    TRUFFLE_CHECK(sharedPoolSummary.families.size() == 3);
    TRUFFLE_CHECK(sharedPoolSummary.totals.planCount == 3);
    TRUFFLE_CHECK(sharedPoolSummary.totals.bindGroupCount == 15);
    TRUFFLE_CHECK(sharedPoolSummary.totals.cachedBindGroupCount == 15);
    TRUFFLE_CHECK(sharedPoolSummary.totals.uncachedBindGroupCount == 0);
    TRUFFLE_CHECK(sharedPoolSummary.totals.cacheEntryCount ==
                  5 + 10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.reservationEntryCount ==
                  5 + 10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.persistentCacheEntryCount == 5);
    TRUFFLE_CHECK(sharedPoolSummary.totals.perFrameCacheEntryCount ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.usesDescriptorCache);
    TRUFFLE_CHECK(sharedPoolSummary.totals.partitionsCachePerFrame);
    TRUFFLE_CHECK(sharedPoolSummary.totals.maxBudgetPerEntry.totalUnits == 2);
    TRUFFLE_CHECK(sharedPoolSummary.totals.cacheBudget.totalUnits ==
                  5 + 15 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.reservationBudget.totalUnits ==
                  5 + 15 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.persistentCacheBudget.totalUnits == 5);
    TRUFFLE_CHECK(sharedPoolSummary.totals.perFrameCacheBudget.totalUnits ==
                  15 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_family_shareable(
        sharedPoolSummary.families[0].layout,
        sharedPoolSummary.families[0].strategy,
        sharedPoolSummary.families[0].layout,
        sharedPoolSummary.families[0].strategy));
    TRUFFLE_CHECK(!truffle::rhi::bind_group_descriptor_family_shareable(
        sharedPoolSummary.families[0].layout,
        sharedPoolSummary.families[0].strategy,
        sharedPoolSummary.families[2].layout,
        sharedPoolSummary.families[2].strategy));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_strategy_partition_compatible(
            sharedPoolSummary.families[1].strategy,
            sharedPoolSummary.families[2].strategy));
    TRUFFLE_CHECK(
        !truffle::rhi::bind_group_descriptor_strategy_partition_compatible(
            sharedPoolSummary.families[0].strategy,
            sharedPoolSummary.families[2].strategy));
    const auto sharedPoolPartitions =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_partition_summary(
            sharedPoolSummary);
    TRUFFLE_CHECK(sharedPoolPartitions.layoutCount == 4);
    TRUFFLE_CHECK(sharedPoolPartitions.requestCount == 7);
    TRUFFLE_CHECK(sharedPoolPartitions.plannedGroupCount == 5);
    TRUFFLE_CHECK(sharedPoolPartitions.missingLayoutCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.missingGroupCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.familyCount == 3);
    TRUFFLE_CHECK(sharedPoolPartitions.partitionCount == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.persistentCachePartitionCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.perFrameCachePartitionCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.uncachedReservationPartitionCount == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.mixedCacheKeyPartitionCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.mixedUpdatePartitionCount == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencyCount == 3);
    TRUFFLE_CHECK(sharedPoolPartitions.familyScopedLiveObjectCount == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.partitionScopedLiveObjectCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions.size() == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].familyCount == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].requestCount == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].bindGroupCount == 5);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].entryCount == 5);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].reservationMultiplier == 1);
    TRUFFLE_CHECK(
        sharedPoolPartitions.partitions[0].cacheKeyUsableFamilyCount == 1);
    TRUFFLE_CHECK(
        sharedPoolPartitions.partitions[0].rewriteDescriptorFamilyCount == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0]
                      .rebuildAllocationOnUpdateFamilyCount == 0);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[0].mixedCacheKeyUsability);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[0].mixedUpdateBehavior);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[0].mixedNativeUpdateModels);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].strategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::persistent);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].strategy.cacheKeyUsable);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].totalBudget.totalUnits == 5);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].familyIndices.size() == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[0].familyIndices[0] == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].familyCount == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].requestCount == 3);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].bindGroupCount == 10);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].entryCount ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].reservationMultiplier ==
                  capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(
        sharedPoolPartitions.partitions[1].cacheKeyUsableFamilyCount == 1);
    TRUFFLE_CHECK(
        sharedPoolPartitions.partitions[1].rewriteDescriptorFamilyCount == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1]
                      .rebuildAllocationOnUpdateFamilyCount == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].mixedCacheKeyUsability);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[1].mixedUpdateBehavior);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[1].mixedNativeUpdateModels);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].strategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::per_frame);
    TRUFFLE_CHECK(!sharedPoolPartitions.partitions[1].strategy.cacheKeyUsable);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].totalBudget.totalUnits ==
                  15 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].familyIndices.size() == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].familyIndices[0] == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].familyIndices[1] == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies.size() == 3);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[0].familyIndex == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[0].partitionIndex == 0);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[0].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[0].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::partition);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].familyIndex == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].partitionIndex == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      frame_retired);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].requiresFrameIndex);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1].entryCount ==
                  5 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[1]
                      .partitionHasMixedCacheKeyUsability);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].familyIndex == 2);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].partitionIndex == 1);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      frame_retired);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].requiresFrameIndex);
    TRUFFLE_CHECK(sharedPoolPartitions.familyResidencies[2].entryCount ==
                  5 * capabilities.maxFramesInFlight);
    const auto sharedPoolCohorts =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_cohort_summary(
            sharedPoolPartitions);
    TRUFFLE_CHECK(sharedPoolCohorts.partitionCount == 2);
    TRUFFLE_CHECK(sharedPoolCohorts.familyResidencyCount == 3);
    TRUFFLE_CHECK(sharedPoolCohorts.cohortCount == 2);
    TRUFFLE_CHECK(sharedPoolCohorts.liveObjectCohortCount == 1);
    TRUFFLE_CHECK(sharedPoolCohorts.capacityOnlyCohortCount == 1);
    TRUFFLE_CHECK(sharedPoolCohorts.mixedCacheKeyCohortCount == 1);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts.size() == 2);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      live_objects);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].familyCount == 1);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].requestCount == 2);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].bindGroupCount == 5);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[0].entryCount == 5);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].familyCount == 2);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].requestCount == 3);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].bindGroupCount == 10);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].entryCount ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].mixedCacheKeyUsability);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].familyIndices.size() == 2);
    const auto sharedPoolPlan =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_plan(
            capabilities, sharedPoolBatches);
    TRUFFLE_CHECK(sharedPoolPlan.families.familyCount == 3);
    TRUFFLE_CHECK(sharedPoolPlan.families.mergedGroupCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.families.totals.bindGroupCount == 15);
    TRUFFLE_CHECK(sharedPoolPlan.partitions.partitionCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.partitions.familyResidencyCount == 3);
    TRUFFLE_CHECK(sharedPoolPlan.cohorts.cohortCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.cohorts.liveObjectCohortCount == 1);
    TRUFFLE_CHECK(sharedPoolPlan.cohorts.capacityOnlyCohortCount == 1);
    TRUFFLE_CHECK(!sharedPoolPlan.materialization.complete);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.partitionCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.cohortCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenaCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializationCount == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization
                      .liveObjectReuseMaterializationCount == 1);
    TRUFFLE_CHECK(sharedPoolPlan.materialization
                      .capacityOnlyReuseMaterializationCount == 1);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas.size() == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].partitionIndex == 0);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].cohortCount == 1);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0]
                      .supportsPartitionWideLiveObjectReuse);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].bindGroupCapacity == 5);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].entryCapacity == 5);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].partitionIndex == 1);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].cohortCount == 1);
    TRUFFLE_CHECK(!sharedPoolPlan.materialization.arenas[1]
                       .supportsPartitionWideLiveObjectReuse);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1]
                      .mixedCacheKeyUsability);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].bindGroupCapacity ==
                  10);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].entryCapacity ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations.size() == 2);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[0].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      live_objects);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[0]
                      .supportsLiveObjectReuse);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[0]
                      .bindGroupCapacity == 5);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[1].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(!sharedPoolPlan.materialization.reuseMaterializations[1]
                       .supportsLiveObjectReuse);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[1]
                      .mixedCacheKeyUsability);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.reuseMaterializations[1]
                      .bindGroupCapacity == 10);
    truffle::rhi::RetainedBindGroupDescriptorArena runtimeArena{
        sharedPoolPlan.materialization.arenas[1]};
    TRUFFLE_CHECK(runtimeArena.partition_index() == 1);
    TRUFFLE_CHECK(runtimeArena.pool_class() ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      per_frame_cache);
    TRUFFLE_CHECK(runtimeArena.lifetime_class() ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      frame_retired);
    TRUFFLE_CHECK(runtimeArena.bind_group_capacity() == 10);
    TRUFFLE_CHECK(runtimeArena.entry_capacity() ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(runtimeArena.slot_count() == capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(runtimeArena.bind_group_capacity_per_slot() == 10);
    TRUFFLE_CHECK(runtimeArena.entry_capacity_per_slot() == 10);
    TRUFFLE_CHECK(!runtimeArena.supports_live_object_reuse());
    const auto initialArenaUsage = runtimeArena.usage();
    TRUFFLE_CHECK(initialArenaUsage.reservationCount == 0);
    TRUFFLE_CHECK(initialArenaUsage.availableEntryCount ==
                  10 * capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(initialArenaUsage.slots.size() == capabilities.maxFramesInFlight);
    TRUFFLE_CHECK(initialArenaUsage.slots[1].availableBindGroupCount == 10);
    truffle::rhi::RetainedBindGroupDescriptorReuseMaterializer runtimeReuse{
        sharedPoolPlan.materialization.reuseMaterializations[0]};
    TRUFFLE_CHECK(runtimeReuse.cohort_index() == 0);
    TRUFFLE_CHECK(runtimeReuse.partition_index() == 0);
    TRUFFLE_CHECK(runtimeReuse.pool_class() ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(runtimeReuse.kind() ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      live_objects);
    TRUFFLE_CHECK(runtimeReuse.bind_group_capacity() == 5);
    TRUFFLE_CHECK(runtimeReuse.entry_capacity() == 5);
    TRUFFLE_CHECK(runtimeReuse.supports_live_object_reuse());
    const auto initialReuseState = runtimeReuse.state();
    TRUFFLE_CHECK(initialReuseState.issuedRequestCount == 0);
    TRUFFLE_CHECK(initialReuseState.activeReservationCount == 0);
    auto persistentPolicyRequest =
        runtimeReuse.make_reservation_request(3, std::nullopt, true);
    TRUFFLE_CHECK(persistentPolicyRequest.ok());
    TRUFFLE_CHECK(persistentPolicyRequest.value().frameIndex == 0);
    TRUFFLE_CHECK(persistentPolicyRequest.value().liveObjectReuse);
    const auto perFrameRequest = truffle::rhi::BindGroupDescriptorArenaReservationRequest{
        .bindGroupCount = 4,
        .entryCount = 4,
        .frameIndex = 1,
    };
    TRUFFLE_CHECK(runtimeArena.can_reserve(perFrameRequest));
    auto perFrameReservation = runtimeArena.reserve(perFrameRequest);
    TRUFFLE_CHECK(perFrameReservation.ok());
    TRUFFLE_CHECK(perFrameReservation.value().frameIndex == 1);
    const auto midArenaUsage = runtimeArena.usage();
    TRUFFLE_CHECK(midArenaUsage.reservationCount == 1);
    TRUFFLE_CHECK(midArenaUsage.usedBindGroupCount == 4);
    TRUFFLE_CHECK(midArenaUsage.usedEntryCount == 4);
    TRUFFLE_CHECK(midArenaUsage.slots[1].usedBindGroupCount == 4);
    TRUFFLE_CHECK(midArenaUsage.slots[1].availableEntryCount == 6);
    TRUFFLE_CHECK(!runtimeArena.can_reserve({
        .bindGroupCount = 7,
        .entryCount = 7,
        .frameIndex = 1,
    }));
    auto retiredSlot = runtimeArena.retire_slot(1);
    TRUFFLE_CHECK(retiredSlot.ok());
    TRUFFLE_CHECK(retiredSlot.value().slotIndex == 1);
    TRUFFLE_CHECK(retiredSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(retiredSlot.value().releasedBindGroupCount == 4);
    TRUFFLE_CHECK(retiredSlot.value().releasedEntryCount == 4);
    TRUFFLE_CHECK(runtimeArena.empty());
    truffle::rhi::RetainedBindGroupDescriptorArena persistentRuntimeArena{
        sharedPoolPlan.materialization.arenas[0]};
    TRUFFLE_CHECK(runtimeReuse.compatible_with(persistentRuntimeArena));
    TRUFFLE_CHECK(!runtimeReuse.compatible_with(runtimeArena));
    TRUFFLE_CHECK(
        persistentRuntimeArena.can_reserve(persistentPolicyRequest.value()));
    auto persistentReservation =
        persistentRuntimeArena.reserve(persistentPolicyRequest.value());
    TRUFFLE_CHECK(persistentReservation.ok());
    TRUFFLE_CHECK(runtimeReuse.observe_reservation(persistentReservation.value()).ok());
    TRUFFLE_CHECK(persistentReservation.value().entryCount == 3);
    const auto midReuseState = runtimeReuse.state();
    TRUFFLE_CHECK(midReuseState.issuedRequestCount == 1);
    TRUFFLE_CHECK(midReuseState.activeReservationCount == 1);
    TRUFFLE_CHECK(midReuseState.activeBindGroupCount == 3);
    TRUFFLE_CHECK(midReuseState.liveObjectReservationCount == 1);
    TRUFFLE_CHECK(midReuseState.trackedReservations.size() == 1);
    TRUFFLE_CHECK(midReuseState.lastFrameIndex.has_value());
    TRUFFLE_CHECK(*midReuseState.lastFrameIndex == 0);
    const auto persistentArenaPressure =
        truffle::rhi::bind_group_descriptor_arena_pressure(persistentRuntimeArena);
    TRUFFLE_CHECK(persistentArenaPressure.level ==
                  truffle::rhi::BindGroupDescriptorPressureLevel::moderate);
    TRUFFLE_CHECK(!persistentArenaPressure.shouldReclaimBeforeGrowing);
    const auto persistentReusePressure =
        truffle::rhi::bind_group_descriptor_reuse_materializer_pressure(runtimeReuse);
    TRUFFLE_CHECK(persistentReusePressure.level ==
                  truffle::rhi::BindGroupDescriptorPressureLevel::moderate);
    TRUFFLE_CHECK(!persistentReusePressure.shouldThrottleRequests);
    TRUFFLE_CHECK(
        !persistentRuntimeArena.can_reserve(runtimeReuse.reservation_request(3)));
    TRUFFLE_CHECK(runtimeReuse.release_reservation(persistentReservation.value()).ok());
    auto reuseStateAfterRelease = runtimeReuse.state();
    TRUFFLE_CHECK(reuseStateAfterRelease.activeReservationCount == 0);
    TRUFFLE_CHECK(persistentRuntimeArena.clear().ok());
    TRUFFLE_CHECK(persistentRuntimeArena.empty());
    TRUFFLE_CHECK(runtimeReuse.clear().ok());
    truffle::rhi::RetainedBindGroupDescriptorArena perFramePolicyArena{
        sharedPoolPlan.materialization.arenas[1]};
    truffle::rhi::RetainedBindGroupDescriptorReuseMaterializer perFrameRuntimeReuse{
        sharedPoolPlan.materialization.reuseMaterializations[1]};
    TRUFFLE_CHECK(perFrameRuntimeReuse.compatible_with(perFramePolicyArena));
    auto rotatingRequest0 =
        perFrameRuntimeReuse.make_reservation_request(2, std::nullopt, true);
    TRUFFLE_CHECK(rotatingRequest0.ok());
    TRUFFLE_CHECK(rotatingRequest0.value().frameIndex == 0);
    TRUFFLE_CHECK(!rotatingRequest0.value().liveObjectReuse);
    auto rotatingReservation0 =
        perFramePolicyArena.reserve(rotatingRequest0.value());
    TRUFFLE_CHECK(rotatingReservation0.ok());
    TRUFFLE_CHECK(
        perFrameRuntimeReuse.observe_reservation(rotatingReservation0.value()).ok());
    auto rotatingRequest1 =
        perFrameRuntimeReuse.make_reservation_request(3, std::nullopt, true);
    TRUFFLE_CHECK(rotatingRequest1.ok());
    TRUFFLE_CHECK(rotatingRequest1.value().frameIndex == 1);
    auto rotatingReservation1 =
        perFramePolicyArena.reserve(rotatingRequest1.value());
    TRUFFLE_CHECK(rotatingReservation1.ok());
    TRUFFLE_CHECK(
        perFrameRuntimeReuse.observe_reservation(rotatingReservation1.value()).ok());
    const auto perFrameReuseState = perFrameRuntimeReuse.state();
    TRUFFLE_CHECK(perFrameReuseState.issuedRequestCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.activeReservationCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.activeBindGroupCount == 5);
    TRUFFLE_CHECK(perFrameReuseState.capacityOnlyReservationCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.trackedReservations.size() == 2);
    TRUFFLE_CHECK(perFrameReuseState.lastFrameIndex.has_value());
    TRUFFLE_CHECK(*perFrameReuseState.lastFrameIndex == 1);
    TRUFFLE_CHECK(perFrameReuseState.nextFrameIndex ==
                  (2 % perFramePolicyArena.slot_count()));
    TRUFFLE_CHECK(perFrameReuseState.slots[0].activeBindGroupCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.slots[1].activeBindGroupCount == 3);
    auto retiredReuseSlot = perFrameRuntimeReuse.retire_slot(1);
    TRUFFLE_CHECK(retiredReuseSlot.ok());
    TRUFFLE_CHECK(retiredReuseSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(retiredReuseSlot.value().releasedBindGroupCount == 3);
    TRUFFLE_CHECK(
        perFrameRuntimeReuse.release_reservation(rotatingReservation0.value()).ok());
    TRUFFLE_CHECK(perFrameRuntimeReuse.clear().ok());
    TRUFFLE_CHECK(perFramePolicyArena.clear().ok());
    TRUFFLE_CHECK(perFrameRuntimeReuse.state().issuedRequestCount == 0);
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator persistentCoordinator{
        persistentRuntimeArena, runtimeReuse};
    TRUFFLE_CHECK(persistentCoordinator.compatible());
    TRUFFLE_CHECK(persistentCoordinator.empty());
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    TRUFFLE_CHECK(persistentCoordinator.empty());
    auto coordinatedPersistentRequest =
        persistentCoordinator.make_reservation_request(2);
    TRUFFLE_CHECK(coordinatedPersistentRequest.ok());
    TRUFFLE_CHECK(coordinatedPersistentRequest.value().frameIndex == 0);
    TRUFFLE_CHECK(persistentCoordinator.can_reserve(2));
    auto coordinatedPersistentReservation = persistentCoordinator.reserve(2);
    TRUFFLE_CHECK(coordinatedPersistentReservation.ok());
    const auto coordinatedPersistentState = persistentCoordinator.state();
    TRUFFLE_CHECK(coordinatedPersistentState.compatible);
    TRUFFLE_CHECK(!coordinatedPersistentState.drifted);
    TRUFFLE_CHECK(coordinatedPersistentState.underlyingReservationsConsistent);
    TRUFFLE_CHECK(coordinatedPersistentState.trackedReservationCount == 1);
    TRUFFLE_CHECK(coordinatedPersistentState.trackedBindGroupCount == 2);
    TRUFFLE_CHECK(coordinatedPersistentState.arenaUsage.reservationCount == 1);
    TRUFFLE_CHECK(coordinatedPersistentState.arenaUsage.reservations.size() == 1);
    TRUFFLE_CHECK(coordinatedPersistentState.reuseState.activeReservationCount == 1);
    TRUFFLE_CHECK(coordinatedPersistentState.reuseState.trackedReservations.size() == 1);
    const auto coordinatedPersistentPressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            persistentCoordinator);
    TRUFFLE_CHECK(coordinatedPersistentPressure.action ==
                  truffle::rhi::BindGroupDescriptorRuntimePressureAction::none);
    TRUFFLE_CHECK(!coordinatedPersistentPressure.shouldThrottleReservations);
    TRUFFLE_CHECK(!persistentCoordinator.can_reserve(4));
    auto externalPersistentRequest =
        runtimeReuse.make_reservation_request(1, std::nullopt, true);
    TRUFFLE_CHECK(externalPersistentRequest.ok());
    auto externalPersistentReservation =
        persistentRuntimeArena.reserve(externalPersistentRequest.value());
    TRUFFLE_CHECK(externalPersistentReservation.ok());
    TRUFFLE_CHECK(runtimeReuse.observe_reservation(
                      externalPersistentReservation.value())
                      .ok());
    const auto driftedPersistentState = persistentCoordinator.state();
    TRUFFLE_CHECK(driftedPersistentState.drifted);
    TRUFFLE_CHECK(driftedPersistentState.underlyingReservationsConsistent);
    TRUFFLE_CHECK(driftedPersistentState.trackedReservationCount == 1);
    TRUFFLE_CHECK(driftedPersistentState.arenaUsage.reservationCount == 2);
    TRUFFLE_CHECK(driftedPersistentState.reuseState.activeReservationCount == 2);
    const auto driftedPersistentPressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            persistentCoordinator);
    TRUFFLE_CHECK(driftedPersistentPressure.action ==
                  truffle::rhi::BindGroupDescriptorRuntimePressureAction::reconcile);
    TRUFFLE_CHECK(driftedPersistentPressure.shouldReconcile);
    TRUFFLE_CHECK(driftedPersistentPressure.externalReservationCount == 1);
    const auto driftedPersistentReclamationPlan =
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            persistentCoordinator);
    TRUFFLE_CHECK(driftedPersistentReclamationPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::reconcile);
    const std::vector<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*>
        driftedAdmissionCoordinators = {&persistentCoordinator};
    const auto reconcileAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 1);
    TRUFFLE_CHECK(reconcileAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reconcile_then_admit);
    TRUFFLE_CHECK(!reconcileAdmissionPlan.shouldAttemptImmediateAdmission);
    TRUFFLE_CHECK(reconcileAdmissionPlan.reconcileAdmissionCount == 1);
    TRUFFLE_CHECK(reconcileAdmissionPlan.preferredCoordinatorIndex.has_value());
    TRUFFLE_CHECK(*reconcileAdmissionPlan.preferredCoordinatorIndex == 0);
    const auto reconcileReclaimAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 3);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reconcile_then_reclaim_then_admit);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.reconcileAdmissionCount == 1);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.reclaimAdmissionCount == 1);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.totalRecoverableBindGroupRelief ==
                  3);
    TRUFFLE_CHECK(!persistentCoordinator.can_reserve(1));
    TRUFFLE_CHECK(!persistentCoordinator.release(
                      coordinatedPersistentReservation.value())
                      .ok());
    TRUFFLE_CHECK(persistentCoordinator.reconcile().ok());
    const auto reconciledPersistentState = persistentCoordinator.state();
    TRUFFLE_CHECK(!reconciledPersistentState.drifted);
    TRUFFLE_CHECK(reconciledPersistentState.trackedReservationCount == 2);
    TRUFFLE_CHECK(
        persistentCoordinator.release(externalPersistentReservation.value()).ok());
    TRUFFLE_CHECK(
        persistentCoordinator.release(coordinatedPersistentReservation.value()).ok());
    TRUFFLE_CHECK(persistentCoordinator.state().trackedReservationCount == 0);
    auto inconsistentPersistentReservation =
        persistentRuntimeArena.reserve(runtimeReuse.reservation_request(1));
    TRUFFLE_CHECK(inconsistentPersistentReservation.ok());
    const auto inconsistentPersistentState = persistentCoordinator.state();
    TRUFFLE_CHECK(inconsistentPersistentState.drifted);
    TRUFFLE_CHECK(!inconsistentPersistentState.underlyingReservationsConsistent);
    const auto inconsistentPersistentPressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            persistentCoordinator);
    TRUFFLE_CHECK(!inconsistentPersistentPressure.underlyingReservationsConsistent);
    TRUFFLE_CHECK(inconsistentPersistentPressure.shouldReconcile);
    const auto inconsistentPersistentReclamationPlan =
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            persistentCoordinator);
    TRUFFLE_CHECK(
        inconsistentPersistentReclamationPlan.action ==
        truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::
            audit_inconsistent_state);
    const auto auditAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 1);
    TRUFFLE_CHECK(auditAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      audit_before_admit);
    TRUFFLE_CHECK(auditAdmissionPlan.auditCoordinatorCount == 1);
    TRUFFLE_CHECK(auditAdmissionPlan.shouldAuditBeforeAdmission);
    TRUFFLE_CHECK(!persistentCoordinator.reconcile().ok());
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    TRUFFLE_CHECK(!persistentCoordinator.drifted());
    auto pressurePersistentReservation = persistentCoordinator.reserve(4);
    TRUFFLE_CHECK(pressurePersistentReservation.ok());
    const auto pressureReclamationPlan =
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            persistentCoordinator);
    TRUFFLE_CHECK(pressureReclamationPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::
                      release_candidates);
    TRUFFLE_CHECK(pressureReclamationPlan.recommendedReleaseCount == 1);
    TRUFFLE_CHECK(pressureReclamationPlan.recommendedBindGroupRelief == 4);
    TRUFFLE_CHECK(pressureReclamationPlan.candidates.size() == 1);
    TRUFFLE_CHECK(!pressureReclamationPlan.candidates[0].preferred);
    truffle::rhi::RetainedBindGroupDescriptorArena arbitrationPerFrameArena{
        sharedPoolPlan.materialization.arenas[1]};
    truffle::rhi::RetainedBindGroupDescriptorReuseMaterializer
        arbitrationPerFrameReuse{
            sharedPoolPlan.materialization.reuseMaterializations[1]};
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator
        arbitrationPerFrameCoordinator{arbitrationPerFrameArena,
                                       arbitrationPerFrameReuse};
    TRUFFLE_CHECK(arbitrationPerFrameCoordinator.reserve(10, 0).ok());
    TRUFFLE_CHECK(arbitrationPerFrameCoordinator.reserve(10, 1).ok());
    const std::vector<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*>
        arbitrationCoordinators = {&persistentCoordinator,
                                   &arbitrationPerFrameCoordinator};
    const auto arbitrationPlan =
        truffle::rhi::bind_group_descriptor_runtime_arbitration_plan(
            arbitrationCoordinators);
    TRUFFLE_CHECK(arbitrationPlan.coordinatorCount == 2);
    TRUFFLE_CHECK(arbitrationPlan.compatibleCoordinatorCount == 2);
    TRUFFLE_CHECK(arbitrationPlan.reclaimingCoordinatorCount == 2);
    TRUFFLE_CHECK(arbitrationPlan.preferredCoordinatorIndex.has_value());
    TRUFFLE_CHECK(*arbitrationPlan.preferredCoordinatorIndex == 1);
    TRUFFLE_CHECK(arbitrationPlan.preferredReclamationAction ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::
                      retire_slot);
    TRUFFLE_CHECK(arbitrationPlan.preferredSlotIndex.has_value());
    TRUFFLE_CHECK(arbitrationPlan.coordinators.size() == 2);
    TRUFFLE_CHECK(arbitrationPlan.coordinators[0].preferred);
    TRUFFLE_CHECK(arbitrationPlan.coordinators[0].coordinatorIndex == 1);
    TRUFFLE_CHECK(arbitrationPlan.coordinators[1].coordinatorIndex == 0);
    const auto immediateAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            arbitrationCoordinators, 1, 0);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinatorCount == 2);
    TRUFFLE_CHECK(immediateAdmissionPlan.immediateAdmissionCount == 1);
    TRUFFLE_CHECK(immediateAdmissionPlan.reclaimAdmissionCount == 1);
    TRUFFLE_CHECK(immediateAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      admit_now);
    TRUFFLE_CHECK(immediateAdmissionPlan.shouldAttemptImmediateAdmission);
    TRUFFLE_CHECK(immediateAdmissionPlan.preferredCoordinatorIndex.has_value());
    TRUFFLE_CHECK(*immediateAdmissionPlan.preferredCoordinatorIndex == 0);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinators.size() == 2);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinators[0].preferred);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinators[0].action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      admit_now);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinators[1].action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reclaim_then_admit);
    const std::vector<truffle::rhi::BindGroupDescriptorRuntimeBatchAdmissionIntent>
        batchAdmissionRequests = {{
            .bindGroupCount = 1,
            .frameIndex = 0,
        },
        {
            .bindGroupCount = 1,
            .frameIndex = 0,
        },
        {
            .bindGroupCount = 4,
            .frameIndex = 0,
        }};
    const auto batchAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_batch_admission_plan(
            arbitrationCoordinators, batchAdmissionRequests);
    TRUFFLE_CHECK(batchAdmissionPlan.requestCount == 3);
    TRUFFLE_CHECK(batchAdmissionPlan.admittedCount == 3);
    TRUFFLE_CHECK(batchAdmissionPlan.immediateAdmissionCount == 1);
    TRUFFLE_CHECK(batchAdmissionPlan.reclaimAdmissionCount == 2);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions.size() == 3);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[0].admitted);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[0].admission.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      admit_now);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[0].admission.coordinatorIndex == 0);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[1].admission.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reclaim_then_admit);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[1].admission.coordinatorIndex == 0);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[2].admission.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reclaim_then_admit);
    TRUFFLE_CHECK(batchAdmissionPlan.decisions[2].admission.coordinatorIndex == 1);
    TRUFFLE_CHECK(batchAdmissionPlan.remainingRecoverableBindGroupRelief >= 9);
    TRUFFLE_CHECK(arbitrationPerFrameCoordinator.clear().ok());
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator incompatibleCoordinator{
        runtimeArena, runtimeReuse};
    TRUFFLE_CHECK(!incompatibleCoordinator.compatible());
    TRUFFLE_CHECK(!incompatibleCoordinator.make_reservation_request(1).ok());
    TRUFFLE_CHECK(!incompatibleCoordinator.can_reserve(1));
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator perFrameCoordinator{
        perFramePolicyArena, perFrameRuntimeReuse};
    TRUFFLE_CHECK(perFrameCoordinator.compatible());
    auto coordinatedFrameReservation0 = perFrameCoordinator.reserve(2);
    TRUFFLE_CHECK(coordinatedFrameReservation0.ok());
    TRUFFLE_CHECK(coordinatedFrameReservation0.value().frameIndex == 0);
    auto coordinatedFrameReservation1 = perFrameCoordinator.reserve(3);
    TRUFFLE_CHECK(coordinatedFrameReservation1.ok());
    TRUFFLE_CHECK(coordinatedFrameReservation1.value().frameIndex == 1);
    const auto coordinatedPerFrameState = perFrameCoordinator.state();
    TRUFFLE_CHECK(coordinatedPerFrameState.trackedReservationCount == 2);
    TRUFFLE_CHECK(!coordinatedPerFrameState.drifted);
    TRUFFLE_CHECK(coordinatedPerFrameState.reuseState.nextFrameIndex ==
                  (2 % perFramePolicyArena.slot_count()));
    const auto coordinatedPerFramePressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            perFrameCoordinator);
    TRUFFLE_CHECK(coordinatedPerFramePressure.action ==
                  truffle::rhi::BindGroupDescriptorRuntimePressureAction::none);
    const auto coordinatedPerFrameReclamationPlan =
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            perFrameCoordinator);
    TRUFFLE_CHECK(coordinatedPerFrameReclamationPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::none);
    auto coordinatedRetiredSlot = perFrameCoordinator.retire_slot(1);
    TRUFFLE_CHECK(coordinatedRetiredSlot.ok());
    TRUFFLE_CHECK(coordinatedRetiredSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(coordinatedRetiredSlot.value().releasedBindGroupCount == 3);
    TRUFFLE_CHECK(perFrameCoordinator.state().trackedReservationCount == 1);
    TRUFFLE_CHECK(perFrameCoordinator.clear().ok());
    TRUFFLE_CHECK(perFrameCoordinator.empty());
    TRUFFLE_CHECK(
        truffle::rhi::validation::bind_group_descriptor_arena_materialization_valid(
            sharedPoolPlan.materialization.arenas[0], capabilities));
    auto invalidRuntimeArenaDesc = sharedPoolPlan.materialization.arenas[1];
    invalidRuntimeArenaDesc.frameSlotCount = capabilities.maxFramesInFlight + 1;
    TRUFFLE_CHECK(
        !truffle::rhi::validation::bind_group_descriptor_arena_materialization_valid(
            invalidRuntimeArenaDesc, capabilities));
    TRUFFLE_CHECK(
        truffle::rhi::validation::bind_group_descriptor_arena_reservation_request_valid(
            perFrameRequest, sharedPoolPlan.materialization.arenas[1]));
    TRUFFLE_CHECK(
        truffle::rhi::validation::bind_group_descriptor_reuse_materializer_compatible(
            sharedPoolPlan.materialization.reuseMaterializations[0],
            sharedPoolPlan.materialization.arenas[0]));
    TRUFFLE_CHECK(
        !truffle::rhi::validation::bind_group_descriptor_reuse_materializer_compatible(
            sharedPoolPlan.materialization.reuseMaterializations[0],
            sharedPoolPlan.materialization.arenas[1]));
    TRUFFLE_CHECK(
        truffle::rhi::validation::bind_group_descriptor_reuse_materializer_request_valid(
            persistentPolicyRequest.value(),
            sharedPoolPlan.materialization.reuseMaterializations[0]));
    TRUFFLE_CHECK(
        !truffle::rhi::validation::bind_group_descriptor_arena_reservation_request_valid(
            {
                .bindGroupCount = 11,
                .entryCount = 11,
                .frameIndex = 1,
            },
            sharedPoolPlan.materialization.arenas[1]));
    TRUFFLE_CHECK(
        !truffle::rhi::validation::bind_group_descriptor_reuse_materializer_request_valid(
            {
                .bindGroupCount = 1,
                .entryCount = 1,
                .frameIndex = 0,
                .liveObjectReuse = true,
            },
            sharedPoolPlan.materialization.reuseMaterializations[1]));
    TRUFFLE_CHECK(
        truffle::rhi::validation::bind_group_descriptor_reuse_materialization_valid(
            sharedPoolPlan.materialization.reuseMaterializations[0], capabilities));
    auto invalidRuntimeReuseDesc =
        sharedPoolPlan.materialization.reuseMaterializations[0];
    invalidRuntimeReuseDesc.supportsLiveObjectReuse = false;
    TRUFFLE_CHECK(
        !truffle::rhi::validation::bind_group_descriptor_reuse_materialization_valid(
            invalidRuntimeReuseDesc, capabilities));
    auto saturatedBudgetTotal = truffle::rhi::BindGroupDescriptorBudget{
        .model = truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans,
        .totalUnits = std::numeric_limits<std::uint32_t>::max() - 2,
        .bufferUnits = std::numeric_limits<std::uint32_t>::max() - 3,
        .textureUnits = std::numeric_limits<std::uint32_t>::max() - 4,
        .samplerUnits = std::numeric_limits<std::uint32_t>::max() - 5,
    };
    truffle::rhi::include_bind_group_descriptor_budget(
        saturatedBudgetTotal,
        {
            .model = truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans,
            .totalUnits = 8,
            .bufferUnits = 7,
            .textureUnits = 6,
            .samplerUnits = 5,
        });
    TRUFFLE_CHECK(saturatedBudgetTotal.totalUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudgetTotal.bufferUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudgetTotal.textureUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    TRUFFLE_CHECK(saturatedBudgetTotal.samplerUnits ==
                  std::numeric_limits<std::uint32_t>::max());
    auto singleFrameCaps = capabilities;
    singleFrameCaps.maxFramesInFlight = 1;
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_desc_valid({
        .layout = &bindGroupLayout,
        .allocationPolicy =
            truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        .allocationFrameIndex = 1,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    }, singleFrameCaps));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_desc_valid({
        .layout = &bindGroupLayout,
        .allocationFrameIndex = 1,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_desc_valid({
        .layout = &bindGroupLayout,
        .reuseHint = truffle::rhi::BindGroupReuseHint::rebuild,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = &sampledTexture,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = &sampler,
            },
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_desc_valid({
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = &uniformBuffer, .size = 16},
        }},
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_desc_valid({
        .layout = &bindGroupLayout,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = &uniformBuffer, .size = 16},
            },
        },
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::render_pass_desc_valid({
        .extent = {16, 16},
        .colorAttachment = {
            .texture = &colorOnlyTexture,
            .loadOp = truffle::rhi::LoadOp::clear,
            .storeOp = truffle::rhi::StoreOp::store,
            .clearValue = {.r = 0.1f, .g = 0.2f, .b = 0.3f, .a = 1.0f},
        },
        .depthAttachment = {
            .texture = &depthStencilAttachment,
            .loadOp = truffle::rhi::LoadOp::clear,
            .storeOp = truffle::rhi::StoreOp::store,
            .clearDepth = 1.0f,
            .stencilLoadOp = truffle::rhi::LoadOp::clear,
            .stencilStoreOp = truffle::rhi::StoreOp::store,
            .clearStencil = 7,
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::render_pass_desc_valid({
        .extent = {16, 16},
        .depthAttachment = {
            .texture = &depthOnlyAttachment,
            .stencilLoadOp = truffle::rhi::LoadOp::clear,
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::render_pass_desc_valid({
        .extent = {16, 16},
        .depthAttachment = {
            .texture = &depthStencilAttachment,
            .clearDepth = 2.0f,
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::render_pass_desc_valid({
        .extent = {17, 16},
        .depthAttachment = {
            .texture = &depthStencilAttachment,
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::render_pass_desc_valid({
        .extent = {16, 16},
        .colorAttachment = {
            .texture = &depthOnlyAttachment,
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 0,
        .type = truffle::rhi::BindingResourceType::uniform_buffer,
        .buffer = {.buffer = &smallUniformBuffer},
    }, bindGroupLayoutDesc.bindings[0]));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 0,
        .type = truffle::rhi::BindingResourceType::uniform_buffer,
        .buffer = {.buffer = &uniformBuffer, .offset = 64},
    }, bindGroupLayoutDesc.bindings[0]));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 1,
        .type = truffle::rhi::BindingResourceType::sampled_texture,
        .texture = &colorOnlyTexture,
    }, bindGroupLayoutDesc.bindings[1]));

    const truffle::rhi::BindGroupLayoutDesc dynamicBindGroupLayoutDesc{
        .debugName = "core_dynamic_bind_group_layout",
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .arrayCount = 2,
            .minBindingSize = 16,
            .dynamicOffset = true,
        }},
    };
    TestBindGroupLayout dynamicBindGroupLayout{dynamicBindGroupLayoutDesc};
    const truffle::rhi::BindGroupDesc dynamicBindGroupDesc{
        .layout = &dynamicBindGroupLayout,
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffers = {
                {.buffer = &uniformBuffer, .size = 16},
                {.buffer = &secondUniformBuffer, .offset = 16, .size = 16},
            },
        }},
    };
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid(
        dynamicBindGroupDesc));
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 16},
            {.bindingIndex = 0, .arrayElement = 1, .offset = 16},
        }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 16},
        }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 16},
            {.bindingIndex = 0, .arrayElement = 0, .offset = 16},
        }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 16},
            {.bindingIndex = 0, .arrayElement = 2, .offset = 16},
        }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 56},
            {.bindingIndex = 0, .arrayElement = 1, .offset = 16},
        }));
    auto dynamicOffsetLimits = truffle::rhi::DeviceLimits{};
    dynamicOffsetLimits.minUniformBufferOffsetAlignment = 16;
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        dynamicBindGroupDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 4},
            {.bindingIndex = 0, .arrayElement = 1, .offset = 16},
        },
        dynamicOffsetLimits));
    const truffle::rhi::BindGroupDesc compensatingStaticOffsetDesc{
        .layout = &dynamicBindGroupLayout,
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffers = {
                {.buffer = &uniformBuffer, .offset = 8, .size = 16},
                {.buffer = &secondUniformBuffer, .offset = 16, .size = 16},
            },
        }},
    };
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid(
        compensatingStaticOffsetDesc));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        compensatingStaticOffsetDesc,
        {
            {.bindingIndex = 0, .arrayElement = 0, .offset = 8},
            {.bindingIndex = 0, .arrayElement = 1, .offset = 16},
        },
        dynamicOffsetLimits));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_dynamic_offsets_valid(
        {
            .layout = &bindGroupLayout,
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .buffer = {.buffer = &uniformBuffer, .size = 16},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .texture = &sampledTexture,
                },
                {
                    .bindingIndex = 2,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .sampler = &sampler,
                },
            },
        },
        {{.bindingIndex = 0, .offset = 4}}));

    const truffle::rhi::BindGroupLayoutDesc arrayBindGroupLayoutDesc{
        .debugName = "core_array_bind_group_layout",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .arrayCount = 2,
                .minBindingSize = 16,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
            },
        },
    };
    TestBindGroupLayout arrayBindGroupLayout{arrayBindGroupLayoutDesc};
    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid({
        .layout = &arrayBindGroupLayout,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffers = {
                    {.buffer = &uniformBuffer, .size = 16},
                    {.buffer = &secondUniformBuffer, .size = 16},
                },
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .textures = {&sampledTexture, &secondSampledTexture},
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .samplers = {&sampler, &secondSampler},
            },
        },
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 0,
        .type = truffle::rhi::BindingResourceType::uniform_buffer,
        .buffer = {.buffer = &uniformBuffer, .size = 16},
    }, arrayBindGroupLayoutDesc.bindings[0]));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 1,
        .type = truffle::rhi::BindingResourceType::sampled_texture,
        .textures = {&sampledTexture},
    }, arrayBindGroupLayoutDesc.bindings[1]));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 1,
        .type = truffle::rhi::BindingResourceType::sampled_texture,
        .texture = &sampledTexture,
        .textures = {&secondSampledTexture},
    }, arrayBindGroupLayoutDesc.bindings[1]));
    TRUFFLE_CHECK(!truffle::rhi::validation::bind_group_entry_valid({
        .bindingIndex = 1,
        .type = truffle::rhi::BindingResourceType::sampled_texture,
        .textures = {&sampledTexture, &colorOnlyTexture},
    }, arrayBindGroupLayoutDesc.bindings[1]));

    return 0;
}
