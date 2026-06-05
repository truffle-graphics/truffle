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
    TRUFFLE_CHECK(caps.limits.maxDescriptorArrayElements >= 1);
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

    const auto* colorSupport = truffle::rhi::find_format_support(
        caps, truffle::rhi::TextureFormat::bgra8_unorm);
    TRUFFLE_CHECK(colorSupport != nullptr);
    TRUFFLE_CHECK(colorSupport->sampled);
    TRUFFLE_CHECK(colorSupport->colorAttachment);

    const auto* depthSupport = truffle::rhi::find_format_support(
        caps, truffle::rhi::TextureFormat::depth32_float);
    TRUFFLE_CHECK(depthSupport != nullptr);
    TRUFFLE_CHECK(depthSupport->depthStencilAttachment);

    const bool expectsReflection = backend.kind() != truffle::rhi::BackendKind::null_backend;
    TRUFFLE_CHECK(caps.features.shaderReflection == expectsReflection);

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
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .visibility = truffle::rhi::ShaderStageFlags::fragment,
                .arrayCount = 1,
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

    const truffle::rhi::PipelineLayoutDesc computeLayout{
        .debugName = "contract_compute_layout",
        .bindings = {{
            .bindingIndex = 0,
            .type = truffle::rhi::BindingResourceType::storage_buffer,
            .visibility = truffle::rhi::ShaderStageFlags::compute,
            .minBindingSize = 16,
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
        .bindings = graphicsLayout.bindings,
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
            {
                .bindingIndex = 1,
                .type = truffle::rhi::BindingResourceType::sampled_texture,
                .texture = barrierTexture.value().get(),
            },
        },
    });
    TRUFFLE_CHECK(graphicsBindGroup.ok());

    auto commandBuffer = device.create_command_buffer();
    TRUFFLE_CHECK(commandBuffer != nullptr);
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::initial);
    TRUFFLE_CHECK(commandBuffer->begin().ok());
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::recording);
    TRUFFLE_CHECK(commandBuffer->bind_group(0, *graphicsBindGroup.value()).ok());
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
    TRUFFLE_CHECK(!stateCmd->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = storageBuffer.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::storage_read_write,
        }).ok());
    TRUFFLE_CHECK(!stateCmd->bind_pipeline(foreignPipeline).ok());
    TRUFFLE_CHECK(stateCmd->bind_pipeline(*pipeline.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_group(0, *graphicsBindGroup.value()).ok());
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
    TRUFFLE_CHECK(stateCmd->bind_group(0, *computeBindGroup.value()).ok());
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
    TRUFFLE_CHECK(report.maxDescriptorArrayElements ==
                  reportedCaps.limits.maxDescriptorArrayElements);
    TRUFFLE_CHECK(report.maxBindlessResources ==
                  reportedCaps.limits.maxBindlessResources);
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
    auto bindGroupLayout = device.create_bind_group_layout({
        .debugName = "contract_negative_bind_group_layout",
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
    auto validBindGroup = device.create_bind_group({
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
    auto stageComputeShader =
        device.create_shader(make_shader_desc(backendKind, truffle::rhi::ShaderStage::compute));
    TRUFFLE_CHECK(stageVertexShader.ok());
    TRUFFLE_CHECK(stageFragmentShader.ok());
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
