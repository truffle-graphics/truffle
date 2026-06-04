#include "test_support.hpp"
#include "truffle/rhi/direct3d_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"

static bool has_binding(const truffle::rhi::IPipelineReflection& reflection,
                        truffle::rhi::ShaderStage stage,
                        truffle::rhi::ResourceBindingType type,
                        std::uint32_t bindingIndex) {
    for (std::size_t i = 0; i < reflection.get_binding_count(); ++i) {
        const auto& binding = reflection.get_binding_info(i);
        if (binding.stage == stage && binding.type == type &&
            binding.bindingIndex == bindingIndex) {
            return true;
        }
    }
    return false;
}

int main() {
    auto backend = truffle::rhi::create_direct3d_backend();

    auto adapters = backend->enumerate_adapters();
    TRUFFLE_CHECK(!adapters.empty());
    TRUFFLE_CHECK(adapters[0].backend == truffle::rhi::BackendKind::direct3d);

    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics).kind() ==
                  truffle::rhi::QueueKind::graphics);
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::compute).kind() ==
                  truffle::rhi::QueueKind::compute);
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::transfer).kind() ==
                  truffle::rhi::QueueKind::transfer);

    auto cmd = device->create_command_buffer();
    TRUFFLE_CHECK(cmd != nullptr);
    TRUFFLE_CHECK(cmd->begin().ok());
    TRUFFLE_CHECK(cmd->end().ok());

    auto fence = device->create_fence({.signaled = false});
    TRUFFLE_CHECK(!fence->signaled());
    TRUFFLE_CHECK(
        device->queue(truffle::rhi::QueueKind::graphics).submit(*cmd, fence.get()).ok());
    TRUFFLE_CHECK(fence->signaled());

    TRUFFLE_CHECK(!device->create_surface({
                      .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
                      .initialExtent = {0, 0},
                  }).ok());
    auto surfaceResult = device->create_surface({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {32, 32},
    });
    TRUFFLE_CHECK(surfaceResult.ok());
    auto surface = std::move(surfaceResult).value();

    TRUFFLE_CHECK(!device->create_swapchain(*surface, {
                      .extent = {0, 0},
                      .framesInFlight = 0,
                  }).ok());
    auto swapchainResult = device->create_swapchain(*surface, {
        .extent = {32, 32},
        .framesInFlight = 2,
    });
    TRUFFLE_CHECK(swapchainResult.ok());
    auto swapchain = std::move(swapchainResult).value();
    TRUFFLE_CHECK(swapchain->acquire_next_texture() != nullptr);
    TRUFFLE_CHECK(swapchain->resize({64, 64}).ok());
    TRUFFLE_CHECK(!swapchain->schedule_present(*cmd).ok());
    auto presentCmd = device->create_command_buffer();
    TRUFFLE_CHECK(presentCmd->begin().ok());
    TRUFFLE_CHECK(swapchain->schedule_present(*presentCmd).ok());
    TRUFFLE_CHECK(presentCmd->end().ok());
    TRUFFLE_CHECK(!swapchain->schedule_present(*presentCmd).ok());

    // State machine enforcement checks
    auto cmdInvalid = device->create_command_buffer();
    TRUFFLE_CHECK(!cmdInvalid->end().ok());
    TRUFFLE_CHECK(cmdInvalid->begin().ok());
    TRUFFLE_CHECK(!cmdInvalid->begin().ok());

    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = {16, 16};
    TRUFFLE_CHECK(cmdInvalid->begin_render_pass(passDesc).ok());
    TRUFFLE_CHECK(!cmdInvalid->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(cmdInvalid->end_render_pass().ok());
    TRUFFLE_CHECK(cmdInvalid->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(cmdInvalid->end().ok());

    // Milestone 1 resource foundation checks.
    TRUFFLE_CHECK(!device->create_buffer({
                      .size = 0,
                      .usage = truffle::rhi::BufferUsage::vertex,
                  }).ok());
    auto bufferResult = device->create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(bufferResult.ok());

    auto textureResult = device->create_texture({
        .extent = {16, 16},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
    });
    TRUFFLE_CHECK(textureResult.ok());

    auto samplerResult = device->create_sampler({});
    TRUFFLE_CHECK(samplerResult.ok());

    auto ringResult = device->create_upload_ring(2, 256);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();
    auto alloc = ring->allocate(64, 16);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.mappedPtr != nullptr);
    ring->advance();

    TRUFFLE_CHECK(!device->create_shader({
                      .stage = truffle::rhi::ShaderStage::vertex,
                  }).ok());

    auto vertexShaderResult = device->create_shader({
        .stage = truffle::rhi::ShaderStage::vertex,
        .bytecode = {std::byte{0x1}},
    });
    auto fragmentShaderResult = device->create_shader({
        .stage = truffle::rhi::ShaderStage::fragment,
        .bytecode = {std::byte{0x2}},
    });
    auto computeShaderResult = device->create_shader({
        .stage = truffle::rhi::ShaderStage::compute,
        .bytecode = {std::byte{0x3}},
    });
    TRUFFLE_CHECK(vertexShaderResult.ok());
    TRUFFLE_CHECK(fragmentShaderResult.ok());
    TRUFFLE_CHECK(computeShaderResult.ok());

    auto vertexShader = std::move(vertexShaderResult).value();
    auto fragmentShader = std::move(fragmentShaderResult).value();
    auto computeShader = std::move(computeShaderResult).value();

    TRUFFLE_CHECK(!device->create_pipeline({
                      .vertexShader = vertexShader.get(),
                  }).ok());
    TRUFFLE_CHECK(!device->create_compute_pipeline({}).ok());

    auto pipelineResult = device->create_pipeline({
        .vertexShader = vertexShader.get(),
        .fragmentShader = fragmentShader.get(),
    });
    auto computePipelineResult = device->create_compute_pipeline({
        .computeShader = computeShader.get(),
    });
    TRUFFLE_CHECK(pipelineResult.ok());
    TRUFFLE_CHECK(computePipelineResult.ok());

    auto pipeline = std::move(pipelineResult).value();
    auto computePipeline = std::move(computePipelineResult).value();

    const auto* graphicsReflection = pipeline->reflection();
    TRUFFLE_CHECK(graphicsReflection != nullptr);
    TRUFFLE_CHECK(graphicsReflection->get_binding_count() == 2);
    TRUFFLE_CHECK(has_binding(*graphicsReflection,
                              truffle::rhi::ShaderStage::vertex,
                              truffle::rhi::ResourceBindingType::Buffer,
                              0));
    TRUFFLE_CHECK(has_binding(*graphicsReflection,
                              truffle::rhi::ShaderStage::fragment,
                              truffle::rhi::ResourceBindingType::Buffer,
                              1));

    const auto* computeReflection = computePipeline->reflection();
    TRUFFLE_CHECK(computeReflection != nullptr);
    TRUFFLE_CHECK(computeReflection->get_binding_count() == 3);
    TRUFFLE_CHECK(has_binding(*computeReflection,
                              truffle::rhi::ShaderStage::compute,
                              truffle::rhi::ResourceBindingType::Buffer,
                              0));
    TRUFFLE_CHECK(has_binding(*computeReflection,
                              truffle::rhi::ShaderStage::compute,
                              truffle::rhi::ResourceBindingType::Buffer,
                              1));
    TRUFFLE_CHECK(has_binding(*computeReflection,
                              truffle::rhi::ShaderStage::compute,
                              truffle::rhi::ResourceBindingType::Buffer,
                              2));

    return 0;
}
