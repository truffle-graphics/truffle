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
    TRUFFLE_CHECK(caps.limits.maxTextureDimension2D >= 32);
    TRUFFLE_CHECK(caps.limits.maxBufferSize >= 128);
    TRUFFLE_CHECK(caps.limits.minUniformBufferOffsetAlignment >= 1);
    TRUFFLE_CHECK(caps.limits.minStorageBufferOffsetAlignment >= 1);
    TRUFFLE_CHECK(caps.limits.maxColorAttachments >= 1);
    TRUFFLE_CHECK(caps.limits.maxVertexBuffers >= 1);
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
            .entryPoint = "comp_main",
            .bytecode = to_bytes(kComputeMSL),
        };
    }

    return truffle::rhi::ShaderDesc{
        .stage = stage,
        .entryPoint = "main",
        .bytecode = {std::byte{0x1}, std::byte{0x2}},
    };
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

    auto pipeline = device.create_pipeline({
        .vertexShader = vertexShader.value().get(),
        .fragmentShader = fragmentShader.value().get(),
    });
    TRUFFLE_CHECK(pipeline.ok());
    TRUFFLE_CHECK(verify_reflection_invariants(pipeline.value()->reflection(), false));

    auto computePipeline = device.create_compute_pipeline({
        .computeShader = computeShader.value().get(),
    });
    TRUFFLE_CHECK(computePipeline.ok());
    TRUFFLE_CHECK(verify_reflection_invariants(computePipeline.value()->reflection(), true));

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

    auto commandBuffer = device.create_command_buffer();
    TRUFFLE_CHECK(commandBuffer != nullptr);
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::initial);
    TRUFFLE_CHECK(commandBuffer->begin().ok());
    TRUFFLE_CHECK(commandBuffer->state() == truffle::rhi::CommandBufferState::recording);
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
    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = swapchain.value()->desc().extent;
    passDesc.colorAttachment.texture = swapchain.value()->acquire_next_texture();
    TRUFFLE_CHECK(stateCmd->begin_render_pass(passDesc).ok());
    TRUFFLE_CHECK(!stateCmd->begin_render_pass(passDesc).ok());
    TRUFFLE_CHECK(!stateCmd->resource_barrier(
        truffle::rhi::BufferBarrierDesc{
            .buffer = storageBuffer.value().get(),
            .before = truffle::rhi::ResourceState::undefined,
            .after = truffle::rhi::ResourceState::storage_read_write,
        }).ok());
    TRUFFLE_CHECK(stateCmd->bind_pipeline(*pipeline.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_vertex_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_vertex_buffer(0, *vertexUniformBuffer.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_uniform_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_uniform_buffer(0, *vertexUniformBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_index_buffer(*indexBuffer.value()).ok());
    TRUFFLE_CHECK(!stateCmd->draw_indirect(*indexBuffer.value(), 0).ok());
    TRUFFLE_CHECK(stateCmd->draw_indirect(*indirectBuffer.value(), 0).ok());
    TRUFFLE_CHECK(!stateCmd->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(!stateCmd->end().ok());
    TRUFFLE_CHECK(stateCmd->end_render_pass().ok());
    TRUFFLE_CHECK(!stateCmd->end_render_pass().ok());
    TRUFFLE_CHECK(stateCmd->bind_compute_pipeline(*computePipeline.value()).ok());
    TRUFFLE_CHECK(!stateCmd->bind_storage_buffer(0, *indexBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->bind_storage_buffer(0, *storageBuffer.value()).ok());
    TRUFFLE_CHECK(stateCmd->end().ok());

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
    auto badDevice = backend->create_device({.adapterId = 99});
    TRUFFLE_CHECK(!badDevice.ok());
    TRUFFLE_CHECK(badDevice.status().code == truffle::core::StatusCode::unavailable);

    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    TRUFFLE_CHECK(verify_capability_contract(*backend, *device) == 0);
    return verify_common_device_contract(*device, backendKind);
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
