#include "test_support.hpp"

#include "truffle/core/status.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/rhi/validation.hpp"
#if defined(TRUFFLE_HAS_VULKAN_BACKEND)
#include "truffle/rhi/vulkan_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_METAL_BACKEND)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_OPENGL_BACKEND)
#include "truffle/rhi/opengl_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_DIRECT3D_BACKEND)
#include "truffle/rhi/direct3d_backend.hpp"
#endif

#include <cstring>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {

std::vector<std::byte> to_bytes(const char* src) {
    const auto* begin = reinterpret_cast<const std::byte*>(src);
    return {begin, begin + std::strlen(src)};
}

bool verify_reflection_invariants(const truffle::rhi::IPipelineReflection* reflection,
                                   bool expectComputeStages) {
    if (!reflection) {
        return true;
    }

    std::unordered_set<std::uint64_t> seen;
    for (std::size_t i = 0; i < reflection->get_binding_count(); ++i) {
        const auto& binding = reflection->get_binding_info(i);
        if (binding.type == truffle::rhi::ResourceBindingType::Unknown) {
            return false;
        }
        if (binding.arrayCount == 0 || !binding.readOnly) {
            return false;
        }

        if (expectComputeStages) {
            if (binding.stage != truffle::rhi::ShaderStage::compute) {
                return false;
            }
        } else {
            if (binding.stage != truffle::rhi::ShaderStage::vertex &&
                binding.stage != truffle::rhi::ShaderStage::fragment) {
                return false;
            }
        }

        const std::uint64_t key =
            (static_cast<std::uint64_t>(binding.bindingIndex) << 16u) |
            (static_cast<std::uint64_t>(binding.stage) << 8u) |
            static_cast<std::uint64_t>(binding.type);
        if (!seen.insert(key).second) {
            return false;
        }
        const auto* found = reflection->find_binding(
            binding.bindingIndex, binding.stage, binding.type);
        if (!found || found->bindingIndex != binding.bindingIndex ||
            found->stage != binding.stage || found->type != binding.type) {
            return false;
        }
        if (!reflection->find_binding(binding.bindingIndex, binding.stage)) {
            return false;
        }
    }

    return true;
}

int verify_capability_contract(const truffle::rhi::IBackend& backend,
                               const truffle::rhi::IDevice& device) {
    const auto adapters = backend.enumerate_adapters();
    TRUFFLE_CHECK(!adapters.empty());

    const auto& adapter = adapters.front();
    TRUFFLE_CHECK(adapter.id == 0);
    TRUFFLE_CHECK(!adapter.name.empty());
    TRUFFLE_CHECK(adapter.backend == backend.kind());
    TRUFFLE_CHECK(adapter.type != truffle::rhi::AdapterType::unknown);
    TRUFFLE_CHECK(!adapter.driverDescription.empty());

    const auto& caps = device.capabilities();
    TRUFFLE_CHECK(adapter.capabilities.maxFramesInFlight == caps.maxFramesInFlight);
    TRUFFLE_CHECK(adapter.capabilities.descriptorPolicy.mappingModel ==
                  caps.descriptorPolicy.mappingModel);
    TRUFFLE_CHECK(adapter.capabilities.descriptorPolicy.allocationModel ==
                  caps.descriptorPolicy.allocationModel);
    TRUFFLE_CHECK(adapter.capabilities.descriptorPolicy.flattenedNativeBindings ==
                  caps.descriptorPolicy.flattenedNativeBindings);
    TRUFFLE_CHECK(caps.maxFramesInFlight >= 1);
    TRUFFLE_CHECK(truffle::rhi::supports_queue(caps, truffle::rhi::QueueKind::graphics));
    TRUFFLE_CHECK(truffle::rhi::supports_queue(caps, truffle::rhi::QueueKind::compute));
    TRUFFLE_CHECK(truffle::rhi::supports_queue(caps, truffle::rhi::QueueKind::transfer));
    TRUFFLE_CHECK(caps.features.headlessSurface);
    TRUFFLE_CHECK(caps.features.compute);
    TRUFFLE_CHECK(caps.features.indirectDraw);
    TRUFFLE_CHECK(caps.features.validation == caps.validation);
    TRUFFLE_CHECK(caps.features.presentation == caps.presentation);
    TRUFFLE_CHECK(!caps.presentModes.empty());
    TRUFFLE_CHECK(truffle::rhi::supports_present_mode(
        caps, truffle::rhi::PresentMode::fifo));
    TRUFFLE_CHECK(!caps.surfaceKinds.empty());
    TRUFFLE_CHECK(truffle::rhi::supports_native_surface_kind(
        caps, truffle::rhi::NativeSurfaceKind::headless));
    TRUFFLE_CHECK(!caps.shaderFormats.empty());
    TRUFFLE_CHECK(truffle::rhi::supports_shader_byte_format(
        caps, truffle::rhi::ShaderByteFormat::unknown));
    TRUFFLE_CHECK(caps.limits.maxTextureDimension2D >= 32);
    TRUFFLE_CHECK(caps.limits.maxBufferSize >= 128);
    TRUFFLE_CHECK(caps.limits.minUniformBufferOffsetAlignment >= 1);
    TRUFFLE_CHECK(caps.limits.minStorageBufferOffsetAlignment >= 1);
    TRUFFLE_CHECK(caps.limits.maxColorAttachments >= 1);
    TRUFFLE_CHECK(caps.limits.maxVertexBuffers >= 1);
    TRUFFLE_CHECK(caps.limits.maxVertexAttributes >= 1);
    TRUFFLE_CHECK(caps.limits.maxVertexBufferStride >= 16);
    TRUFFLE_CHECK(caps.limits.maxDescriptorArrayElements >= 1);
    TRUFFLE_CHECK(caps.limits.maxSamplerAnisotropy >= 1);
    TRUFFLE_CHECK(truffle::rhi::supports_descriptor_arrays(caps) ==
                  (caps.features.descriptorArrays &&
                   caps.limits.maxDescriptorArrayElements > 1));
    TRUFFLE_CHECK(truffle::rhi::supports_dynamic_resource_indexing(caps) ==
                  (truffle::rhi::supports_descriptor_arrays(caps) &&
                   caps.features.dynamicResourceIndexing));
    TRUFFLE_CHECK(truffle::rhi::supports_bindless_resources(caps) ==
                   (truffle::rhi::supports_dynamic_resource_indexing(caps) &&
                    caps.features.bindlessResources &&
                    caps.limits.maxBindlessResources > 1));
    TRUFFLE_CHECK(!caps.formats.empty());
    TRUFFLE_CHECK(!caps.memoryHeaps.empty());
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        caps, truffle::rhi::TextureFormat::rgba8_unorm));
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        caps, truffle::rhi::TextureFormat::depth32_float_stencil8));

    const auto* colorSupport = truffle::rhi::find_format_support(
        caps, truffle::rhi::TextureFormat::bgra8_unorm);
    TRUFFLE_CHECK(colorSupport != nullptr);
    TRUFFLE_CHECK(colorSupport->sampled);
    TRUFFLE_CHECK(colorSupport->colorAttachment);

    const auto* depthSupport = truffle::rhi::find_format_support(
        caps, truffle::rhi::TextureFormat::depth32_float);
    TRUFFLE_CHECK(depthSupport != nullptr);
    TRUFFLE_CHECK(depthSupport->depthStencilAttachment);
    const auto* depthStencilSupport = truffle::rhi::find_format_support(
        caps, truffle::rhi::TextureFormat::depth32_float_stencil8);
    TRUFFLE_CHECK(depthStencilSupport != nullptr);
    TRUFFLE_CHECK(depthStencilSupport->depthStencilAttachment);

    const bool expectsReflection = backend.kind() != truffle::rhi::BackendKind::null_backend;
    TRUFFLE_CHECK(caps.features.shaderReflection == expectsReflection);

    switch (backend.kind()) {
    case truffle::rhi::BackendKind::null_backend:
    case truffle::rhi::BackendKind::metal:
    case truffle::rhi::BackendKind::opengl:
        TRUFFLE_CHECK(caps.descriptorPolicy.mappingModel ==
                      truffle::rhi::NativeDescriptorMappingModel::direct_slots);
        TRUFFLE_CHECK(caps.descriptorPolicy.allocationModel ==
                      truffle::rhi::NativeDescriptorAllocationModel::inline_direct);
        TRUFFLE_CHECK(caps.descriptorPolicy.updateModel ==
                      truffle::rhi::NativeDescriptorUpdateModel::direct_write);
        TRUFFLE_CHECK(caps.descriptorPolicy.budgetModel ==
                      truffle::rhi::NativeDescriptorBudgetModel::native_slot_spans);
        TRUFFLE_CHECK(caps.descriptorPolicy.flattenedNativeBindings);
        break;
    case truffle::rhi::BackendKind::vulkan:
        TRUFFLE_CHECK(caps.descriptorPolicy.mappingModel ==
                      truffle::rhi::NativeDescriptorMappingModel::descriptor_sets);
        TRUFFLE_CHECK(caps.descriptorPolicy.allocationModel ==
                      truffle::rhi::NativeDescriptorAllocationModel::bind_group_owned);
        TRUFFLE_CHECK(caps.descriptorPolicy.updateModel ==
                      truffle::rhi::NativeDescriptorUpdateModel::copy_into_allocation);
        TRUFFLE_CHECK(caps.descriptorPolicy.budgetModel ==
                      truffle::rhi::NativeDescriptorBudgetModel::descriptor_count);
        TRUFFLE_CHECK(!caps.descriptorPolicy.flattenedNativeBindings);
        break;
    case truffle::rhi::BackendKind::direct3d:
        TRUFFLE_CHECK(caps.descriptorPolicy.mappingModel ==
                      truffle::rhi::NativeDescriptorMappingModel::descriptor_tables);
        TRUFFLE_CHECK(caps.descriptorPolicy.allocationModel ==
                      truffle::rhi::NativeDescriptorAllocationModel::bind_group_owned);
        TRUFFLE_CHECK(caps.descriptorPolicy.updateModel ==
                      truffle::rhi::NativeDescriptorUpdateModel::copy_into_allocation);
        TRUFFLE_CHECK(caps.descriptorPolicy.budgetModel ==
                      truffle::rhi::NativeDescriptorBudgetModel::descriptor_count);
        TRUFFLE_CHECK(!caps.descriptorPolicy.flattenedNativeBindings);
        break;
    }

    return 0;
}

truffle::rhi::ShaderDesc make_shader_desc(truffle::rhi::BackendKind backendKind,
                                              truffle::rhi::ShaderStage stage) {
    if (backendKind == truffle::rhi::BackendKind::metal) {
        if (stage == truffle::rhi::ShaderStage::vertex) {
            static const char kVertexMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
vertex float4 vert_main(uint vid [[vertex_id]]) {
    const float2 p[3] = { {-1.0, -1.0}, {3.0, -1.0}, {-1.0, 3.0} };
    return float4(p[vid % 3], 0.0, 1.0);
}
)msl";
            return truffle::rhi::ShaderDesc{
                .stage = stage,
                .byteFormat = truffle::rhi::ShaderByteFormat::msl_source,
                .entryPoint = "vert_main",
                .bytecode = to_bytes(kVertexMSL),
            };
        }

        if (stage == truffle::rhi::ShaderStage::fragment) {
            static const char kFragmentMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
fragment float4 frag_main() {
    return float4(0.2, 0.4, 0.6, 1.0);
}
)msl";
            return truffle::rhi::ShaderDesc{
                .stage = stage,
                .byteFormat = truffle::rhi::ShaderByteFormat::msl_source,
                .entryPoint = "frag_main",
                .bytecode = to_bytes(kFragmentMSL),
            };
        }

        static const char kComputeMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
kernel void comp_main(device uint* data [[buffer(0)]],
                      uint tid [[thread_position_in_grid]]) {
    data[tid] = data[tid] + 1;
}
)msl";
        return truffle::rhi::ShaderDesc{
            .stage = stage,
            .byteFormat = truffle::rhi::ShaderByteFormat::msl_source,
            .entryPoint = "comp_main",
            .bytecode = to_bytes(kComputeMSL),
        };
    }

    return truffle::rhi::ShaderDesc{
        .stage = stage,
        .byteFormat = truffle::rhi::ShaderByteFormat::contract,
        .entryPoint = "main",
        .bytecode = {std::byte{0x1}, std::byte{0x2}},
    };
}

truffle::rhi::ShaderDesc make_depth_only_fragment_shader_desc(
    truffle::rhi::BackendKind backendKind) {
    if (backendKind == truffle::rhi::BackendKind::metal) {
        static const char kFragmentMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
fragment void frag_main() {}
)msl";
        return truffle::rhi::ShaderDesc{
            .stage = truffle::rhi::ShaderStage::fragment,
            .byteFormat = truffle::rhi::ShaderByteFormat::msl_source,
            .entryPoint = "frag_main",
            .bytecode = to_bytes(kFragmentMSL),
        };
    }

    return make_shader_desc(backendKind, truffle::rhi::ShaderStage::fragment);
}

class ForeignBuffer final : public truffle::rhi::IBuffer {
public:
    const truffle::rhi::BufferDesc& desc() const noexcept override { return desc_; }

private:
    truffle::rhi::BufferDesc desc_{
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::vertex |
                      truffle::rhi::BufferUsageFlags::index |
                      truffle::rhi::BufferUsageFlags::uniform |
                      truffle::rhi::BufferUsageFlags::storage |
                      truffle::rhi::BufferUsageFlags::transfer_source |
                      truffle::rhi::BufferUsageFlags::transfer_destination,
    };
};

class ForeignTexture final : public truffle::rhi::ITexture {
public:
    const truffle::rhi::TextureDesc& desc() const noexcept override { return desc_; }

private:
    truffle::rhi::TextureDesc desc_{
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::sampled |
                      truffle::rhi::TextureUsageFlags::color_attachment |
                      truffle::rhi::TextureUsageFlags::transfer_source |
                      truffle::rhi::TextureUsageFlags::transfer_destination,
    };
};

class ForeignPipeline final : public truffle::rhi::IPipeline {
public:
    const truffle::rhi::PipelineDesc& desc() const noexcept override { return desc_; }
    const truffle::rhi::IPipelineReflection* reflection() const noexcept override {
        return nullptr;
    }

private:
    truffle::rhi::PipelineDesc desc_{};
};

class ForeignComputePipeline final : public truffle::rhi::IComputePipeline {
public:
    const truffle::rhi::ComputePipelineDesc& desc() const noexcept override {
        return desc_;
    }
    const truffle::rhi::IPipelineReflection* reflection() const noexcept override {
        return nullptr;
    }

private:
    truffle::rhi::ComputePipelineDesc desc_{};
};

std::unique_ptr<truffle::rhi::IDevice> create_foreign_device(
    truffle::rhi::BackendKind currentKind) {
    auto try_create = [](std::unique_ptr<truffle::rhi::IBackend> backend)
        -> std::unique_ptr<truffle::rhi::IDevice> {
        if (backend->enumerate_adapters().empty()) {
            return nullptr;
        }
        auto device = backend->create_device({});
        if (!device.ok()) {
            return nullptr;
        }
        return std::move(device).value();
    };

#if defined(TRUFFLE_HAS_VULKAN_BACKEND)
    if (currentKind != truffle::rhi::BackendKind::vulkan) {
        if (auto device = try_create(truffle::rhi::create_vulkan_backend())) {
            return device;
        }
    }
#endif
#if defined(TRUFFLE_HAS_OPENGL_BACKEND)
    if (currentKind != truffle::rhi::BackendKind::opengl) {
        if (auto device = try_create(truffle::rhi::create_opengl_backend())) {
            return device;
        }
    }
#endif
#if defined(TRUFFLE_HAS_DIRECT3D_BACKEND)
    if (currentKind != truffle::rhi::BackendKind::direct3d) {
        if (auto device = try_create(truffle::rhi::create_direct3d_backend())) {
            return device;
        }
    }
#endif
#if defined(TRUFFLE_HAS_METAL_BACKEND)
    if (currentKind != truffle::rhi::BackendKind::metal) {
        if (auto device = try_create(truffle::rhi::create_metal_backend())) {
            return device;
        }
    }
#endif
    if (currentKind != truffle::rhi::BackendKind::null_backend) {
        if (auto device = try_create(truffle::rhi::create_null_backend())) {
            return device;
        }
    }
    return nullptr;
}

