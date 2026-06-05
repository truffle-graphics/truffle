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
    TRUFFLE_CHECK(truffle::rhi::validation::memory_domain_supported(
        truffle::rhi::MemoryDomain::upload, capabilities));
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
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 0, group0Layout));
    TRUFFLE_CHECK(truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 1, group1Layout));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 2, group0Layout));
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_layout_bind_group_compatible(
        groupedPipelineLayout, 1, group0Layout));
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
    TestSampler sampler;
    TestSampler secondSampler;
    TestBindGroupLayout bindGroupLayout{bindGroupLayoutDesc};

    TRUFFLE_CHECK(truffle::rhi::validation::bind_group_desc_valid({
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