int verify_common_positive_path_contract(truffle::rhi::IDevice& device,
                                          truffle::rhi::ISurface& surface,
                                          truffle::rhi::BackendKind backendKind) {
    auto swapchain = device.create_swapchain(surface, {
        .extent = {32, 32},
        .framesInFlight = 2,
    });
    TRUFFLE_CHECK(swapchain.ok());
    TRUFFLE_CHECK(swapchain.value()->desc().extent.width == 32);
    TRUFFLE_CHECK(swapchain.value()->desc().extent.height == 32);
    TRUFFLE_CHECK(swapchain.value()->image_count() == 2);
    TRUFFLE_CHECK(swapchain.value()->desc().imageCount == 2);
    TRUFFLE_CHECK(!swapchain.value()->has_acquired_texture());
    auto firstAcquire = swapchain.value()->acquire_next_texture_result();
    TRUFFLE_CHECK(firstAcquire.ok());
    TRUFFLE_CHECK(firstAcquire.texture != nullptr);
    TRUFFLE_CHECK(firstAcquire.status.ok());
    TRUFFLE_CHECK(firstAcquire.imageIndex == 0);
    TRUFFLE_CHECK(!firstAcquire.suboptimal);
    TRUFFLE_CHECK(!firstAcquire.outOfDate);
    TRUFFLE_CHECK(swapchain.value()->has_acquired_texture());
    TRUFFLE_CHECK(swapchain.value()->current_image_index() == 0);
    TRUFFLE_CHECK(swapchain.value()->resize({64, 64}).ok());
    TRUFFLE_CHECK(swapchain.value()->desc().extent.width == 64);
    TRUFFLE_CHECK(swapchain.value()->desc().extent.height == 64);
    TRUFFLE_CHECK(!swapchain.value()->has_acquired_texture());
    auto reacquire = swapchain.value()->acquire_next_texture_result();
    TRUFFLE_CHECK(reacquire.ok());
    TRUFFLE_CHECK(reacquire.texture != nullptr);
    TRUFFLE_CHECK(reacquire.imageIndex == 0);
    TRUFFLE_CHECK(swapchain.value()->has_acquired_texture());

    auto uploadRing = device.create_upload_ring(2, 128);
    TRUFFLE_CHECK(uploadRing.ok());
    TRUFFLE_CHECK(uploadRing.value()->frames_in_flight() == 2);
    TRUFFLE_CHECK(uploadRing.value()->capacity_per_frame() == 128);
    auto alloc = uploadRing.value()->allocate(32, 16);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.buffer != nullptr);
    TRUFFLE_CHECK(alloc.mappedPtr != nullptr);
    TRUFFLE_CHECK(alloc.size == 32);
    auto uploadBackingMap = alloc.buffer->map();
    TRUFFLE_CHECK(uploadBackingMap.ok());
    TRUFFLE_CHECK(static_cast<void*>(
                      static_cast<std::byte*>(uploadBackingMap.value()) +
                      alloc.offset) == alloc.mappedPtr);
    TRUFFLE_CHECK(alloc.buffer->unmap().ok());
    TRUFFLE_CHECK(!uploadRing.value()->allocate(1, 0).valid());
    TRUFFLE_CHECK(!uploadRing.value()->allocate(1, 3).valid());
    auto alignedAlloc = uploadRing.value()->allocate(1, 64);
    TRUFFLE_CHECK(alignedAlloc.valid());
    TRUFFLE_CHECK((alignedAlloc.offset % 64) == 0);
    auto overflowAlloc = uploadRing.value()->allocate(1024, 16);
    TRUFFLE_CHECK(!overflowAlloc.valid());
    TRUFFLE_CHECK(uploadRing.value()->current_frame_index() == 0);
    auto unsignaledReuseFence = device.create_fence({.signaled = false});
    auto blockedAdvance = uploadRing.value()->advance_if_ready(*unsignaledReuseFence);
    TRUFFLE_CHECK(!blockedAdvance.ok());
    TRUFFLE_CHECK(blockedAdvance.code == truffle::core::StatusCode::timeout);
    TRUFFLE_CHECK(uploadRing.value()->current_frame_index() == 0);
    auto signaledReuseFence = device.create_fence({.signaled = true});
    TRUFFLE_CHECK(uploadRing.value()->advance_if_ready(*signaledReuseFence).ok());
    TRUFFLE_CHECK(uploadRing.value()->current_frame_index() == 1);
    uploadRing.value()->advance();
    TRUFFLE_CHECK(uploadRing.value()->current_frame_index() == 0);

    auto vertexShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::vertex));
    auto fragmentShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::fragment));
    auto computeShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::compute));
    TRUFFLE_CHECK(vertexShader.ok());
    TRUFFLE_CHECK(fragmentShader.ok());
    TRUFFLE_CHECK(computeShader.ok());

    const truffle::rhi::PipelineLayoutDesc graphicsLayout{
        .debugName = "contract_graphics_layout",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 1,
                .groupIndex = 1,
            },
        },
    };
    auto pipeline = device.create_pipeline({
        .cacheKey = 0xABCDEFu,
        .vertexShader = vertexShader.value().get(),
        .fragmentShader = fragmentShader.value().get(),
        .layout = graphicsLayout,
    });
    TRUFFLE_CHECK(pipeline.ok());
    TRUFFLE_CHECK(pipeline.value()->cache_key() == 0xABCDEFu);
    TRUFFLE_CHECK(verify_reflection_invariants(pipeline.value()->reflection(), false));

    const truffle::rhi::PipelineLayoutDesc dynamicGraphicsLayout{
        .debugName = "contract_dynamic_graphics_layout",
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .minBindingSize = 16,
            .dynamicOffset = true,
        }},
    };
    auto dynamicPipeline = device.create_pipeline({
        .cacheKey = 0xD1A0u,
        .vertexShader = vertexShader.value().get(),
        .fragmentShader = fragmentShader.value().get(),
        .layout = dynamicGraphicsLayout,
    });
    TRUFFLE_CHECK(dynamicPipeline.ok());
    const truffle::rhi::PipelineLayoutDesc explicitNativeSlotLayout{
        .debugName = "contract_explicit_native_slot_layout",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
                .groupIndex = 0,
                .nativeSlot = 0,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
                .groupIndex = 1,
                .nativeSlot = 3,
            },
        },
    };
    auto explicitNativePipeline = device.create_pipeline({
        .cacheKey = 0x51A07u,
        .vertexShader = vertexShader.value().get(),
        .fragmentShader = fragmentShader.value().get(),
        .layout = explicitNativeSlotLayout,
    });
    TRUFFLE_CHECK(explicitNativePipeline.ok());
    auto aliasedNativeSlotPipeline = device.create_pipeline({
        .vertexShader = vertexShader.value().get(),
        .fragmentShader = fragmentShader.value().get(),
        .layout = {
            .bindings = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .visibility = truffle::rhi::ShaderStageFlags::vertex,
                    .minBindingSize = 16,
                    .groupIndex = 0,
                },
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .visibility = truffle::rhi::ShaderStageFlags::vertex,
                    .minBindingSize = 16,
                    .groupIndex = 1,
                },
            },
        },
    });
    TRUFFLE_CHECK(!aliasedNativeSlotPipeline.ok());
    TRUFFLE_CHECK(aliasedNativeSlotPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    const truffle::rhi::PipelineLayoutDesc computeLayout{
        .debugName = "contract_compute_layout",
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::storage_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::compute,
            .minBindingSize = 16,
            .groupIndex = 1,
        }},
    };
    auto computePipeline = device.create_compute_pipeline({
        .cacheKey = 0x123456u,
        .computeShader = computeShader.value().get(),
        .layout = computeLayout,
    });
    TRUFFLE_CHECK(computePipeline.ok());
    TRUFFLE_CHECK(computePipeline.value()->cache_key() == 0x123456u);
    TRUFFLE_CHECK(verify_reflection_invariants(computePipeline.value()->reflection(), true));
    const auto extractedGraphicsGroup1 =
        truffle::rhi::pipeline_layout_bind_group_layout(graphicsLayout, 1);
    TRUFFLE_CHECK(extractedGraphicsGroup1.has_value());
    TRUFFLE_CHECK(extractedGraphicsGroup1->bindings.size() == 1);
    TRUFFLE_CHECK(extractedGraphicsGroup1->bindings[0].groupIndex == 0);
    TRUFFLE_CHECK(extractedGraphicsGroup1->bindings[0].type ==
                  truffle::rhi::BindingResourceType::sampled_texture);
    const auto graphicsLayoutBudget =
        truffle::rhi::pipeline_layout_descriptor_budget(
            graphicsLayout, device.capabilities());
    TRUFFLE_CHECK(graphicsLayoutBudget.bindGroupCount == 2);
    TRUFFLE_CHECK(graphicsLayoutBudget.maxBudgetPerBindGroup.model ==
                  device.capabilities().descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(graphicsLayoutBudget.maxBudgetPerBindGroup.totalUnits == 1);
    TRUFFLE_CHECK(graphicsLayoutBudget.totalBudget.totalUnits == 2);
    ForeignBuffer foreignBuffer;
    ForeignTexture foreignTexture;
    ForeignPipeline foreignPipeline;
    ForeignComputePipeline foreignComputePipeline;

    TRUFFLE_CHECK(device.queue(truffle::rhi::QueueKind::graphics).kind() ==
                  truffle::rhi::QueueKind::graphics);
    TRUFFLE_CHECK(device.queue(truffle::rhi::QueueKind::compute).kind() ==
                  truffle::rhi::QueueKind::compute);
    TRUFFLE_CHECK(device.queue(truffle::rhi::QueueKind::transfer).kind() ==
                  truffle::rhi::QueueKind::transfer);

    auto barrierBuffer = device.create_buffer({
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::transfer_source |
                      truffle::rhi::BufferUsageFlags::transfer_destination |
                      truffle::rhi::BufferUsageFlags::uniform |
                      truffle::rhi::BufferUsageFlags::storage,
    });
    const auto uniformDynamicAlignment =
        device.capabilities().limits.minUniformBufferOffsetAlignment;
    auto dynamicUniformBuffer = device.create_buffer({
        .size = uniformDynamicAlignment + 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
    });
    auto barrierWrongBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    auto barrierTexture = device.create_texture({
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::sampled |
                      truffle::rhi::TextureUsageFlags::color_attachment |
                      truffle::rhi::TextureUsageFlags::transfer_source |
                      truffle::rhi::TextureUsageFlags::transfer_destination,
    });
    TRUFFLE_CHECK(barrierBuffer.ok());
    TRUFFLE_CHECK(dynamicUniformBuffer.ok());
    TRUFFLE_CHECK(barrierWrongBuffer.ok());
    TRUFFLE_CHECK(barrierTexture.ok());
    auto foreignComputeRecoveryCmd = device.create_command_buffer();
    TRUFFLE_CHECK(foreignComputeRecoveryCmd != nullptr);
    TRUFFLE_CHECK(foreignComputeRecoveryCmd->begin().ok());
    TRUFFLE_CHECK(!foreignComputeRecoveryCmd
                       ->bind_compute_pipeline(foreignComputePipeline)
                       .ok());
    truffle::rhi::RenderPassDesc recoveryPassDesc;
    recoveryPassDesc.extent = {32, 32};
    recoveryPassDesc.colorAttachment.texture = barrierTexture.value().get();
    TRUFFLE_CHECK(foreignComputeRecoveryCmd->begin_render_pass(recoveryPassDesc).ok());
    TRUFFLE_CHECK(foreignComputeRecoveryCmd->end_render_pass().ok());
    TRUFFLE_CHECK(foreignComputeRecoveryCmd->end().ok());
    const truffle::rhi::BindGroupLayoutDesc graphicsBindGroupLayoutDesc{
        .debugName = "contract_graphics_bind_group_layout",
        .bindings = {graphicsLayout.bindings[0]},
    };
    auto graphicsBindGroupLayout =
        device.create_bind_group_layout(graphicsBindGroupLayoutDesc);
    TRUFFLE_CHECK(graphicsBindGroupLayout.ok());
    auto graphicsBindGroup = device.create_bind_group({
        .debugName = "contract_graphics_bind_group",
        .layout = graphicsBindGroupLayout.value().get(),
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = barrierBuffer.value().get(), .size = 16},
            },
        },
    });
    TRUFFLE_CHECK(graphicsBindGroup.ok());
    auto dynamicGraphicsBindGroupLayout = device.create_bind_group_layout({
        .debugName = "contract_dynamic_graphics_bind_group_layout",
        .bindings = dynamicGraphicsLayout.bindings,
    });
    TRUFFLE_CHECK(dynamicGraphicsBindGroupLayout.ok());
    auto dynamicGraphicsBindGroup = device.create_bind_group({
        .debugName = "contract_dynamic_graphics_bind_group",
        .layout = dynamicGraphicsBindGroupLayout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = dynamicUniformBuffer.value().get(), .size = 16},
        }},
    });
    TRUFFLE_CHECK(dynamicGraphicsBindGroup.ok());
    auto explicitNativeBindGroup0Layout = device.create_bind_group_layout({
        .debugName = "contract_explicit_native_slot_group0_layout",
        .bindings = {explicitNativeSlotLayout.bindings[0]},
    });
    auto explicitNativeBindGroup1Layout = device.create_bind_group_layout({
        .debugName = "contract_explicit_native_slot_group1_layout",
        .bindings = {explicitNativeSlotLayout.bindings[1]},
    });
    TRUFFLE_CHECK(explicitNativeBindGroup0Layout.ok());
    TRUFFLE_CHECK(explicitNativeBindGroup1Layout.ok());
    auto explicitNativeBindGroup0 = device.create_bind_group({
        .debugName = "contract_explicit_native_slot_group0",
        .layout = explicitNativeBindGroup0Layout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = barrierBuffer.value().get(), .size = 16},
        }},
    });
    auto explicitNativeBindGroup1 = device.create_bind_group({
        .debugName = "contract_explicit_native_slot_group1",
        .layout = explicitNativeBindGroup1Layout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = dynamicUniformBuffer.value().get(), .size = 16},
        }},
    });
    TRUFFLE_CHECK(explicitNativeBindGroup0.ok());
    TRUFFLE_CHECK(explicitNativeBindGroup1.ok());
    const truffle::rhi::BindGroupLayoutDesc graphicsTextureBindGroupLayoutDesc{
        .debugName = "contract_graphics_texture_bind_group_layout",
        .bindings = {graphicsLayout.bindings[1]},
    };
    auto graphicsTextureBindGroupLayout =
        device.create_bind_group_layout(graphicsTextureBindGroupLayoutDesc);
    TRUFFLE_CHECK(graphicsTextureBindGroupLayout.ok());
    auto graphicsTextureBindGroup = device.create_bind_group({
        .debugName = "contract_graphics_texture_bind_group",
        .layout = graphicsTextureBindGroupLayout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::sampled_texture,
            .texture = barrierTexture.value().get(),
        }},
    });
    TRUFFLE_CHECK(graphicsTextureBindGroup.ok());

    auto commandBuffer = device.create_command_buffer();
    TRUFFLE_CHECK(commandBuffer != nullptr);
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::initial);
    TRUFFLE_CHECK(commandBuffer->begin().ok());
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::recording);
    TRUFFLE_CHECK(!commandBuffer->bind_group(0, *graphicsBindGroup.value()).ok());
    TRUFFLE_CHECK(!commandBuffer->resource_barrier(
        truffle::rhi::BufferBarrierDesc{}).ok());
    TRUFFLE_CHECK(commandBuffer->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = barrierBuffer.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::copy_destination,
        }).ok());
    TRUFFLE_CHECK(commandBuffer->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = barrierBuffer.value().get(),
            .before = truffle::rhi::ResourceState::copy_destination,
            .after = truffle::rhi::ResourceState::shader_read,
        }).ok());
    TRUFFLE_CHECK(!commandBuffer->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = barrierWrongBuffer.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::copy_destination,
        }).ok());
    TRUFFLE_CHECK(!commandBuffer->resource_barrier(
        truffle::rhi::TextureBarrierDesc{}).ok());
    TRUFFLE_CHECK(commandBuffer->resource_barrier(
        truffle::rhi::TextureBarrierDesc{
            .texture = barrierTexture.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::copy_destination,
        }).ok());
    TRUFFLE_CHECK(commandBuffer->resource_barrier(
        truffle::rhi::TextureBarrierDesc{
            .texture = barrierTexture.value().get(),
            .before = truffle::rhi::ResourceState::copy_destination,
            .after = truffle::rhi::ResourceState::shader_read,
        }).ok());
    TRUFFLE_CHECK(swapchain.value()->has_acquired_texture());
    TRUFFLE_CHECK(swapchain.value()->schedule_present(*commandBuffer).ok());
    TRUFFLE_CHECK(!swapchain.value()->has_acquired_texture());
    TRUFFLE_CHECK(commandBuffer->end().ok());
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::executable);
    auto latePresent = swapchain.value()->schedule_present(*commandBuffer);
    TRUFFLE_CHECK(!latePresent.ok());
    TRUFFLE_CHECK(latePresent.code == truffle::core::StatusCode::invalid_state);

    auto submitFence = device.create_fence({.signaled = false});
    TRUFFLE_CHECK(!submitFence->signaled());
    TRUFFLE_CHECK(submitFence->value() == 0);
    auto timeout = submitFence->wait_for(0);
    TRUFFLE_CHECK(!timeout.ok());
    TRUFFLE_CHECK(timeout.code == truffle::core::StatusCode::timeout);
    auto timelineTimeout = submitFence->wait_for_value(1, 0);
    TRUFFLE_CHECK(!timelineTimeout.ok());
    TRUFFLE_CHECK(timelineTimeout.code == truffle::core::StatusCode::timeout);
    const auto targetFenceValue = submitFence->value() + 1;
    TRUFFLE_CHECK(device.queue(truffle::rhi::QueueKind::graphics)
                      .submit(*commandBuffer, submitFence.get())
                      .ok());
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::submitted);
    submitFence->wait();
    TRUFFLE_CHECK(submitFence->signaled());
    TRUFFLE_CHECK(submitFence->value() >= targetFenceValue);
    TRUFFLE_CHECK(submitFence->wait_for_value(targetFenceValue, 0).ok());
    TRUFFLE_CHECK(submitFence->wait_for(0).ok());
    TRUFFLE_CHECK(submitFence->reset().ok());
    TRUFFLE_CHECK(!submitFence->signaled());
    TRUFFLE_CHECK(submitFence->value() == 0);
    auto duplicateSubmit =
        device.queue(truffle::rhi::QueueKind::graphics).submit(*commandBuffer);
    TRUFFLE_CHECK(!duplicateSubmit.ok());
    TRUFFLE_CHECK(duplicateSubmit.code == truffle::core::StatusCode::invalid_state);

    auto initializedFence = device.create_fence({.initialValue = 7});
    TRUFFLE_CHECK(initializedFence->signaled());
    TRUFFLE_CHECK(initializedFence->value() == 7);
    TRUFFLE_CHECK(initializedFence->wait_for_value(7, 0).ok());

    auto notReady = device.create_command_buffer();
    TRUFFLE_CHECK(notReady != nullptr);
    TRUFFLE_CHECK(notReady->state() == truffle::rhi::CommandBufferState::initial);
    auto invalidSubmit =
        device.queue(truffle::rhi::QueueKind::graphics).submit(*notReady, nullptr);
    TRUFFLE_CHECK(!invalidSubmit.ok());
    TRUFFLE_CHECK(invalidSubmit.code == truffle::core::StatusCode::invalid_state);

    auto labelCmd = device.create_command_buffer();
    TRUFFLE_CHECK(labelCmd != nullptr);
    TRUFFLE_CHECK(!labelCmd->push_debug_label({.name = "before_begin"}).ok());
    TRUFFLE_CHECK(labelCmd->begin().ok());
    TRUFFLE_CHECK(!labelCmd->push_debug_label({}).ok());
    TRUFFLE_CHECK(!labelCmd->insert_debug_marker({
        .name = "bad_marker_color",
        .hasColor = true,
        .red = -0.1f,
    }).ok());
    TRUFFLE_CHECK(labelCmd->push_debug_label({
        .name = "frame",
        .hasColor = true,
        .red = 0.1f,
        .green = 0.2f,
        .blue = 0.3f,
        .alpha = 1.0f,
    }).ok());
    TRUFFLE_CHECK(labelCmd->insert_debug_marker({.name = "after_upload"}).ok());
    TRUFFLE_CHECK(!labelCmd->end().ok());
    TRUFFLE_CHECK(labelCmd->pop_debug_label().ok());
    TRUFFLE_CHECK(!labelCmd->pop_debug_label().ok());
    TRUFFLE_CHECK(labelCmd->end().ok());

    auto stateCmd = device.create_command_buffer();
    TRUFFLE_CHECK(stateCmd != nullptr);
    TRUFFLE_CHECK(!stateCmd->draw(3).ok());
    TRUFFLE_CHECK(stateCmd->begin().ok());
    TRUFFLE_CHECK(!stateCmd->draw(3).ok());
    auto vertexUniformBuffer = device.create_buffer({
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::vertex |
                      truffle::rhi::BufferUsageFlags::uniform,
    });
    auto indexBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::index,
    });
    auto storageBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::storage,
    });
    auto indirectBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::indirect,
    });
    TRUFFLE_CHECK(vertexUniformBuffer.ok());
    TRUFFLE_CHECK(indexBuffer.ok());
    TRUFFLE_CHECK(storageBuffer.ok());
    TRUFFLE_CHECK(indirectBuffer.ok());
    TRUFFLE_CHECK(!stateCmd->set_viewport(0.0f, 0.0f, 16.0f, 16.0f).ok());
    TRUFFLE_CHECK(!stateCmd->set_scissor(0, 0, 16, 16).ok());
    const truffle::rhi::BindGroupLayoutDesc computeBindGroupLayoutDesc{
        .debugName = "contract_compute_bind_group_layout",
        .bindings = computeLayout.bindings,
    };
    auto computeBindGroupLayout =
        device.create_bind_group_layout(computeBindGroupLayoutDesc);
    TRUFFLE_CHECK(computeBindGroupLayout.ok());
    auto computeBindGroup = device.create_bind_group({
        .debugName = "contract_compute_bind_group",
        .layout = computeBindGroupLayout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::storage_buffer,
            .buffer = {.buffer = storageBuffer.value().get(), .size = 16},
        }},
    });
    TRUFFLE_CHECK(computeBindGroup.ok());
    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = swapchain.value()->desc().extent;
    truffle::rhi::RenderPassDesc foreignPassDesc;
    foreignPassDesc.extent = passDesc.extent;
    foreignPassDesc.colorAttachment.texture = &foreignTexture;
    TRUFFLE_CHECK(!stateCmd->begin_render_pass(foreignPassDesc).ok());
    passDesc.colorAttachment.texture = swapchain.value()->acquire_next_texture();
    TRUFFLE_CHECK(stateCmd->begin_render_pass(passDesc).ok());
    TRUFFLE_CHECK(!stateCmd->begin_render_pass(passDesc).ok());
    TRUFFLE_CHECK(!stateCmd->set_viewport(
        0.0f, 0.0f, 0.0f, 16.0f).ok());
    TRUFFLE_CHECK(!stateCmd->set_viewport(
        0.0f, 0.0f, 16.0f, 16.0f, 0.75f, 0.25f).ok());
    TRUFFLE_CHECK(stateCmd->set_viewport(
        0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f).ok());
    TRUFFLE_CHECK(!stateCmd->set_scissor(0, 0, 0, 16).ok());
    TRUFFLE_CHECK(stateCmd->set_scissor(0, 0, 16, 16).ok());
    TRUFFLE_CHECK(!stateCmd->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = storageBuffer.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::storage_read_write,
        }).ok());
    TRUFFLE_CHECK(!stateCmd->bind_pipeline(foreignPipeline).ok());
    TRUFFLE_CHECK(stateCmd->bind_pipeline(*dynamicPipeline.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_group(
        0, *dynamicGraphicsBindGroup.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_group(
        0,
        *dynamicGraphicsBindGroup.value(),
        {{.bindingIndex = 0, .offset = uniformDynamicAlignment + 56}}).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(
        0,
        *dynamicGraphicsBindGroup.value(),
        {{.bindingIndex = 0, .offset = uniformDynamicAlignment}}).ok());
    TRUFFLE_CHECK(stateCmd->draw(3).ok());
    TRUFFLE_CHECK(stateCmd->bind_pipeline(*explicitNativePipeline.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(0, *explicitNativeBindGroup0.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(1, *explicitNativeBindGroup1.value()).ok());
    TRUFFLE_CHECK(stateCmd->draw(3).ok());
    TRUFFLE_CHECK(stateCmd->bind_pipeline(*pipeline.value()).ok());
    TRUFFLE_CHECK(!stateCmd->draw(3).ok());
    TRUFFLE_CHECK(!stateCmd->bind_group(1, *graphicsBindGroup.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(0, *graphicsBindGroup.value()).ok());
    TRUFFLE_CHECK(!stateCmd->draw_indirect(*indirectBuffer.value(), 0).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(1, *graphicsTextureBindGroup.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_vertex_buffer(0, foreignBuffer).ok());
    TRUFFLE_CHECK(!stateCmd->bind_vertex_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_vertex_buffer(0, *vertexUniformBuffer.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_uniform_buffer(0, foreignBuffer).ok());
    TRUFFLE_CHECK(!stateCmd->bind_uniform_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_uniform_buffer(0, *vertexUniformBuffer.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_index_buffer(foreignBuffer).ok());
    TRUFFLE_CHECK(stateCmd->bind_index_buffer(*indexBuffer.value()).ok());
    TRUFFLE_CHECK(!stateCmd->draw_indirect(*indexBuffer.value(), 0).ok());
    TRUFFLE_CHECK(stateCmd->draw_indirect(*indirectBuffer.value(), 0).ok());
    TRUFFLE_CHECK(!stateCmd->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(!stateCmd->end().ok());
    TRUFFLE_CHECK(stateCmd->end_render_pass().ok());
    TRUFFLE_CHECK(!stateCmd->end_render_pass().ok());
    TRUFFLE_CHECK(!stateCmd->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = &foreignBuffer,
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::copy_destination,
        }).ok());
    TRUFFLE_CHECK(!stateCmd->resource_barrier(
        truffle::rhi::TextureBarrierDesc{
            .texture = &foreignTexture,
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::copy_destination,
        }).ok());
    TRUFFLE_CHECK(!stateCmd->bind_compute_pipeline(foreignComputePipeline).ok());
    TRUFFLE_CHECK(stateCmd->bind_compute_pipeline(*computePipeline.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_group(0, *computeBindGroup.value()).ok());
    TRUFFLE_CHECK(!stateCmd->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(1, *computeBindGroup.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_storage_buffer(0, foreignBuffer).ok());
    TRUFFLE_CHECK(!stateCmd->bind_storage_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_storage_buffer(0, *storageBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(stateCmd->end().ok());

    return 0;
}

bool has_event_kind(const std::vector<truffle::rhi::BackendEvent>& events,
                    truffle::rhi::BackendEventKind kind) {
    for (const auto& event : events) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

std::uint64_t total_memory_budget(
    const std::vector<truffle::rhi::MemoryHeapInfo>& heaps) {
    std::uint64_t total = 0;
    for (const auto& heap : heaps) {
        total += heap.budgetBytes;
    }
    return total;
}

bool has_dedicated_memory_heap(
    const std::vector<truffle::rhi::MemoryHeapInfo>& heaps) {
    for (const auto& heap : heaps) {
        if (heap.dedicated) {
            return true;
        }
    }
    return false;
}

int verify_backend_diagnostics_contract(truffle::rhi::IBackend& backend) {
    const auto stats = backend.backend_stats();
    TRUFFLE_CHECK(stats.devicesCreated == 1);
    TRUFFLE_CHECK(stats.buffersCreated > 0);
    TRUFFLE_CHECK(stats.texturesCreated > 0);
    TRUFFLE_CHECK(stats.samplersCreated > 0);
    TRUFFLE_CHECK(stats.shadersCreated >= 3);
    TRUFFLE_CHECK(stats.graphicsPipelinesCreated > 0);
    TRUFFLE_CHECK(stats.computePipelinesCreated > 0);
    TRUFFLE_CHECK(stats.bindGroupLayoutsCreated > 0);
    TRUFFLE_CHECK(stats.bindGroupsCreated > 0);
    TRUFFLE_CHECK(stats.surfacesCreated > 0);
    TRUFFLE_CHECK(stats.swapchainsCreated > 0);
    TRUFFLE_CHECK(stats.commandBuffersCreated > 0);
    TRUFFLE_CHECK(stats.fencesCreated > 0);
    TRUFFLE_CHECK(stats.uploadRingsCreated > 0);
    TRUFFLE_CHECK(stats.drawsRecorded > 0);
    TRUFFLE_CHECK(stats.dispatchesRecorded > 0);
    TRUFFLE_CHECK(stats.submissions > 0);
    TRUFFLE_CHECK(stats.debugLabelsPushed > 0);
    TRUFFLE_CHECK(stats.debugMarkersInserted > 0);

    const auto events = backend.recent_events();
    TRUFFLE_CHECK(!events.empty());
    std::uint64_t previousSequence = 0;
    for (const auto& event : events) {
        TRUFFLE_CHECK(event.backend == backend.kind());
        TRUFFLE_CHECK(event.status == truffle::core::StatusCode::ok);
        TRUFFLE_CHECK(event.sequence > previousSequence);
        previousSequence = event.sequence;
    }
    TRUFFLE_CHECK(has_event_kind(events, truffle::rhi::BackendEventKind::command_recorded));
    TRUFFLE_CHECK(has_event_kind(events, truffle::rhi::BackendEventKind::debug_marker));
    TRUFFLE_CHECK(has_event_kind(events, truffle::rhi::BackendEventKind::submitted));

    const auto report = truffle::rhi::collect_backend_parity_report(backend);
    const auto reportedCaps = backend.enumerate_adapters().front().capabilities;
    TRUFFLE_CHECK(report.backend == backend.kind());
    TRUFFLE_CHECK(report.adapterCount == backend.enumerate_adapters().size());
    TRUFFLE_CHECK(report.graphicsQueue == reportedCaps.queues.graphics);
    TRUFFLE_CHECK(report.computeQueue == reportedCaps.queues.compute);
    TRUFFLE_CHECK(report.transferQueue == reportedCaps.queues.transfer);
    TRUFFLE_CHECK(report.presentation == reportedCaps.features.presentation);
    TRUFFLE_CHECK(report.nativeSurface == reportedCaps.features.nativeSurface);
    TRUFFLE_CHECK(report.debugLabels == reportedCaps.features.debugLabels);
    TRUFFLE_CHECK(report.descriptorArrays ==
                  truffle::rhi::supports_descriptor_arrays(reportedCaps));
    TRUFFLE_CHECK(report.dynamicResourceIndexing ==
                  truffle::rhi::supports_dynamic_resource_indexing(reportedCaps));
    TRUFFLE_CHECK(report.bindlessResources ==
                  truffle::rhi::supports_bindless_resources(reportedCaps));
    TRUFFLE_CHECK(report.maxFramesInFlight >= 1);
    TRUFFLE_CHECK(report.maxResourceBindings > 0);
    TRUFFLE_CHECK(report.maxVertexAttributes ==
                  reportedCaps.limits.maxVertexAttributes);
    TRUFFLE_CHECK(report.maxVertexBufferStride ==
                  reportedCaps.limits.maxVertexBufferStride);
    TRUFFLE_CHECK(report.maxDescriptorArrayElements ==
                  reportedCaps.limits.maxDescriptorArrayElements);
    TRUFFLE_CHECK(report.maxBindlessResources ==
                  reportedCaps.limits.maxBindlessResources);
    TRUFFLE_CHECK(report.maxSamplerAnisotropy ==
                  reportedCaps.limits.maxSamplerAnisotropy);
    TRUFFLE_CHECK(report.unifiedMemory == reportedCaps.features.unifiedMemory);
    TRUFFLE_CHECK(report.descriptorMappingModel ==
                  reportedCaps.descriptorPolicy.mappingModel);
    TRUFFLE_CHECK(report.descriptorAllocationModel ==
                  reportedCaps.descriptorPolicy.allocationModel);
    TRUFFLE_CHECK(report.descriptorUpdateModel ==
                  reportedCaps.descriptorPolicy.updateModel);
    TRUFFLE_CHECK(report.descriptorBudgetModel ==
                  reportedCaps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(report.flattenedNativeBindings ==
                  reportedCaps.descriptorPolicy.flattenedNativeBindings);
    TRUFFLE_CHECK(report.memoryHeapCount == reportedCaps.memoryHeaps.size());
    TRUFFLE_CHECK(report.memoryBudgetBytes ==
                  total_memory_budget(reportedCaps.memoryHeaps));
    TRUFFLE_CHECK(report.dedicatedMemoryHeap ==
                  has_dedicated_memory_heap(reportedCaps.memoryHeaps));
    TRUFFLE_CHECK(report.formatCount > 0);
    TRUFFLE_CHECK(report.shaderFormatCount > 0);
    TRUFFLE_CHECK(report.stats.devicesCreated == stats.devicesCreated);
    TRUFFLE_CHECK(report.stats.submissions == stats.submissions);

    backend.clear_diagnostics();
    const auto clearedStats = backend.backend_stats();
    TRUFFLE_CHECK(clearedStats.devicesCreated == 0);
    TRUFFLE_CHECK(clearedStats.buffersCreated == 0);
    TRUFFLE_CHECK(clearedStats.submissions == 0);
    TRUFFLE_CHECK(backend.recent_events().empty());
    const auto clearedReport = truffle::rhi::collect_backend_parity_report(backend);
    TRUFFLE_CHECK(clearedReport.stats.devicesCreated == 0);
    TRUFFLE_CHECK(clearedReport.stats.submissions == 0);

    return 0;
}

int verify_common_device_contract(truffle::rhi::IDevice& device,
                                    truffle::rhi::BackendKind backendKind) {
    const auto& caps = device.capabilities();

    auto badBuffer = device.create_buffer({
        .size = 0,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(!badBuffer.ok());
    TRUFFLE_CHECK(badBuffer.status().code == truffle::core::StatusCode::invalid_argument);

    auto tooLargeBuffer = device.create_buffer({
        .size = caps.limits.maxBufferSize + 1,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(!tooLargeBuffer.ok());
    TRUFFLE_CHECK(tooLargeBuffer.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto goodBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(goodBuffer.ok());
    TRUFFLE_CHECK(truffle::rhi::validation::buffer_view_valid({
        .buffer = goodBuffer.value().get(),
        .offset = 16,
        .size = 16,
        .requiredUsage = truffle::rhi::BufferUsageFlags::vertex,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::buffer_view_valid({
        .buffer = goodBuffer.value().get(),
        .offset = 64,
        .size = 1,
        .requiredUsage = truffle::rhi::BufferUsageFlags::vertex,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::buffer_view_valid({
        .buffer = goodBuffer.value().get(),
        .offset = 0,
        .size = 16,
        .requiredUsage = truffle::rhi::BufferUsageFlags::storage,
    }));
    if (truffle::rhi::validation::memory_domain_supported(
            truffle::rhi::MemoryDomain::upload, caps)) {
        auto mappedBuffer = device.create_buffer({
            .size = 64,
            .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
            .memory = truffle::rhi::MemoryDomain::upload,
            .mappedAtCreation = true,
        });
        TRUFFLE_CHECK(mappedBuffer.ok());
        TRUFFLE_CHECK(mappedBuffer.value()->mapped());
        TRUFFLE_CHECK(mappedBuffer.value()->mapped_data() != nullptr);
        auto doubleMap = mappedBuffer.value()->map();
        TRUFFLE_CHECK(!doubleMap.ok());
        TRUFFLE_CHECK(doubleMap.status().code ==
                      truffle::core::StatusCode::invalid_state);
        TRUFFLE_CHECK(mappedBuffer.value()->unmap().ok());
        TRUFFLE_CHECK(!mappedBuffer.value()->mapped());
        TRUFFLE_CHECK(mappedBuffer.value()->mapped_data() == nullptr);
        auto remap = mappedBuffer.value()->map();
        TRUFFLE_CHECK(remap.ok());
        TRUFFLE_CHECK(remap.value() != nullptr);
        TRUFFLE_CHECK(mappedBuffer.value()->mapped_data() == remap.value());
        auto repeatedMap = mappedBuffer.value()->map();
        TRUFFLE_CHECK(!repeatedMap.ok());
        TRUFFLE_CHECK(repeatedMap.status().code ==
                      truffle::core::StatusCode::invalid_state);
        TRUFFLE_CHECK(mappedBuffer.value()->unmap().ok());
        TRUFFLE_CHECK(!mappedBuffer.value()->unmap().ok());
    }
    if (truffle::rhi::validation::memory_domain_supported(
            truffle::rhi::MemoryDomain::device_local, caps)) {
        auto mappedDeviceLocalBuffer = device.create_buffer({
            .size = 64,
            .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
            .memory = truffle::rhi::MemoryDomain::device_local,
            .mappedAtCreation = true,
        });
        TRUFFLE_CHECK(!mappedDeviceLocalBuffer.ok());
        TRUFFLE_CHECK(mappedDeviceLocalBuffer.status().code ==
                      truffle::core::StatusCode::invalid_argument);
        auto deviceLocalBuffer = device.create_buffer({
            .size = 64,
            .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
            .memory = truffle::rhi::MemoryDomain::device_local,
        });
        TRUFFLE_CHECK(deviceLocalBuffer.ok());
        auto deviceLocalMap = deviceLocalBuffer.value()->map();
        TRUFFLE_CHECK(!deviceLocalMap.ok());
        TRUFFLE_CHECK(deviceLocalMap.status().code ==
                      truffle::core::StatusCode::unsupported);
    }

    auto badTexture = device.create_texture({
        .extent = {0, 0},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
    });
    TRUFFLE_CHECK(!badTexture.ok());
    TRUFFLE_CHECK(badTexture.status().code == truffle::core::StatusCode::invalid_argument);

    auto oversizedTexture = device.create_texture({
        .extent = {caps.limits.maxTextureDimension2D + 1,
                   caps.limits.maxTextureDimension2D + 1},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
    });
    TRUFFLE_CHECK(!oversizedTexture.ok());
    TRUFFLE_CHECK(oversizedTexture.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badTextureShape = device.create_texture({
        .extent = {32, 32},
        .mipLevels = 0,
    });
    TRUFFLE_CHECK(!badTextureShape.ok());
    TRUFFLE_CHECK(badTextureShape.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badTextureUsage = device.create_texture({
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::depth32_float,
        .usageFlags = truffle::rhi::TextureUsageFlags::color_attachment,
    });
    TRUFFLE_CHECK(!badTextureUsage.ok());
    TRUFFLE_CHECK(badTextureUsage.status().code ==
                  truffle::core::StatusCode::unsupported);

    auto goodTexture = device.create_texture({
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .usageFlags = truffle::rhi::TextureUsageFlags::sampled |
                      truffle::rhi::TextureUsageFlags::color_attachment,
    });
    TRUFFLE_CHECK(goodTexture.ok());
    auto depthTexture = device.create_texture({
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::depth32_float,
        .usageFlags = truffle::rhi::TextureUsageFlags::depth_stencil,
    });
    TRUFFLE_CHECK(depthTexture.ok());
    auto depthStencilTexture = device.create_texture({
        .extent = {32, 32},
        .format = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .usageFlags = truffle::rhi::TextureUsageFlags::depth_stencil,
    });
    TRUFFLE_CHECK(depthStencilTexture.ok());
    TRUFFLE_CHECK(truffle::rhi::validation::texture_view_valid({
        .texture = goodTexture.value().get(),
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .dimension = truffle::rhi::TextureDimension::two_d,
        .range = {},
        .requiredUsage = truffle::rhi::TextureUsageFlags::sampled,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_view_valid({
        .texture = goodTexture.value().get(),
        .format = truffle::rhi::TextureFormat::depth32_float,
        .dimension = truffle::rhi::TextureDimension::two_d,
        .range = {},
        .requiredUsage = truffle::rhi::TextureUsageFlags::sampled,
    }));

    auto badBindGroupLayout = device.create_bind_group_layout({
        .bindings = {{
            .bindingIndex = caps.limits.maxResourceBindings,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
        }},
    });
    TRUFFLE_CHECK(!badBindGroupLayout.ok());
    TRUFFLE_CHECK(badBindGroupLayout.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto overlappingBindGroupLayout = device.create_bind_group_layout({
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
    });
    TRUFFLE_CHECK(!overlappingBindGroupLayout.ok());
    TRUFFLE_CHECK(overlappingBindGroupLayout.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto aliasedNativeSlotLayout = device.create_bind_group_layout({
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::storage_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .nativeSlot = 0,
            },
        },
    });
    TRUFFLE_CHECK(!aliasedNativeSlotLayout.ok());
    TRUFFLE_CHECK(aliasedNativeSlotLayout.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto overflowingNativeSlotLayout = device.create_bind_group_layout({
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::vertex,
            .arrayCount = 2,
            .nativeSlot = caps.limits.maxResourceBindings - 1,
        }},
    });
    TRUFFLE_CHECK(!overflowingNativeSlotLayout.ok());
    TRUFFLE_CHECK(overflowingNativeSlotLayout.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    if (!truffle::rhi::supports_descriptor_arrays(caps)) {
        auto descriptorArrayLayout = device.create_bind_group_layout({
            .bindings = {{
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
            }},
        });
        TRUFFLE_CHECK(!descriptorArrayLayout.ok());
        TRUFFLE_CHECK(descriptorArrayLayout.status().code ==
                      truffle::core::StatusCode::invalid_argument);
    }
    if (!truffle::rhi::supports_dynamic_resource_indexing(caps)) {
        auto dynamicIndexingLayout = device.create_bind_group_layout({
            .bindings = {{
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
                .dynamicIndexing = true,
            }},
        });
        TRUFFLE_CHECK(!dynamicIndexingLayout.ok());
        TRUFFLE_CHECK(dynamicIndexingLayout.status().code ==
                      truffle::core::StatusCode::invalid_argument);
    }
    if (!truffle::rhi::supports_bindless_resources(caps)) {
        auto bindlessLayout = device.create_bind_group_layout({
            .bindings = {{
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 2,
                .dynamicIndexing = true,
                .bindless = true,
            }},
        });
        TRUFFLE_CHECK(!bindlessLayout.ok());
        TRUFFLE_CHECK(bindlessLayout.status().code ==
                      truffle::core::StatusCode::invalid_argument);
    }

    auto bindGroupUniformBuffer = device.create_buffer({
        .size = 64,
        .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
    });
    auto bindGroupSampler = device.create_sampler({});
    TRUFFLE_CHECK(bindGroupUniformBuffer.ok());
    TRUFFLE_CHECK(bindGroupSampler.ok());
    TRUFFLE_CHECK(truffle::rhi::effective_min_filter(
                      bindGroupSampler.value()->desc()) ==
                  truffle::rhi::SamplerFilter::linear);
    auto nearestSampler = device.create_sampler({
        .linear_filtering = false,
        .debugName = "contract_nearest_sampler",
    });
    TRUFFLE_CHECK(nearestSampler.ok());
    TRUFFLE_CHECK(truffle::rhi::effective_min_filter(
                      nearestSampler.value()->desc()) ==
                  truffle::rhi::SamplerFilter::nearest);
    auto richSampler = device.create_sampler({
        .minFilter = truffle::rhi::SamplerFilter::nearest,
        .magFilter = truffle::rhi::SamplerFilter::linear,
        .mipmapMode = truffle::rhi::SamplerMipmapMode::nearest,
        .addressModeU = truffle::rhi::SamplerAddressMode::repeat,
        .addressModeV = truffle::rhi::SamplerAddressMode::mirrored_repeat,
        .addressModeW = truffle::rhi::SamplerAddressMode::clamp_to_border,
        .minLod = 0.0f,
        .maxLod = 4.0f,
        .maxAnisotropy = caps.limits.maxSamplerAnisotropy,
        .compareEnabled = true,
        .compareOp = truffle::rhi::SamplerCompareOp::less_equal,
        .borderColor = truffle::rhi::SamplerBorderColor::opaque_white,
        .debugName = "contract_rich_sampler",
    });
    TRUFFLE_CHECK(richSampler.ok());
    TRUFFLE_CHECK(richSampler.value()->desc().maxAnisotropy ==
                  caps.limits.maxSamplerAnisotropy);
    auto zeroAnisotropySampler = device.create_sampler({
        .maxAnisotropy = 0,
    });
    TRUFFLE_CHECK(!zeroAnisotropySampler.ok());
    TRUFFLE_CHECK(zeroAnisotropySampler.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto excessiveAnisotropySampler = device.create_sampler({
        .maxAnisotropy = caps.limits.maxSamplerAnisotropy + 1,
    });
    TRUFFLE_CHECK(!excessiveAnisotropySampler.ok());
    TRUFFLE_CHECK(excessiveAnisotropySampler.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto invalidLodSampler = device.create_sampler({
        .minLod = 2.0f,
        .maxLod = 1.0f,
    });
    TRUFFLE_CHECK(!invalidLodSampler.ok());
    TRUFFLE_CHECK(invalidLodSampler.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto bindGroupLayout = device.create_bind_group_layout({
        .debugName = "contract_negative_bind_group_layout",
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
    });
    TRUFFLE_CHECK(bindGroupLayout.ok());
    TRUFFLE_CHECK(bindGroupLayout.value()->cache_key() == 0xB100u);
    const auto layoutFootprint = bindGroupLayout.value()->descriptor_footprint();
    TRUFFLE_CHECK(layoutFootprint.bindingCount == 3);
    TRUFFLE_CHECK(layoutFootprint.descriptorCount == 3);
    TRUFFLE_CHECK(layoutFootprint.dynamicOffsetCount == 0);
    TRUFFLE_CHECK(layoutFootprint.bufferDescriptorCount == 1);
    TRUFFLE_CHECK(layoutFootprint.textureDescriptorCount == 1);
    TRUFFLE_CHECK(layoutFootprint.samplerDescriptorCount == 1);
    TRUFFLE_CHECK(layoutFootprint.bufferSlots.firstSlot == 0);
    TRUFFLE_CHECK(layoutFootprint.bufferSlots.slotCount == 1);
    TRUFFLE_CHECK(layoutFootprint.textureSlots.firstSlot == 1);
    TRUFFLE_CHECK(layoutFootprint.textureSlots.slotCount == 1);
    TRUFFLE_CHECK(layoutFootprint.samplerSlots.firstSlot == 2);
    TRUFFLE_CHECK(layoutFootprint.samplerSlots.slotCount == 1);
    auto validBindGroup = device.create_bind_group({
        .cacheKey = 0xB101u,
        .layout = bindGroupLayout.value().get(),
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(validBindGroup.ok());
    TRUFFLE_CHECK(validBindGroup.value()->cache_key() == 0xB101u);
    TRUFFLE_CHECK(validBindGroup.value()->allocation_policy() ==
                  truffle::rhi::BindGroupAllocationPolicy::persistent);
    TRUFFLE_CHECK(validBindGroup.value()->allocation_frame_index() == 0);
    TRUFFLE_CHECK(validBindGroup.value()->reuse_hint() ==
                  truffle::rhi::BindGroupReuseHint::stable);
    const auto stableStrategy = truffle::rhi::bind_group_descriptor_strategy(
        validBindGroup.value()->desc(), caps);
    TRUFFLE_CHECK(stableStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::persistent);
    TRUFFLE_CHECK(!stableStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!stableStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(stableStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(!stableStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(stableStrategy.frameSlotCount == 1);
    TRUFFLE_CHECK(stableStrategy.recycleFrameLag == 0);
    TRUFFLE_CHECK(stableStrategy.budget.model ==
                  caps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(stableStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(stableStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::manual);
    TRUFFLE_CHECK(stableStrategy.mappingModel ==
                  caps.descriptorPolicy.mappingModel);
    TRUFFLE_CHECK(stableStrategy.allocationModel ==
                  caps.descriptorPolicy.allocationModel);
    TRUFFLE_CHECK(stableStrategy.updateModel ==
                  caps.descriptorPolicy.updateModel);
    TRUFFLE_CHECK(stableStrategy.flattenedNativeBindings ==
                  caps.descriptorPolicy.flattenedNativeBindings);
    TRUFFLE_CHECK(!stableStrategy.rebuildAllocationOnUpdate);
    const auto stableArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(stableStrategy, 2);
    TRUFFLE_CHECK(stableArenaPlan.bindGroupCount == 2);
    TRUFFLE_CHECK(stableArenaPlan.reservationMultiplier == 1);
    TRUFFLE_CHECK(stableArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!stableArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(stableArenaPlan.cacheEntryCount == 2);
    TRUFFLE_CHECK(stableArenaPlan.reservationEntryCount == 2);
    TRUFFLE_CHECK(stableArenaPlan.cacheBudget.totalUnits == 6);
    TRUFFLE_CHECK(stableArenaPlan.reservationBudget.totalUnits == 6);
    const auto bindGroupFootprint = validBindGroup.value()->descriptor_footprint();
    TRUFFLE_CHECK(bindGroupFootprint.bindingCount == layoutFootprint.bindingCount);
    TRUFFLE_CHECK(bindGroupFootprint.descriptorCount ==
                  layoutFootprint.descriptorCount);
    TRUFFLE_CHECK(bindGroupFootprint.bufferSlots.firstSlot ==
                  layoutFootprint.bufferSlots.firstSlot);
    TRUFFLE_CHECK(bindGroupFootprint.textureSlots.firstSlot ==
                  layoutFootprint.textureSlots.firstSlot);
    TRUFFLE_CHECK(bindGroupFootprint.samplerSlots.firstSlot ==
                  layoutFootprint.samplerSlots.firstSlot);
    const auto transientFrameIndex =
        caps.maxFramesInFlight > 1 ? 1u : 0u;
    auto frameCachedBindGroup = device.create_bind_group({
        .cacheKey = 0xB104u,
        .layout = bindGroupLayout.value().get(),
        .allocationPolicy =
            truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        .allocationFrameIndex = transientFrameIndex,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(frameCachedBindGroup.ok());
    TRUFFLE_CHECK(frameCachedBindGroup.value()->cache_key() == 0xB104u);
    TRUFFLE_CHECK(frameCachedBindGroup.value()->allocation_policy() ==
                  truffle::rhi::BindGroupAllocationPolicy::transient_frame);
    TRUFFLE_CHECK(frameCachedBindGroup.value()->allocation_frame_index() ==
                  transientFrameIndex);
    TRUFFLE_CHECK(frameCachedBindGroup.value()->reuse_hint() ==
                  truffle::rhi::BindGroupReuseHint::stable);
    const auto frameCachedStrategy = truffle::rhi::bind_group_descriptor_strategy(
        frameCachedBindGroup.value()->desc(), caps);
    TRUFFLE_CHECK(frameCachedStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::per_frame);
    TRUFFLE_CHECK(!frameCachedStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!frameCachedStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(frameCachedStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(frameCachedStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(frameCachedStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(frameCachedStrategy.frameSlotCount == caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameCachedStrategy.recycleFrameLag == caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameCachedStrategy.budget.model ==
                  caps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(frameCachedStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(frameCachedStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::frame_retire);
    const auto frameArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(frameCachedStrategy, 2);
    TRUFFLE_CHECK(frameArenaPlan.bindGroupCount == 2);
    TRUFFLE_CHECK(frameArenaPlan.reservationMultiplier ==
                  caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(frameArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(frameArenaPlan.cacheEntryCount ==
                  2 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.reservationEntryCount ==
                  2 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.cacheBudget.totalUnits ==
                  6 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(frameArenaPlan.reservationBudget.totalUnits ==
                  6 * caps.maxFramesInFlight);
    auto transientBindGroup = device.create_bind_group({
        .cacheKey = 0xB102u,
        .layout = bindGroupLayout.value().get(),
        .allocationPolicy =
            truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        .reuseHint = truffle::rhi::BindGroupReuseHint::rebuild,
        .allocationFrameIndex = transientFrameIndex,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(transientBindGroup.ok());
    TRUFFLE_CHECK(transientBindGroup.value()->cache_key() == 0xB102u);
    TRUFFLE_CHECK(transientBindGroup.value()->allocation_policy() ==
                  truffle::rhi::BindGroupAllocationPolicy::transient_frame);
    TRUFFLE_CHECK(transientBindGroup.value()->allocation_frame_index() ==
                  transientFrameIndex);
    TRUFFLE_CHECK(transientBindGroup.value()->reuse_hint() ==
                  truffle::rhi::BindGroupReuseHint::rebuild);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_prefers_arena_recycling(
            transientBindGroup.value()->desc()));
    const auto rebuildStrategy = truffle::rhi::bind_group_descriptor_strategy(
        transientBindGroup.value()->desc(), caps);
    TRUFFLE_CHECK(rebuildStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::none);
    TRUFFLE_CHECK(!rebuildStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!rebuildStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(rebuildStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(!rebuildStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(rebuildStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(rebuildStrategy.frameSlotCount == caps.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildStrategy.recycleFrameLag == caps.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildStrategy.budget.model ==
                  caps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(rebuildStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(rebuildStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::immediate);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_lifetime_class(
                      rebuildStrategy) ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::immediate);
    const auto rebuildArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(rebuildStrategy, 3);
    TRUFFLE_CHECK(rebuildArenaPlan.bindGroupCount == 3);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationMultiplier ==
                  caps.maxFramesInFlight);
    TRUFFLE_CHECK(!rebuildArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!rebuildArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(rebuildArenaPlan.cacheEntryCount == 0);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationEntryCount ==
                  3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(rebuildArenaPlan.cacheBudget.totalUnits == 0);
    TRUFFLE_CHECK(rebuildArenaPlan.reservationBudget.totalUnits ==
                  9 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_arena_pool_class(
                      rebuildStrategy) ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      uncached_reservation);
    auto updateHintBindGroup = device.create_bind_group({
        .cacheKey = 0xB103u,
        .layout = bindGroupLayout.value().get(),
        .reuseHint = truffle::rhi::BindGroupReuseHint::update_in_place,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(updateHintBindGroup.ok());
    TRUFFLE_CHECK(updateHintBindGroup.value()->reuse_hint() ==
                  truffle::rhi::BindGroupReuseHint::update_in_place);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_prefers_descriptor_cache(
            updateHintBindGroup.value()->desc()));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_prefers_descriptor_rewrite(
            updateHintBindGroup.value()->desc()));
    const auto updateStrategy = truffle::rhi::bind_group_descriptor_strategy(
        updateHintBindGroup.value()->desc(), caps);
    TRUFFLE_CHECK(updateStrategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::persistent);
    TRUFFLE_CHECK(updateStrategy.rewriteDescriptors);
    TRUFFLE_CHECK(!updateStrategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(!updateStrategy.recycleAfterFrame);
    TRUFFLE_CHECK(updateStrategy.cacheKeyUsable);
    TRUFFLE_CHECK(!updateStrategy.requiresFrameIndex);
    TRUFFLE_CHECK(updateStrategy.frameSlotCount == 1);
    TRUFFLE_CHECK(updateStrategy.recycleFrameLag == 0);
    TRUFFLE_CHECK(updateStrategy.budget.model ==
                  caps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(updateStrategy.budget.totalUnits == 3);
    TRUFFLE_CHECK(updateStrategy.evictionPolicy ==
                  truffle::rhi::BindGroupDescriptorEvictionPolicy::manual);
    TRUFFLE_CHECK(truffle::rhi::bind_group_descriptor_lifetime_class(
                      updateStrategy) ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    const auto updateArenaPlan =
        truffle::rhi::bind_group_descriptor_arena_plan(
            updateHintBindGroup.value()->desc(), caps, 4);
    TRUFFLE_CHECK(updateArenaPlan.bindGroupCount == 4);
    TRUFFLE_CHECK(updateArenaPlan.reservationMultiplier == 1);
    TRUFFLE_CHECK(updateArenaPlan.usesDescriptorCache);
    TRUFFLE_CHECK(!updateArenaPlan.partitionsCachePerFrame);
    TRUFFLE_CHECK(updateArenaPlan.cacheEntryCount == 4);
    TRUFFLE_CHECK(updateArenaPlan.reservationEntryCount == 4);
    TRUFFLE_CHECK(updateArenaPlan.cacheBudget.totalUnits == 12);
    TRUFFLE_CHECK(updateArenaPlan.reservationBudget.totalUnits == 12);
    TRUFFLE_CHECK(
        !truffle::rhi::bind_group_descriptor_strategy_partition_compatible(
            stableStrategy, updateStrategy));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_strategy_partition_reusable(
            stableStrategy, updateStrategy));
    truffle::rhi::SharedPipelineLayoutDescriptorArenaSummary updateReuseFamilySummary;
    updateReuseFamilySummary.layoutCount = 2;
    updateReuseFamilySummary.requestCount = 2;
    updateReuseFamilySummary.plannedGroupCount = 2;
    updateReuseFamilySummary.familyCount = 2;
    updateReuseFamilySummary.families.push_back({
        .layout = {},
        .strategy = stableStrategy,
        .arenaPlan = stableArenaPlan,
        .requestCount = 1,
    });
    updateReuseFamilySummary.families.push_back({
        .layout = {},
        .strategy = updateStrategy,
        .arenaPlan = updateArenaPlan,
        .requestCount = 1,
    });
    const auto updateReusePartitions =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_partition_summary(
            updateReuseFamilySummary);
    TRUFFLE_CHECK(updateReusePartitions.partitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitions.persistentCachePartitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitions.perFrameCachePartitionCount == 0);
    TRUFFLE_CHECK(updateReusePartitions.uncachedReservationPartitionCount == 0);
    TRUFFLE_CHECK(updateReusePartitions.mixedCacheKeyPartitionCount == 0);
    TRUFFLE_CHECK(updateReusePartitions.mixedUpdatePartitionCount == 1);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencyCount == 2);
    TRUFFLE_CHECK(updateReusePartitions.familyScopedLiveObjectCount == 2);
    TRUFFLE_CHECK(updateReusePartitions.partitionScopedLiveObjectCount == 0);
    TRUFFLE_CHECK(updateReusePartitions.partitions.size() == 1);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].familyCount == 2);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].requestCount == 2);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].bindGroupCount == 6);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].entryCount == 6);
    TRUFFLE_CHECK(
        updateReusePartitions.partitions[0].cacheKeyUsableFamilyCount == 2);
    TRUFFLE_CHECK(
        updateReusePartitions.partitions[0].rewriteDescriptorFamilyCount == 1);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0]
                      .rebuildAllocationOnUpdateFamilyCount == 0);
    TRUFFLE_CHECK(!updateReusePartitions.partitions[0].mixedCacheKeyUsability);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].mixedUpdateBehavior);
    TRUFFLE_CHECK(!updateReusePartitions.partitions[0].mixedNativeUpdateModels);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].strategy.cacheKeyUsable);
    TRUFFLE_CHECK(
        updateReusePartitions.partitions[0].strategy.rewriteDescriptors);
    TRUFFLE_CHECK(!updateReusePartitions.partitions[0]
                       .strategy.rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].totalBudget.totalUnits ==
                  18);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].familyIndices.size() == 2);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].familyIndices[0] == 0);
    TRUFFLE_CHECK(updateReusePartitions.partitions[0].familyIndices[1] == 1);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies.size() == 2);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].familyIndex == 0);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].partitionIndex == 0);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0]
                      .sharesPartitionCapacity);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].usesDescriptorCache);
    TRUFFLE_CHECK(!updateReusePartitions.familyResidencies[0]
                       .partitionsCachePerFrame);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0].entryCount == 2);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0]
                      .totalBudget.totalUnits == 6);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[0]
                      .partitionHasMixedUpdateBehavior);
    TRUFFLE_CHECK(!updateReusePartitions.familyResidencies[0]
                       .partitionHasMixedNativeUpdateModels);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].familyIndex == 1);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].partitionIndex == 0);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].poolClass ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].lifetimeClass ==
                  truffle::rhi::BindGroupDescriptorLifetimeClass::
                      retained_manual);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].liveObjectScope ==
                  truffle::rhi::BindGroupDescriptorLiveObjectScope::family);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1].entryCount == 4);
    TRUFFLE_CHECK(updateReusePartitions.familyResidencies[1]
                      .totalBudget.totalUnits == 12);
    const auto updateReuseCohorts =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_cohort_summary(
            updateReusePartitions);
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
    TRUFFLE_CHECK(!updateReuseCohorts.cohorts[0].rewriteDescriptors);
    TRUFFLE_CHECK(!updateReuseCohorts.cohorts[0].rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].familyCount == 1);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].bindGroupCount == 2);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].entryCount == 2);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[0].familyIndices[0] == 0);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].kind ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      capacity_only);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].rewriteDescriptors);
    TRUFFLE_CHECK(!updateReuseCohorts.cohorts[1].rebuildAllocationOnUpdate);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].familyCount == 1);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].bindGroupCount == 4);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].entryCount == 4);
    TRUFFLE_CHECK(updateReuseCohorts.cohorts[1].familyIndices[0] == 1);
    truffle::rhi::SharedPipelineLayoutDescriptorArenaPlan updateReusePlan;
    updateReusePlan.families = updateReuseFamilySummary;
    updateReusePlan.partitions = updateReusePartitions;
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
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].bindGroupCapacity == 6);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].entryCapacity == 6);
    TRUFFLE_CHECK(!updateReuseMaterialization.arenas[0]
                       .supportsPartitionWideLiveObjectReuse);
    TRUFFLE_CHECK(updateReuseMaterialization.arenas[0].mixedUpdateBehavior);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[0]
                      .bindGroupCapacity == 2);
    TRUFFLE_CHECK(updateReuseMaterialization.reuseMaterializations[1]
                      .bindGroupCapacity == 4);
    const truffle::rhi::BindGroupDescriptorArenaPlan aggregatePlans[] = {
        stableArenaPlan,
        frameArenaPlan,
        rebuildArenaPlan,
        updateArenaPlan,
    };
    const auto aggregateTotals =
        truffle::rhi::bind_group_descriptor_arena_totals(aggregatePlans);
    TRUFFLE_CHECK(aggregateTotals.planCount == 4);
    TRUFFLE_CHECK(aggregateTotals.bindGroupCount == 11);
    TRUFFLE_CHECK(aggregateTotals.cachedBindGroupCount == 8);
    TRUFFLE_CHECK(aggregateTotals.uncachedBindGroupCount == 3);
    TRUFFLE_CHECK(aggregateTotals.cacheEntryCount ==
                  6 + 2 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.reservationEntryCount ==
                  6 + 5 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.persistentCacheEntryCount == 6);
    TRUFFLE_CHECK(aggregateTotals.perFrameCacheEntryCount ==
                  2 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.uncachedReservationEntryCount ==
                  3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.maxReservationMultiplier ==
                  caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.usesDescriptorCache);
    TRUFFLE_CHECK(aggregateTotals.partitionsCachePerFrame);
    TRUFFLE_CHECK(!aggregateTotals.mixedBudgetModels);
    TRUFFLE_CHECK(aggregateTotals.budgetModel ==
                  caps.descriptorPolicy.budgetModel);
    TRUFFLE_CHECK(aggregateTotals.maxBudgetPerEntry.totalUnits == 3);
    TRUFFLE_CHECK(aggregateTotals.cacheBudget.totalUnits ==
                  18 + 6 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.reservationBudget.totalUnits ==
                  18 + 15 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.persistentCacheBudget.totalUnits == 18);
    TRUFFLE_CHECK(aggregateTotals.perFrameCacheBudget.totalUnits ==
                  6 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(aggregateTotals.uncachedReservationBudget.totalUnits ==
                  9 * caps.maxFramesInFlight);
    const truffle::rhi::PipelineLayoutDesc descriptorPlanningLayout{
        .debugName = "contract_descriptor_planning_layout",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .groupIndex = 1,
            },
        },
    };
    const auto graphicsGroup1ArenaPlan =
        truffle::rhi::pipeline_layout_bind_group_arena_plan(
            descriptorPlanningLayout,
            caps,
            {
                .groupIndex = 1,
                .bindGroupCount = 3,
                .cacheKey = 0xB201u,
                .allocationPolicy =
                    truffle::rhi::BindGroupAllocationPolicy::transient_frame,
            });
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan.has_value());
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->groupIndex == 1);
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->layout.bindings.size() == 1);
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->strategy.cacheScope ==
                  truffle::rhi::BindGroupCacheScope::per_frame);
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->strategy.budget.totalUnits == 1);
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->arenaPlan.cacheEntryCount ==
                  3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsGroup1ArenaPlan->arenaPlan.cacheBudget.totalUnits ==
                  3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(!truffle::rhi::pipeline_layout_bind_group_arena_plan(
        descriptorPlanningLayout,
        caps,
        {
            .groupIndex = 2,
            .bindGroupCount = 1,
        }).has_value());
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest graphicsLayoutRequests[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 2,
            .cacheKey = 0xB202u,
        },
        {
            .groupIndex = 1,
            .bindGroupCount = 3,
            .cacheKey = 0xB203u,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
        {
            .groupIndex = 2,
            .bindGroupCount = 1,
        },
    };
    const auto graphicsLayoutSummary =
        truffle::rhi::pipeline_layout_descriptor_arena_summary(
            descriptorPlanningLayout, caps, graphicsLayoutRequests);
    TRUFFLE_CHECK(graphicsLayoutSummary.requestCount == 3);
    TRUFFLE_CHECK(graphicsLayoutSummary.plannedGroupCount == 2);
    TRUFFLE_CHECK(graphicsLayoutSummary.missingGroupCount == 1);
    TRUFFLE_CHECK(!graphicsLayoutSummary.complete);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.planCount == 2);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.bindGroupCount == 5);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.cachedBindGroupCount == 5);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.uncachedBindGroupCount == 0);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.cacheEntryCount ==
                  2 + 3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.reservationEntryCount ==
                  2 + 3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.persistentCacheEntryCount == 2);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.perFrameCacheEntryCount ==
                  3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.usesDescriptorCache);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.partitionsCachePerFrame);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.maxBudgetPerEntry.totalUnits == 1);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.cacheBudget.totalUnits ==
                  2 + 3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.reservationBudget.totalUnits ==
                  2 + 3 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.persistentCacheBudget.totalUnits ==
                  2);
    TRUFFLE_CHECK(graphicsLayoutSummary.totals.perFrameCacheBudget.totalUnits ==
                  3 * caps.maxFramesInFlight);
    const truffle::rhi::PipelineLayoutDesc sharedPoolLayoutA{
        .debugName = "contract_shared_pool_layout_a",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
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
        .debugName = "contract_shared_pool_layout_b",
        .bindings = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
                .minBindingSize = 16,
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
            .cacheKey = 0xB204u,
        },
        {
            .groupIndex = 1,
            .bindGroupCount = 1,
            .cacheKey = 0xB205u,
            .allocationPolicy =
                truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        },
    };
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolRequestsB[] = {
        {
            .groupIndex = 0,
            .bindGroupCount = 3,
            .cacheKey = 0xB206u,
        },
        {
            .groupIndex = 2,
            .bindGroupCount = 4,
            .cacheKey = 0xB207u,
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
    const truffle::rhi::PipelineLayoutBindGroupArenaRequest sharedPoolMissingLayoutRequests[] = {
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
            .requests = sharedPoolMissingLayoutRequests,
        },
    };
    const auto sharedPoolSummary =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_summary(
            caps, sharedPoolBatches);
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
                  5 + 10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.reservationEntryCount ==
                  5 + 10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.persistentCacheEntryCount == 5);
    TRUFFLE_CHECK(sharedPoolSummary.totals.perFrameCacheEntryCount ==
                  10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.usesDescriptorCache);
    TRUFFLE_CHECK(sharedPoolSummary.totals.partitionsCachePerFrame);
    TRUFFLE_CHECK(sharedPoolSummary.totals.maxBudgetPerEntry.totalUnits == 2);
    TRUFFLE_CHECK(sharedPoolSummary.totals.cacheBudget.totalUnits ==
                  5 + 15 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.reservationBudget.totalUnits ==
                  5 + 15 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolSummary.totals.persistentCacheBudget.totalUnits == 5);
    TRUFFLE_CHECK(sharedPoolSummary.totals.perFrameCacheBudget.totalUnits ==
                  15 * caps.maxFramesInFlight);
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
                  10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPartitions.partitions[1].reservationMultiplier ==
                  caps.maxFramesInFlight);
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
                  15 * caps.maxFramesInFlight);
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
                  5 * caps.maxFramesInFlight);
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
                  5 * caps.maxFramesInFlight);
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
                  10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].mixedCacheKeyUsability);
    TRUFFLE_CHECK(sharedPoolCohorts.cohorts[1].familyIndices.size() == 2);
    const auto sharedPoolPlan =
        truffle::rhi::pipeline_layout_shared_descriptor_arena_plan(
            caps, sharedPoolBatches);
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
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].bindGroupCapacity == 5);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0].entryCapacity == 5);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[0]
                      .supportsPartitionWideLiveObjectReuse);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].bindGroupCapacity == 10);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1].entryCapacity ==
                  10 * caps.maxFramesInFlight);
    TRUFFLE_CHECK(sharedPoolPlan.materialization.arenas[1]
                      .mixedCacheKeyUsability);
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
    auto descriptorArena = device.create_bind_group_descriptor_arena(
        sharedPoolPlan.materialization.arenas[0]);
    TRUFFLE_CHECK(descriptorArena.ok());
    TRUFFLE_CHECK(descriptorArena.value()->partition_index() == 0);
    TRUFFLE_CHECK(descriptorArena.value()->pool_class() ==
                  truffle::rhi::BindGroupDescriptorArenaPoolClass::
                      persistent_cache);
    TRUFFLE_CHECK(descriptorArena.value()->bind_group_capacity() == 5);
    auto descriptorReuseMaterializer =
        device.create_bind_group_descriptor_reuse_materializer(
            sharedPoolPlan.materialization.reuseMaterializations[0]);
    TRUFFLE_CHECK(descriptorReuseMaterializer.ok());
    TRUFFLE_CHECK(descriptorReuseMaterializer.value()->cohort_index() == 0);
    TRUFFLE_CHECK(descriptorReuseMaterializer.value()->kind() ==
                  truffle::rhi::BindGroupDescriptorReuseCohortKind::
                      live_objects);
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->supports_live_object_reuse());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->compatible_with(*descriptorArena.value()));
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->state().issuedRequestCount == 0);
    auto persistentReservationRequest =
        descriptorReuseMaterializer.value()->make_reservation_request(3);
    TRUFFLE_CHECK(persistentReservationRequest.ok());
    TRUFFLE_CHECK(persistentReservationRequest.value().frameIndex == 0);
    TRUFFLE_CHECK(
        descriptorArena.value()->can_reserve(persistentReservationRequest.value()));
    auto persistentReservation =
        descriptorArena.value()->reserve(persistentReservationRequest.value());
    TRUFFLE_CHECK(persistentReservation.ok());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->observe_reservation(
            persistentReservation.value())
            .ok());
    TRUFFLE_CHECK(descriptorArena.value()->usage().usedBindGroupCount == 3);
    TRUFFLE_CHECK(
        !descriptorArena.value()->can_reserve(
            descriptorReuseMaterializer.value()->reservation_request(3)));
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->state().activeReservationCount == 1);
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->state().trackedReservations.size() == 1);
    TRUFFLE_CHECK(descriptorArena.value()->release(persistentReservation.value())
                      .ok());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->release_reservation(
            persistentReservation.value())
            .ok());
    TRUFFLE_CHECK(descriptorArena.value()->clear().ok());
    TRUFFLE_CHECK(descriptorArena.value()->empty());
    TRUFFLE_CHECK(descriptorReuseMaterializer.value()->clear().ok());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->state().issuedRequestCount == 0);
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator persistentCoordinator{
        *descriptorArena.value(), *descriptorReuseMaterializer.value()};
    TRUFFLE_CHECK(persistentCoordinator.compatible());
    TRUFFLE_CHECK(persistentCoordinator.empty());
    auto coordinatedPersistentRequest =
        persistentCoordinator.make_reservation_request(2);
    TRUFFLE_CHECK(coordinatedPersistentRequest.ok());
    TRUFFLE_CHECK(persistentCoordinator.can_reserve(2));
    auto coordinatedPersistentReservation = persistentCoordinator.reserve(2);
    TRUFFLE_CHECK(coordinatedPersistentReservation.ok());
    const auto coordinatedPersistentState = persistentCoordinator.state();
    TRUFFLE_CHECK(coordinatedPersistentState.trackedReservationCount == 1);
    TRUFFLE_CHECK(!coordinatedPersistentState.drifted);
    TRUFFLE_CHECK(coordinatedPersistentState.underlyingReservationsConsistent);
    const auto coordinatedPersistentPressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            persistentCoordinator);
    TRUFFLE_CHECK(coordinatedPersistentPressure.action ==
                  truffle::rhi::BindGroupDescriptorRuntimePressureAction::none);
    auto externalPersistentRequest =
        descriptorReuseMaterializer.value()->make_reservation_request(1);
    TRUFFLE_CHECK(externalPersistentRequest.ok());
    auto externalPersistentReservation =
        descriptorArena.value()->reserve(externalPersistentRequest.value());
    TRUFFLE_CHECK(externalPersistentReservation.ok());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->observe_reservation(
            externalPersistentReservation.value())
            .ok());
    TRUFFLE_CHECK(persistentCoordinator.state().drifted);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            persistentCoordinator)
            .action ==
        truffle::rhi::BindGroupDescriptorRuntimePressureAction::reconcile);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            persistentCoordinator)
            .action ==
        truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::reconcile);
    const std::vector<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*>
        driftedAdmissionCoordinators = {&persistentCoordinator};
    const auto reconcileAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 1);
    TRUFFLE_CHECK(reconcileAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reconcile_then_admit);
    const auto reconcileReclaimAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 3);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      reconcile_then_reclaim_then_admit);
    TRUFFLE_CHECK(reconcileReclaimAdmissionPlan.reclaimAdmissionCount == 1);
    TRUFFLE_CHECK(persistentCoordinator.reconcile().ok());
    TRUFFLE_CHECK(
        persistentCoordinator.state().trackedReservationCount == 2);
    TRUFFLE_CHECK(
        persistentCoordinator.release(externalPersistentReservation.value()).ok());
    TRUFFLE_CHECK(
        persistentCoordinator.release(coordinatedPersistentReservation.value()).ok());
    auto inconsistentPersistentReservation =
        descriptorArena.value()->reserve(
            descriptorReuseMaterializer.value()->reservation_request(1));
    TRUFFLE_CHECK(inconsistentPersistentReservation.ok());
    TRUFFLE_CHECK(persistentCoordinator.state().drifted);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            persistentCoordinator)
            .action ==
        truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::
            audit_inconsistent_state);
    const auto auditAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            driftedAdmissionCoordinators, 1);
    TRUFFLE_CHECK(auditAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      audit_before_admit);
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    TRUFFLE_CHECK(!persistentCoordinator.drifted());
    auto invalidDescriptorArenaDesc = sharedPoolPlan.materialization.arenas[0];
    invalidDescriptorArenaDesc.bindGroupCapacity = 0;
    auto invalidDescriptorArena =
        device.create_bind_group_descriptor_arena(invalidDescriptorArenaDesc);
    TRUFFLE_CHECK(!invalidDescriptorArena.ok());
    TRUFFLE_CHECK(invalidDescriptorArena.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto invalidDescriptorReuseDesc =
        sharedPoolPlan.materialization.reuseMaterializations[0];
    invalidDescriptorReuseDesc.supportsLiveObjectReuse = false;
    auto invalidDescriptorReuseMaterializer =
        device.create_bind_group_descriptor_reuse_materializer(
            invalidDescriptorReuseDesc);
    TRUFFLE_CHECK(!invalidDescriptorReuseMaterializer.ok());
    TRUFFLE_CHECK(invalidDescriptorReuseMaterializer.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto perFrameDescriptorArena = device.create_bind_group_descriptor_arena(
        sharedPoolPlan.materialization.arenas[1]);
    TRUFFLE_CHECK(perFrameDescriptorArena.ok());
    TRUFFLE_CHECK(perFrameDescriptorArena.value()->slot_count() ==
                  caps.maxFramesInFlight);
    auto perFrameDescriptorReuseMaterializer =
        device.create_bind_group_descriptor_reuse_materializer(
            sharedPoolPlan.materialization.reuseMaterializations[1]);
    TRUFFLE_CHECK(perFrameDescriptorReuseMaterializer.ok());
    TRUFFLE_CHECK(perFrameDescriptorReuseMaterializer.value()->compatible_with(
        *perFrameDescriptorArena.value()));
    auto frameRequest0 =
        perFrameDescriptorReuseMaterializer.value()->make_reservation_request(10);
    TRUFFLE_CHECK(frameRequest0.ok());
    TRUFFLE_CHECK(frameRequest0.value().frameIndex == 0);
    auto frameReservation0 =
        perFrameDescriptorArena.value()->reserve(frameRequest0.value());
    TRUFFLE_CHECK(frameReservation0.ok());
    TRUFFLE_CHECK(
        perFrameDescriptorReuseMaterializer.value()->observe_reservation(
            frameReservation0.value())
            .ok());
    auto frameRequest1 =
        perFrameDescriptorReuseMaterializer.value()->make_reservation_request(10);
    TRUFFLE_CHECK(frameRequest1.ok());
    TRUFFLE_CHECK(frameRequest1.value().frameIndex == 1);
    auto frameReservation1 =
        perFrameDescriptorArena.value()->reserve(frameRequest1.value());
    TRUFFLE_CHECK(frameReservation1.ok());
    TRUFFLE_CHECK(
        perFrameDescriptorReuseMaterializer.value()->observe_reservation(
            frameReservation1.value())
            .ok());
    TRUFFLE_CHECK(!perFrameDescriptorArena.value()->can_reserve({
        .bindGroupCount = 1,
        .entryCount = 1,
        .frameIndex = 0,
    }));
    const auto perFrameUsage = perFrameDescriptorArena.value()->usage();
    TRUFFLE_CHECK(perFrameUsage.reservationCount == 2);
    TRUFFLE_CHECK(perFrameUsage.usedEntryCount == 20);
    TRUFFLE_CHECK(perFrameUsage.slots[0].usedBindGroupCount == 10);
    TRUFFLE_CHECK(perFrameUsage.slots[1].usedBindGroupCount == 10);
    const auto perFrameReuseState =
        perFrameDescriptorReuseMaterializer.value()->state();
    TRUFFLE_CHECK(perFrameReuseState.issuedRequestCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.activeReservationCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.capacityOnlyReservationCount == 2);
    TRUFFLE_CHECK(perFrameReuseState.trackedReservations.size() == 2);
    const auto perFrameArenaPressure =
        truffle::rhi::bind_group_descriptor_arena_pressure(
            *perFrameDescriptorArena.value());
    TRUFFLE_CHECK(perFrameArenaPressure.shouldRetireHottestSlot);
    TRUFFLE_CHECK(perFrameReuseState.nextFrameIndex ==
                  (2 % perFrameDescriptorArena.value()->slot_count()));
    auto retiredFrameSlot = perFrameDescriptorArena.value()->retire_slot(1);
    TRUFFLE_CHECK(retiredFrameSlot.ok());
    TRUFFLE_CHECK(retiredFrameSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(retiredFrameSlot.value().releasedBindGroupCount == 10);
    auto retiredReuseFrameSlot =
        perFrameDescriptorReuseMaterializer.value()->retire_slot(1);
    TRUFFLE_CHECK(retiredReuseFrameSlot.ok());
    TRUFFLE_CHECK(retiredReuseFrameSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(perFrameDescriptorArena.value()->release(frameReservation0.value())
                      .ok());
    TRUFFLE_CHECK(
        perFrameDescriptorReuseMaterializer.value()->release_reservation(
            frameReservation0.value())
            .ok());
    TRUFFLE_CHECK(perFrameDescriptorArena.value()->can_reserve({
        .bindGroupCount = 1,
        .entryCount = 1,
        .frameIndex = 0,
    }));
    TRUFFLE_CHECK(perFrameDescriptorArena.value()->clear().ok());
    TRUFFLE_CHECK(perFrameDescriptorArena.value()->empty());
    TRUFFLE_CHECK(perFrameDescriptorReuseMaterializer.value()->clear().ok());
    TRUFFLE_CHECK(
        perFrameDescriptorReuseMaterializer.value()->state().activeReservationCount ==
        0);
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator incompatibleCoordinator{
        *perFrameDescriptorArena.value(), *descriptorReuseMaterializer.value()};
    TRUFFLE_CHECK(!incompatibleCoordinator.compatible());
    TRUFFLE_CHECK(!incompatibleCoordinator.make_reservation_request(1).ok());
    truffle::rhi::BindGroupDescriptorRuntimeCoordinator perFrameCoordinator{
        *perFrameDescriptorArena.value(), *perFrameDescriptorReuseMaterializer.value()};
    auto coordinatedFrameReservation0 = perFrameCoordinator.reserve(10);
    TRUFFLE_CHECK(coordinatedFrameReservation0.ok());
    TRUFFLE_CHECK(coordinatedFrameReservation0.value().frameIndex == 0);
    auto coordinatedFrameReservation1 = perFrameCoordinator.reserve(10);
    TRUFFLE_CHECK(coordinatedFrameReservation1.ok());
    TRUFFLE_CHECK(coordinatedFrameReservation1.value().frameIndex == 1);
    TRUFFLE_CHECK(perFrameCoordinator.state().trackedReservationCount == 2);
    TRUFFLE_CHECK(!perFrameCoordinator.state().drifted);
    const auto perFrameCoordinatorPressure =
        truffle::rhi::bind_group_descriptor_runtime_coordinator_pressure(
            perFrameCoordinator);
    TRUFFLE_CHECK(perFrameCoordinatorPressure.action ==
                  truffle::rhi::BindGroupDescriptorRuntimePressureAction::retire_slot);
    TRUFFLE_CHECK(perFrameCoordinatorPressure.reclaimSlotIndex.has_value());
    const auto perFrameCoordinatorReclamationPlan =
        truffle::rhi::bind_group_descriptor_runtime_reclamation_plan(
            perFrameCoordinator);
    TRUFFLE_CHECK(perFrameCoordinatorReclamationPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::retire_slot);
    TRUFFLE_CHECK(perFrameCoordinatorReclamationPlan.recommendedSlotIndex.has_value());
    TRUFFLE_CHECK(perFrameCoordinatorReclamationPlan.recommendedReleaseCount == 1);
    TRUFFLE_CHECK(persistentCoordinator.reserve(4).ok());
    const std::vector<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*>
        arbitrationCoordinators = {&persistentCoordinator, &perFrameCoordinator};
    const auto arbitrationPlan =
        truffle::rhi::bind_group_descriptor_runtime_arbitration_plan(
            arbitrationCoordinators);
    TRUFFLE_CHECK(arbitrationPlan.coordinatorCount == 2);
    TRUFFLE_CHECK(arbitrationPlan.preferredCoordinatorIndex.has_value());
    TRUFFLE_CHECK(*arbitrationPlan.preferredCoordinatorIndex == 1);
    TRUFFLE_CHECK(arbitrationPlan.preferredReclamationAction ==
                  truffle::rhi::BindGroupDescriptorRuntimeReclamationAction::retire_slot);
    TRUFFLE_CHECK(arbitrationPlan.coordinators.size() == 2);
    TRUFFLE_CHECK(arbitrationPlan.coordinators[0].preferred);
    TRUFFLE_CHECK(arbitrationPlan.coordinators[0].coordinatorIndex == 1);
    const auto immediateAdmissionPlan =
        truffle::rhi::bind_group_descriptor_runtime_admission_plan(
            arbitrationCoordinators, 1, 0);
    TRUFFLE_CHECK(immediateAdmissionPlan.immediateAdmissionCount == 1);
    TRUFFLE_CHECK(immediateAdmissionPlan.reclaimAdmissionCount == 1);
    TRUFFLE_CHECK(immediateAdmissionPlan.action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      admit_now);
    TRUFFLE_CHECK(immediateAdmissionPlan.preferredCoordinatorIndex.has_value());
    TRUFFLE_CHECK(*immediateAdmissionPlan.preferredCoordinatorIndex == 0);
    TRUFFLE_CHECK(immediateAdmissionPlan.coordinators[0].action ==
                  truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                      admit_now);
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
    const auto batchRevalidationPlan =
        truffle::rhi::bind_group_descriptor_runtime_revalidate_batch_admission_plan(
            arbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(batchRevalidationPlan.valid);
    TRUFFLE_CHECK(batchRevalidationPlan.validSelectedCount == 3);
    TRUFFLE_CHECK(batchRevalidationPlan.invalidSelectedCount == 0);
    TRUFFLE_CHECK(batchRevalidationPlan.staleSelectedCount == 0);
    std::array<truffle::rhi::BindGroupDescriptorRuntimeCoordinator*, 2>
        mutableArbitrationCoordinators = {&persistentCoordinator,
                                          &perFrameCoordinator};
    const auto batchExecutionResult =
        truffle::rhi::bind_group_descriptor_runtime_execute_batch_admission_plan(
            mutableArbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(batchExecutionResult.status.ok());
    TRUFFLE_CHECK(batchExecutionResult.committedCount == 3);
    TRUFFLE_CHECK(batchExecutionResult.reclaimedEntryCount == 2);
    TRUFFLE_CHECK(batchExecutionResult.entries[1].releasedBindGroupCount == 4);
    TRUFFLE_CHECK(batchExecutionResult.entries[2].releasedBindGroupCount == 10);
    TRUFFLE_CHECK(persistentCoordinator.state().trackedReservationCount == 2);
    TRUFFLE_CHECK(perFrameCoordinator.state().trackedReservationCount == 2);
    auto staleBatchRequest =
        descriptorReuseMaterializer.value()->make_reservation_request(1);
    TRUFFLE_CHECK(staleBatchRequest.ok());
    auto staleBatchReservation =
        descriptorArena.value()->reserve(staleBatchRequest.value());
    TRUFFLE_CHECK(staleBatchReservation.ok());
    TRUFFLE_CHECK(
        descriptorReuseMaterializer.value()->observe_reservation(
            staleBatchReservation.value())
            .ok());
    const auto staleBatchRevalidationPlan =
        truffle::rhi::bind_group_descriptor_runtime_revalidate_batch_admission_plan(
            arbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(!staleBatchRevalidationPlan.valid);
    TRUFFLE_CHECK(staleBatchRevalidationPlan.invalidSelectedCount >= 1);
    TRUFFLE_CHECK(staleBatchRevalidationPlan.firstInvalidRequestIndex.has_value());
    TRUFFLE_CHECK(*staleBatchRevalidationPlan.firstInvalidRequestIndex == 0);
    const auto staleBatchRepairPlan =
        truffle::rhi::bind_group_descriptor_runtime_repair_batch_admission_plan(
            arbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(staleBatchRepairPlan.repairable);
    TRUFFLE_CHECK(staleBatchRepairPlan.changed);
    TRUFFLE_CHECK(staleBatchRepairPlan.shouldReplaceSavedPlan);
    TRUFFLE_CHECK(!staleBatchRepairPlan.revalidation.valid);
    TRUFFLE_CHECK(staleBatchRepairPlan.rewrittenSelectedCount >= 1);
    TRUFFLE_CHECK(staleBatchRepairPlan.entries.size() == 3);
    TRUFFLE_CHECK(staleBatchRepairPlan.entries[0].changed);
    TRUFFLE_CHECK(staleBatchRepairPlan.entries[0].savedSelected);
    TRUFFLE_CHECK(staleBatchRepairPlan.entries[0].repairedSelected);
    const auto staleBatchDeltaPlan =
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_plan(
            arbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(staleBatchDeltaPlan.repairable);
    TRUFFLE_CHECK(staleBatchDeltaPlan.changed);
    TRUFFLE_CHECK(staleBatchDeltaPlan.shouldReplaceSavedPlan);
    TRUFFLE_CHECK(staleBatchDeltaPlan.deltaCount >= 1);
    TRUFFLE_CHECK(staleBatchDeltaPlan.firstDeltaRequestIndex.has_value());
    TRUFFLE_CHECK(*staleBatchDeltaPlan.firstDeltaRequestIndex == 0);
    TRUFFLE_CHECK(staleBatchDeltaPlan.deltas[0].kind ==
                  truffle::rhi::BindGroupDescriptorRuntimeBatchRepairDeltaKind::
                      rewrite);
    TRUFFLE_CHECK(staleBatchDeltaPlan.invalidSavedPlanReasonCount >= 1);
    TRUFFLE_CHECK(staleBatchDeltaPlan.staleActionReasonCount >= 1);
    TRUFFLE_CHECK(staleBatchDeltaPlan.rewrittenActionReasonCount >= 1);
    TRUFFLE_CHECK(staleBatchDeltaPlan.deltas[0].reasonCount >= 2);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_has_reason(
            staleBatchDeltaPlan.deltas[0].reasons,
            truffle::rhi::BindGroupDescriptorRuntimeBatchRepairDeltaReasonFlags::
                saved_plan_invalid));
    auto staleBatchDeltaApplied =
        truffle::rhi::bind_group_descriptor_runtime_apply_batch_repair_delta_plan(
            batchAdmissionPlan, staleBatchDeltaPlan);
    TRUFFLE_CHECK(staleBatchDeltaApplied.ok());
    TRUFFLE_CHECK(staleBatchDeltaApplied.value().admittedCount ==
                  staleBatchRepairPlan.repairedPlan.admittedCount);
    TRUFFLE_CHECK(staleBatchDeltaApplied.value().decisions[0].admission.action ==
                  staleBatchRepairPlan.repairedPlan.decisions[0].admission.action);
    const auto staleBatchExecutionResult =
        truffle::rhi::bind_group_descriptor_runtime_execute_batch_admission_plan(
            mutableArbitrationCoordinators, batchAdmissionPlan);
    TRUFFLE_CHECK(!staleBatchExecutionResult.status.ok());
    TRUFFLE_CHECK(staleBatchExecutionResult.status.code ==
                  truffle::core::StatusCode::invalid_state);
    TRUFFLE_CHECK(staleBatchExecutionResult.committedCount == 0);
    TRUFFLE_CHECK(staleBatchExecutionResult.failedRequestIndex.has_value());
    TRUFFLE_CHECK(*staleBatchExecutionResult.failedRequestIndex == 0);
    TRUFFLE_CHECK(persistentCoordinator.reconcile().ok());
    TRUFFLE_CHECK(
        persistentCoordinator.release(staleBatchReservation.value()).ok());
    auto coordinatedRetiredSlot = perFrameCoordinator.retire_slot(1);
    TRUFFLE_CHECK(coordinatedRetiredSlot.ok());
    TRUFFLE_CHECK(coordinatedRetiredSlot.value().releasedReservationCount == 1);
    TRUFFLE_CHECK(perFrameCoordinator.state().trackedReservationCount == 1);
    TRUFFLE_CHECK(perFrameCoordinator.clear().ok());
    TRUFFLE_CHECK(perFrameCoordinator.empty());
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    std::array<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*, 2>
        repairExecutionCoordinators = {&persistentCoordinator, &perFrameCoordinator};
    std::array<truffle::rhi::BindGroupDescriptorRuntimeBatchAdmissionIntent, 2>
        repairExecutionRequests = {{
            {.bindGroupCount = 1, .frameIndex = 0},
            {.bindGroupCount = 1, .frameIndex = 1},
        }};
    const auto repairExecutionPlan =
        truffle::rhi::bind_group_descriptor_runtime_batch_admission_plan(
            repairExecutionCoordinators, repairExecutionRequests);
    std::array<truffle::rhi::BindGroupDescriptorRuntimeCoordinator*, 2>
        mutableRepairExecutionCoordinators = {&persistentCoordinator,
                                              &perFrameCoordinator};
    const auto repairExecutionResult =
        truffle::rhi::bind_group_descriptor_runtime_repair_and_execute_batch_admission_plan(
            mutableRepairExecutionCoordinators, repairExecutionPlan);
    const auto repairExecutionDeltaPlan =
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_plan(
            repairExecutionCoordinators, repairExecutionPlan);
    TRUFFLE_CHECK(repairExecutionResult.status.ok());
    TRUFFLE_CHECK(repairExecutionResult.usedSavedPlan);
    TRUFFLE_CHECK(!repairExecutionResult.usedReplacementPlan);
    TRUFFLE_CHECK(repairExecutionResult.repair.revalidation.valid);
    TRUFFLE_CHECK(!repairExecutionResult.repair.shouldReplaceSavedPlan);
    TRUFFLE_CHECK(repairExecutionDeltaPlan.repairable);
    TRUFFLE_CHECK(repairExecutionDeltaPlan.empty);
    TRUFFLE_CHECK(repairExecutionDeltaPlan.deltaCount == 0);
    TRUFFLE_CHECK(repairExecutionDeltaPlan.invalidSavedPlanReasonCount == 0);
    TRUFFLE_CHECK(repairExecutionResult.execution.committedCount ==
                  repairExecutionPlan.admittedCount);
    TRUFFLE_CHECK(perFrameCoordinator.clear().ok());
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    auto rollbackExecutionRequest = persistentCoordinator.make_reservation_request(1);
    TRUFFLE_CHECK(rollbackExecutionRequest.ok());
    truffle::rhi::BindGroupDescriptorRuntimeBatchAdmissionPlan rollbackBatchPlan;
    rollbackBatchPlan.decisions.push_back({
        .requestIndex = 0,
        .request = {
            .bindGroupCount = 1,
            .frameIndex = 0,
        },
        .admitted = true,
        .admission =
            {
                .coordinatorIndex = 0,
                .action = truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                    admit_now,
                .request = rollbackExecutionRequest.value(),
                .targetSlotIndex = 0,
            },
    });
    rollbackBatchPlan.decisions.push_back({
        .requestIndex = 1,
        .request = {
            .bindGroupCount = 1,
            .frameIndex = 0,
        },
        .admitted = true,
        .admission =
            {
                .coordinatorIndex = 1,
                .action = truffle::rhi::BindGroupDescriptorRuntimeAdmissionAction::
                    admit_now,
                .request = rollbackExecutionRequest.value(),
                .targetSlotIndex = 0,
            },
    });
    std::array<truffle::rhi::BindGroupDescriptorRuntimeCoordinator*, 2>
        rollbackCoordinators = {&persistentCoordinator, &incompatibleCoordinator};
    const auto rollbackExecutionResult =
        truffle::rhi::bind_group_descriptor_runtime_execute_batch_admission_plan(
            rollbackCoordinators, rollbackBatchPlan);
    TRUFFLE_CHECK(!rollbackExecutionResult.status.ok());
    TRUFFLE_CHECK(rollbackExecutionResult.status.code ==
                  truffle::core::StatusCode::invalid_state);
    TRUFFLE_CHECK(rollbackExecutionResult.committedCount == 0);
    TRUFFLE_CHECK(rollbackExecutionResult.rolledBackCount == 0);
    TRUFFLE_CHECK(rollbackExecutionResult.failedRequestIndex.has_value());
    TRUFFLE_CHECK(*rollbackExecutionResult.failedRequestIndex == 1);
    TRUFFLE_CHECK(!rollbackExecutionResult.entries[1].status.ok());
    TRUFFLE_CHECK(!rollbackExecutionResult.entries[0].rolledBack);
    TRUFFLE_CHECK(persistentCoordinator.state().trackedReservationCount == 0);
    std::array<const truffle::rhi::BindGroupDescriptorRuntimeCoordinator*, 2>
        rollbackRepairCoordinators = {&persistentCoordinator,
                                      &incompatibleCoordinator};
    const auto rollbackRepairPlan =
        truffle::rhi::bind_group_descriptor_runtime_repair_batch_admission_plan(
            rollbackRepairCoordinators, rollbackBatchPlan);
    TRUFFLE_CHECK(rollbackRepairPlan.repairable);
    TRUFFLE_CHECK(rollbackRepairPlan.changed);
    TRUFFLE_CHECK(rollbackRepairPlan.shouldReplaceSavedPlan);
    TRUFFLE_CHECK(rollbackRepairPlan.repairedPlan.admittedCount == 2);
    TRUFFLE_CHECK(rollbackRepairPlan.rewrittenSelectedCount >= 1);
    TRUFFLE_CHECK(
        rollbackRepairPlan.repairedPlan.decisions[1].admission.coordinatorIndex == 0);
    const auto rollbackRepairDeltaPlan =
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_plan(
            rollbackRepairCoordinators, rollbackBatchPlan);
    TRUFFLE_CHECK(rollbackRepairDeltaPlan.repairable);
    TRUFFLE_CHECK(!rollbackRepairDeltaPlan.empty);
    TRUFFLE_CHECK(rollbackRepairDeltaPlan.deltaCount >= 1);
    TRUFFLE_CHECK(rollbackRepairDeltaPlan.changedCoordinatorCount >= 1);
    TRUFFLE_CHECK(rollbackRepairDeltaPlan.incompatibleCoordinatorReasonCount >= 1);
    TRUFFLE_CHECK(rollbackRepairDeltaPlan.reassignedCoordinatorReasonCount >= 1);
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_has_reason(
            rollbackRepairDeltaPlan.deltas[0].reasons,
            truffle::rhi::BindGroupDescriptorRuntimeBatchRepairDeltaReasonFlags::
                coordinator_incompatible));
    TRUFFLE_CHECK(
        truffle::rhi::bind_group_descriptor_runtime_batch_repair_delta_has_reason(
            rollbackRepairDeltaPlan.deltas[0].reasons,
            truffle::rhi::BindGroupDescriptorRuntimeBatchRepairDeltaReasonFlags::
                coordinator_reassigned));
    auto rollbackDeltaApplied =
        truffle::rhi::bind_group_descriptor_runtime_apply_batch_repair_delta_plan(
            rollbackBatchPlan, rollbackRepairDeltaPlan);
    TRUFFLE_CHECK(rollbackDeltaApplied.ok());
    TRUFFLE_CHECK(
        rollbackDeltaApplied.value().decisions[1].admission.coordinatorIndex == 0);
    const auto rollbackRepairExecutionResult =
        truffle::rhi::bind_group_descriptor_runtime_repair_and_execute_batch_admission_plan(
            rollbackCoordinators, rollbackBatchPlan);
    TRUFFLE_CHECK(rollbackRepairExecutionResult.status.ok());
    TRUFFLE_CHECK(!rollbackRepairExecutionResult.usedSavedPlan);
    TRUFFLE_CHECK(rollbackRepairExecutionResult.usedReplacementPlan);
    TRUFFLE_CHECK(rollbackRepairExecutionResult.repair.shouldReplaceSavedPlan);
    TRUFFLE_CHECK(rollbackRepairExecutionResult.execution.committedCount == 2);
    TRUFFLE_CHECK(persistentCoordinator.state().trackedReservationCount == 2);
    TRUFFLE_CHECK(persistentCoordinator.clear().ok());
    auto invalidTransientBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .allocationPolicy =
            truffle::rhi::BindGroupAllocationPolicy::transient_frame,
        .allocationFrameIndex = caps.maxFramesInFlight,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(!invalidTransientBindGroup.ok());
    TRUFFLE_CHECK(invalidTransientBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto invalidPersistentFrameBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .allocationFrameIndex = 1,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(!invalidPersistentFrameBindGroup.ok());
    TRUFFLE_CHECK(invalidPersistentFrameBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto invalidRebuildPersistentBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .reuseHint = truffle::rhi::BindGroupReuseHint::rebuild,
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(!invalidRebuildPersistentBindGroup.ok());
    TRUFFLE_CHECK(invalidRebuildPersistentBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    if (truffle::rhi::supports_descriptor_arrays(caps)) {
        auto arrayBindGroupLayout = device.create_bind_group_layout({
            .debugName = "contract_array_bind_group_layout",
            .bindings = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .visibility = truffle::rhi::ShaderStageFlags::fragment,
                    .arrayCount = 2,
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .visibility = truffle::rhi::ShaderStageFlags::fragment,
                    .arrayCount = 2,
                },
            },
        });
        TRUFFLE_CHECK(arrayBindGroupLayout.ok());
        auto arrayBindGroup = device.create_bind_group({
            .debugName = "contract_array_bind_group",
            .layout = arrayBindGroupLayout.value().get(),
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .textures = {goodTexture.value().get(), goodTexture.value().get()},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .samplers = {bindGroupSampler.value().get(),
                                 bindGroupSampler.value().get()},
                },
            },
        });
        TRUFFLE_CHECK(arrayBindGroup.ok());
        auto shortArrayBindGroup = device.create_bind_group({
            .layout = arrayBindGroupLayout.value().get(),
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .textures = {goodTexture.value().get()},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .samplers = {bindGroupSampler.value().get(),
                                 bindGroupSampler.value().get()},
                },
            },
        });
        TRUFFLE_CHECK(!shortArrayBindGroup.ok());
        TRUFFLE_CHECK(shortArrayBindGroup.status().code ==
                      truffle::core::StatusCode::invalid_argument);
    }
    if (auto foreignDevice = create_foreign_device(backendKind)) {
        auto foreignBuffer = foreignDevice->create_buffer({
            .size = 64,
            .usageFlags = truffle::rhi::BufferUsageFlags::uniform,
        });
        TRUFFLE_CHECK(foreignBuffer.ok());
        auto mixedBackendBindGroup = device.create_bind_group({
            .layout = bindGroupLayout.value().get(),
            .entries = {
                {
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::uniform_buffer,
                    .buffer = {.buffer = foreignBuffer.value().get(), .size = 16},
                },
                {
                    .bindingIndex = 1,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .texture = goodTexture.value().get(),
                },
                {
                    .bindingIndex = 2,
                    .type = truffle::rhi::BindingResourceType::sampler,
                    .sampler = bindGroupSampler.value().get(),
                },
            },
        });
        TRUFFLE_CHECK(!mixedBackendBindGroup.ok());
        TRUFFLE_CHECK(mixedBackendBindGroup.status().code ==
                      truffle::core::StatusCode::invalid_argument);
        if (truffle::rhi::supports_descriptor_arrays(caps)) {
            auto arrayBindGroupLayout = device.create_bind_group_layout({
                .bindings = {{
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .visibility = truffle::rhi::ShaderStageFlags::fragment,
                    .arrayCount = 2,
                }},
            });
            auto foreignTexture = foreignDevice->create_texture({
                .extent = {32, 32},
                .format = truffle::rhi::TextureFormat::rgba8_unorm,
                .usageFlags = truffle::rhi::TextureUsageFlags::sampled,
            });
            TRUFFLE_CHECK(arrayBindGroupLayout.ok());
            TRUFFLE_CHECK(foreignTexture.ok());
            auto mixedBackendArrayBindGroup = device.create_bind_group({
                .layout = arrayBindGroupLayout.value().get(),
                .entries = {{
                    .bindingIndex = 0,
                    .type = truffle::rhi::BindingResourceType::sampled_texture,
                    .textures = {goodTexture.value().get(),
                                 foreignTexture.value().get()},
                }},
            });
            TRUFFLE_CHECK(!mixedBackendArrayBindGroup.ok());
            TRUFFLE_CHECK(mixedBackendArrayBindGroup.status().code ==
                          truffle::core::StatusCode::invalid_argument);
        }
    }
    auto missingLayoutBindGroup = device.create_bind_group({
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
        }},
    });
    TRUFFLE_CHECK(!missingLayoutBindGroup.ok());
    TRUFFLE_CHECK(missingLayoutBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto missingEntryBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .entries = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::uniform_buffer,
            .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
        }},
    });
    TRUFFLE_CHECK(!missingEntryBindGroup.ok());
    TRUFFLE_CHECK(missingEntryBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto wrongUsageBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = goodBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = goodTexture.value().get(),
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(!wrongUsageBindGroup.ok());
    TRUFFLE_CHECK(wrongUsageBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto duplicateEntryBindGroup = device.create_bind_group({
        .layout = bindGroupLayout.value().get(),
        .entries = {
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 0,
                .type = truffle::rhi::BindingResourceType::uniform_buffer,
                .buffer = {.buffer = bindGroupUniformBuffer.value().get(), .size = 16},
            },
            {
                .bindingIndex = 2,
                .type = truffle::rhi::BindingResourceType::sampler,
                .sampler = bindGroupSampler.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(!duplicateEntryBindGroup.ok());
    TRUFFLE_CHECK(duplicateEntryBindGroup.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badSurface = device.create_surface({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {0, 0},
    });
    TRUFFLE_CHECK(!badSurface.ok());
    TRUFFLE_CHECK(badSurface.status().code == truffle::core::StatusCode::invalid_argument);

    auto invalidHeadlessSurface = device.create_surface({
        .native = {
            .kind = truffle::rhi::NativeSurfaceKind::headless,
            .handle = reinterpret_cast<void*>(0x1),
        },
        .initialExtent = {32, 32},
    });
    TRUFFLE_CHECK(!invalidHeadlessSurface.ok());
    TRUFFLE_CHECK(invalidHeadlessSurface.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto unsupportedWin32Surface = device.create_surface({
        .native = {
            .kind = truffle::rhi::NativeSurfaceKind::win32,
            .handle = reinterpret_cast<void*>(0x1),
        },
        .initialExtent = {32, 32},
    });
    TRUFFLE_CHECK(!unsupportedWin32Surface.ok());
    TRUFFLE_CHECK(unsupportedWin32Surface.status().code ==
                  truffle::core::StatusCode::unsupported);

    auto surface = device.create_surface({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {32, 32},
    });
    TRUFFLE_CHECK(surface.ok());

    auto badSwapchain = device.create_swapchain(*surface.value(), {
        .extent = {0, 0},
        .framesInFlight = 0,
    });
    TRUFFLE_CHECK(!badSwapchain.ok());
    TRUFFLE_CHECK(badSwapchain.status().code == truffle::core::StatusCode::invalid_argument);

    auto tooManyFramesSwapchain = device.create_swapchain(*surface.value(), {
        .extent = {32, 32},
        .framesInFlight = caps.maxFramesInFlight + 1,
    });
    TRUFFLE_CHECK(!tooManyFramesSwapchain.ok());
    TRUFFLE_CHECK(tooManyFramesSwapchain.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto tooManyImagesSwapchain = device.create_swapchain(*surface.value(), {
        .extent = {32, 32},
        .framesInFlight = 1,
        .imageCount = caps.maxFramesInFlight + 1,
    });
    TRUFFLE_CHECK(!tooManyImagesSwapchain.ok());
    TRUFFLE_CHECK(tooManyImagesSwapchain.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badRing = device.create_upload_ring(0, 0);
    TRUFFLE_CHECK(!badRing.ok());
    TRUFFLE_CHECK(badRing.status().code == truffle::core::StatusCode::invalid_argument);

    auto tooManyFramesRing =
        device.create_upload_ring(caps.maxFramesInFlight + 1, 128);
    TRUFFLE_CHECK(!tooManyFramesRing.ok());
    TRUFFLE_CHECK(tooManyFramesRing.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badShader = device.create_shader({
        .stage = truffle::rhi::ShaderStage::vertex,
    });
    TRUFFLE_CHECK(!badShader.ok());
    TRUFFLE_CHECK(badShader.status().code == truffle::core::StatusCode::invalid_argument);

    auto badEntryShaderDesc = make_shader_desc(backendKind, truffle::rhi::ShaderStage::vertex);
    badEntryShaderDesc.entryPoint.clear();
    auto badEntryShader = device.create_shader(badEntryShaderDesc);
    TRUFFLE_CHECK(!badEntryShader.ok());
    TRUFFLE_CHECK(badEntryShader.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto invalidSpirvShaderDesc =
        make_shader_desc(backendKind, truffle::rhi::ShaderStage::vertex);
    invalidSpirvShaderDesc.byteFormat = truffle::rhi::ShaderByteFormat::spirv_binary;
    invalidSpirvShaderDesc.bytecode = {std::byte{0x1}, std::byte{0x2}, std::byte{0x3}};
    auto invalidSpirvShader = device.create_shader(invalidSpirvShaderDesc);
    TRUFFLE_CHECK(!invalidSpirvShader.ok());
    TRUFFLE_CHECK(invalidSpirvShader.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto unsupportedShaderDesc =
        make_shader_desc(backendKind, truffle::rhi::ShaderStage::vertex);
    auto unsupportedFormat = truffle::rhi::ShaderByteFormat::dxil_binary;
    if (truffle::rhi::supports_shader_byte_format(caps, unsupportedFormat)) {
        unsupportedFormat = truffle::rhi::ShaderByteFormat::msl_source;
    }
    if (truffle::rhi::supports_shader_byte_format(caps, unsupportedFormat)) {
        unsupportedFormat = truffle::rhi::ShaderByteFormat::spirv_binary;
    }
    if (!truffle::rhi::supports_shader_byte_format(caps, unsupportedFormat)) {
        unsupportedShaderDesc.byteFormat = unsupportedFormat;
        if (unsupportedFormat == truffle::rhi::ShaderByteFormat::dxil_binary) {
            unsupportedShaderDesc.bytecode = {
                std::byte{'D'}, std::byte{'X'}, std::byte{'I'}, std::byte{'L'}};
        } else if (unsupportedFormat == truffle::rhi::ShaderByteFormat::spirv_binary) {
            unsupportedShaderDesc.bytecode = {
                std::byte{0x03}, std::byte{0x02}, std::byte{0x23}, std::byte{0x07}};
        }
        auto unsupportedShader = device.create_shader(unsupportedShaderDesc);
        TRUFFLE_CHECK(!unsupportedShader.ok());
        TRUFFLE_CHECK(unsupportedShader.status().code ==
                      truffle::core::StatusCode::unsupported);
    }

    auto stageVertexShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::vertex));
    auto stageFragmentShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::fragment));
    auto depthOnlyFragmentShader =
        device.create_shader(make_depth_only_fragment_shader_desc(backendKind));
    auto stageComputeShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::compute));
    TRUFFLE_CHECK(stageVertexShader.ok());
    TRUFFLE_CHECK(stageFragmentShader.ok());
    TRUFFLE_CHECK(depthOnlyFragmentShader.ok());
    TRUFFLE_CHECK(stageComputeShader.ok());
    auto wrongVertexStagePipeline = device.create_pipeline({
        .vertexShader = stageComputeShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
    });
    TRUFFLE_CHECK(!wrongVertexStagePipeline.ok());
    TRUFFLE_CHECK(wrongVertexStagePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto wrongFragmentStagePipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageVertexShader.value().get(),
    });
    TRUFFLE_CHECK(!wrongFragmentStagePipeline.ok());
    TRUFFLE_CHECK(wrongFragmentStagePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto wrongComputeStagePipeline = device.create_compute_pipeline({
        .computeShader = stageVertexShader.value().get(),
    });
    TRUFFLE_CHECK(!wrongComputeStagePipeline.ok());
    TRUFFLE_CHECK(wrongComputeStagePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto richRenderStatePipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthTest = false,
        .depthWrite = false,
        .rasterState = {
            .fillMode = truffle::rhi::FillMode::wireframe,
            .cullMode = truffle::rhi::CullMode::front,
            .frontFace = truffle::rhi::FrontFace::clockwise,
            .depthClip = false,
        },
        .colorBlend = {
            .enabled = true,
            .srcColor = truffle::rhi::BlendFactor::source_alpha,
            .dstColor = truffle::rhi::BlendFactor::one_minus_source_alpha,
            .srcAlpha = truffle::rhi::BlendFactor::one,
            .dstAlpha = truffle::rhi::BlendFactor::one_minus_source_alpha,
        },
        .vertexBuffers = {{
            .binding = 0,
            .stride = 16,
            .stepMode = truffle::rhi::VertexStepMode::vertex,
        }},
        .vertexAttributes = {{
            .location = 0,
            .binding = 0,
            .format = truffle::rhi::VertexFormat::float32x4,
            .offset = 0,
        }},
    });
    TRUFFLE_CHECK(richRenderStatePipeline.ok());
    TRUFFLE_CHECK(richRenderStatePipeline.value()->desc().rasterState.fillMode ==
                  truffle::rhi::FillMode::wireframe);
    TRUFFLE_CHECK(!richRenderStatePipeline.value()->desc().rasterState.depthClip);
    TRUFFLE_CHECK(richRenderStatePipeline.value()->desc().colorBlend.enabled);
    TRUFFLE_CHECK(richRenderStatePipeline.value()->desc().vertexAttributes.size() == 1);

    auto depthPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthTest = true,
        .depthWrite = true,
        .depthStencilState = {
            .depthCompare = truffle::rhi::SamplerCompareOp::greater_equal,
        },
    });
    TRUFFLE_CHECK(depthPipeline.ok());
    TRUFFLE_CHECK(depthPipeline.value()->desc().depthFormat ==
                  truffle::rhi::TextureFormat::depth32_float);
    auto depthStencilPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float_stencil8,
        .depthTest = true,
        .depthWrite = true,
        .depthStencilState = {
            .depthCompare = truffle::rhi::SamplerCompareOp::greater_equal,
        },
    });
    TRUFFLE_CHECK(depthStencilPipeline.ok());
    TRUFFLE_CHECK(depthStencilPipeline.value()->desc().depthFormat ==
                  truffle::rhi::TextureFormat::depth32_float_stencil8);
    auto stencilPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
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
    });
    TRUFFLE_CHECK(stencilPipeline.ok());
    TRUFFLE_CHECK(stencilPipeline.value()->desc().depthStencilState.stencilTest);
    auto depthOnlyPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = depthOnlyFragmentShader.value().get(),
        .colorFormat = truffle::rhi::TextureFormat::unknown,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthTest = true,
        .depthWrite = true,
    });
    TRUFFLE_CHECK(depthOnlyPipeline.ok());

    auto badLayoutPipeline = device.create_pipeline({
        .layout = {
            .bindings = {{
                .bindingIndex = caps.limits.maxResourceBindings,
                .visibility = truffle::rhi::ShaderStageFlags::vertex,
            }},
        },
    });
    TRUFFLE_CHECK(!badLayoutPipeline.ok());
    TRUFFLE_CHECK(badLayoutPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto duplicateLayoutPipeline = device.create_compute_pipeline({
        .layout = {
            .bindings = {
                {
                    .bindingIndex = 0,
                    .visibility = truffle::rhi::ShaderStageFlags::compute,
                },
                {
                    .bindingIndex = 0,
                    .visibility = truffle::rhi::ShaderStageFlags::compute,
                },
            },
        },
    });
    TRUFFLE_CHECK(!duplicateLayoutPipeline.ok());
    TRUFFLE_CHECK(duplicateLayoutPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto badRenderStatePipeline = device.create_pipeline({
        .colorFormat = truffle::rhi::TextureFormat::depth32_float,
    });
    TRUFFLE_CHECK(!badRenderStatePipeline.ok());
    TRUFFLE_CHECK(badRenderStatePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badDepthPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .depthTest = true,
    });
    TRUFFLE_CHECK(!badDepthPipeline.ok());
    TRUFFLE_CHECK(badDepthPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badDepthFormatPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .depthFormat = truffle::rhi::TextureFormat::rgba8_unorm,
    });
    TRUFFLE_CHECK(!badDepthFormatPipeline.ok());
    TRUFFLE_CHECK(badDepthFormatPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badStencilDepthFormatPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .colorFormat = truffle::rhi::TextureFormat::rgba8_unorm,
        .depthFormat = truffle::rhi::TextureFormat::depth32_float,
        .depthStencilState = {
            .stencilTest = true,
        },
    });
    TRUFFLE_CHECK(!badStencilDepthFormatPipeline.ok());
    TRUFFLE_CHECK(badStencilDepthFormatPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badRasterStatePipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .depthTest = false,
        .depthWrite = false,
        .rasterState = {
            .cullMode = static_cast<truffle::rhi::CullMode>(99),
        },
    });
    TRUFFLE_CHECK(!badRasterStatePipeline.ok());
    TRUFFLE_CHECK(badRasterStatePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badBlendStatePipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .depthTest = false,
        .depthWrite = false,
        .colorBlend = {
            .srcColor = static_cast<truffle::rhi::BlendFactor>(99),
        },
    });
    TRUFFLE_CHECK(!badBlendStatePipeline.ok());
    TRUFFLE_CHECK(badBlendStatePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badVertexInputPipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
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
    });
    TRUFFLE_CHECK(!badVertexInputPipeline.ok());
    TRUFFLE_CHECK(badVertexInputPipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);
    auto badVertexStridePipeline = device.create_pipeline({
        .vertexShader = stageVertexShader.value().get(),
        .fragmentShader = stageFragmentShader.value().get(),
        .depthTest = false,
        .depthWrite = false,
        .vertexBuffers = {{
            .binding = 0,
            .stride = device.capabilities().limits.maxVertexBufferStride + 1,
        }},
    });
    TRUFFLE_CHECK(!badVertexStridePipeline.ok());
    TRUFFLE_CHECK(badVertexStridePipeline.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    auto depthCmd = device.create_command_buffer();
    TRUFFLE_CHECK(depthCmd != nullptr);
    TRUFFLE_CHECK(depthCmd->begin().ok());
    truffle::rhi::RenderPassDesc depthPassDesc{
        .extent = {32, 32},
    };
    depthPassDesc.colorAttachment.texture = goodTexture.value().get();
    depthPassDesc.depthAttachment.texture = depthTexture.value().get();
    TRUFFLE_CHECK(!truffle::rhi::validation::pipeline_render_pass_compatible(
        richRenderStatePipeline.value()->desc(),
        goodTexture.value()->desc().format,
        depthTexture.value()->desc().format));
    TRUFFLE_CHECK(depthCmd->begin_render_pass(depthPassDesc).ok());
    TRUFFLE_CHECK(!depthCmd->bind_pipeline(*richRenderStatePipeline.value()).ok());
    TRUFFLE_CHECK(depthCmd->bind_pipeline(*depthPipeline.value()).ok());
    TRUFFLE_CHECK(depthCmd->end_render_pass().ok());
    TRUFFLE_CHECK(depthCmd->end().ok());

    auto noDepthCmd = device.create_command_buffer();
    TRUFFLE_CHECK(noDepthCmd != nullptr);
    TRUFFLE_CHECK(noDepthCmd->begin().ok());
    truffle::rhi::RenderPassDesc noDepthPassDesc{
        .extent = {32, 32},
    };
    noDepthPassDesc.colorAttachment.texture = goodTexture.value().get();
    TRUFFLE_CHECK(noDepthCmd->begin_render_pass(noDepthPassDesc).ok());
    TRUFFLE_CHECK(!noDepthCmd->bind_pipeline(*depthPipeline.value()).ok());
    TRUFFLE_CHECK(noDepthCmd->bind_pipeline(*richRenderStatePipeline.value()).ok());
    TRUFFLE_CHECK(noDepthCmd->end_render_pass().ok());
    TRUFFLE_CHECK(noDepthCmd->end().ok());

    auto depthOnlyCmd = device.create_command_buffer();
    TRUFFLE_CHECK(depthOnlyCmd != nullptr);
    TRUFFLE_CHECK(depthOnlyCmd->begin().ok());
    truffle::rhi::RenderPassDesc depthOnlyPassDesc{
        .extent = {32, 32},
    };
    depthOnlyPassDesc.depthAttachment.texture = depthTexture.value().get();
    TRUFFLE_CHECK(depthOnlyCmd->begin_render_pass(depthOnlyPassDesc).ok());
    TRUFFLE_CHECK(!depthOnlyCmd->bind_pipeline(*depthPipeline.value()).ok());
    TRUFFLE_CHECK(depthOnlyCmd->bind_pipeline(*depthOnlyPipeline.value()).ok());
    TRUFFLE_CHECK(depthOnlyCmd->end_render_pass().ok());
    TRUFFLE_CHECK(depthOnlyCmd->end().ok());

    auto depthStencilCmd = device.create_command_buffer();
    TRUFFLE_CHECK(depthStencilCmd != nullptr);
    TRUFFLE_CHECK(depthStencilCmd->begin().ok());
    truffle::rhi::RenderPassDesc depthStencilPassDesc{
        .extent = {32, 32},
    };
    depthStencilPassDesc.colorAttachment.texture = goodTexture.value().get();
    depthStencilPassDesc.depthAttachment.texture =
        depthStencilTexture.value().get();
    depthStencilPassDesc.depthAttachment.stencilLoadOp =
        truffle::rhi::LoadOp::clear;
    depthStencilPassDesc.depthAttachment.stencilStoreOp =
        truffle::rhi::StoreOp::store;
    depthStencilPassDesc.depthAttachment.clearStencil = 5;
    TRUFFLE_CHECK(depthStencilCmd->begin_render_pass(depthStencilPassDesc).ok());
    TRUFFLE_CHECK(!depthStencilCmd->bind_pipeline(*depthPipeline.value()).ok());
    TRUFFLE_CHECK(depthStencilCmd->bind_pipeline(*stencilPipeline.value()).ok());
    TRUFFLE_CHECK(depthStencilCmd->set_stencil_reference(11).ok());
    TRUFFLE_CHECK(depthStencilCmd->end_render_pass().ok());
    TRUFFLE_CHECK(depthStencilCmd->end().ok());

    auto badStencilPassCmd = device.create_command_buffer();
    TRUFFLE_CHECK(badStencilPassCmd != nullptr);
    TRUFFLE_CHECK(badStencilPassCmd->begin().ok());
    truffle::rhi::RenderPassDesc badStencilPassDesc{
        .extent = {32, 32},
    };
    badStencilPassDesc.depthAttachment.texture = depthTexture.value().get();
    badStencilPassDesc.depthAttachment.stencilLoadOp =
        truffle::rhi::LoadOp::clear;
    TRUFFLE_CHECK(!badStencilPassCmd->begin_render_pass(badStencilPassDesc).ok());
    TRUFFLE_CHECK(badStencilPassCmd->end().ok());

    auto badStencilRefCmd = device.create_command_buffer();
    TRUFFLE_CHECK(badStencilRefCmd != nullptr);
    TRUFFLE_CHECK(badStencilRefCmd->begin().ok());
    TRUFFLE_CHECK(!badStencilRefCmd->set_stencil_reference(1).ok());
    TRUFFLE_CHECK(badStencilRefCmd->begin_render_pass(depthPassDesc).ok());
    TRUFFLE_CHECK(!badStencilRefCmd->set_stencil_reference(2).ok());
    TRUFFLE_CHECK(badStencilRefCmd->end_render_pass().ok());
    TRUFFLE_CHECK(badStencilRefCmd->end().ok());

    auto cmd = device.create_command_buffer();
    TRUFFLE_CHECK(cmd != nullptr);
    auto badEnd = cmd->end();
    TRUFFLE_CHECK(!badEnd.ok());
    TRUFFLE_CHECK(badEnd.code == truffle::core::StatusCode::invalid_state);

    const auto positivePath =
        verify_common_positive_path_contract(device, *surface.value(), backendKind);
    TRUFFLE_CHECK(positivePath == 0);

    return 0;
}

int verify_backend_contract(std::unique_ptr<truffle::rhi::IBackend> backend) {
    const auto backendKind = backend->kind();
    TRUFFLE_CHECK(backend->backend_stats().devicesCreated == 0);
    TRUFFLE_CHECK(backend->recent_events().empty());
    const auto emptyReport = truffle::rhi::collect_backend_parity_report(*backend);
    TRUFFLE_CHECK(emptyReport.backend == backendKind);
    TRUFFLE_CHECK(emptyReport.stats.devicesCreated == 0);

    auto badDevice = backend->create_device({.adapterId = 99});
    TRUFFLE_CHECK(!badDevice.ok());
    TRUFFLE_CHECK(badDevice.status().code == truffle::core::StatusCode::unavailable);
    TRUFFLE_CHECK(backend->backend_stats().devicesCreated == 0);

    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    TRUFFLE_CHECK(verify_capability_contract(*backend, *device) == 0);
    TRUFFLE_CHECK(verify_common_device_contract(*device, backendKind) == 0);
    return verify_backend_diagnostics_contract(*backend);
}

} // namespace

int main() {
    if (verify_backend_contract(truffle::rhi::create_null_backend()) != 0) {
        return 1;
    }
#if defined(TRUFFLE_HAS_VULKAN_BACKEND)
    if (verify_backend_contract(truffle::rhi::create_vulkan_backend()) != 0) {
        return 1;
    }
#endif
#if defined(TRUFFLE_HAS_OPENGL_BACKEND)
    if (verify_backend_contract(truffle::rhi::create_opengl_backend()) != 0) {
        return 1;
    }
#endif
#if defined(TRUFFLE_HAS_DIRECT3D_BACKEND)
    if (verify_backend_contract(truffle::rhi::create_direct3d_backend()) != 0) {
        return 1;
    }
#endif
#if defined(TRUFFLE_HAS_METAL_BACKEND)
    auto metalBackend = truffle::rhi::create_metal_backend();
    if (!metalBackend->enumerate_adapters().empty()) {
        if (verify_backend_contract(std::move(metalBackend)) != 0) {
            return 1;
        }
    }
#endif
    return 0;
}
