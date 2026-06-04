#include "test_support.hpp"
#include "truffle/render/frame_graph.hpp"
#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/render/shaders.hpp"
#include "truffle/render/transform_compute_pass.hpp"

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal MSL shaders — fullscreen triangle, no vertex inputs
// ---------------------------------------------------------------------------

static const char kVertexMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
vertex float4 vert_main(uint vid [[vertex_id]]) {
    const float2 pos[3] = {{-1,-1},{3,-1},{-1,3}};
    return float4(pos[vid % 3], 0.0, 1.0);
}
)msl";

static const char kFragmentMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
fragment float4 frag_main() {
    return float4(0.5, 0.5, 0.5, 1.0);
}
)msl";

static const char kVertexWithBufferMSL[] = R"msl(
#include <metal_stdlib>
using namespace metal;
vertex float4 vert_with_buffer(uint vid [[vertex_id]],
                               constant float4* positions [[buffer(0)]]) {
    return positions[vid % 3];
}
)msl";

static std::vector<std::byte> to_bytes(const char* src) {
    const auto* p = reinterpret_cast<const std::byte*>(src);
    return {p, p + std::strlen(src)};
}

static bool has_compute_buffer_binding(const truffle::rhi::IPipelineReflection& reflection,
                                       std::uint32_t index) {
    for (std::size_t i = 0; i < reflection.get_binding_count(); ++i) {
        const auto& b = reflection.get_binding_info(i);
        if (b.stage == truffle::rhi::ShaderStage::compute &&
            b.type == truffle::rhi::ResourceBindingType::Buffer &&
            b.bindingIndex == index) {
            return true;
        }
    }
    return false;
}

int main() {
    auto backend = truffle::rhi::create_metal_backend();

    // Skip gracefully when no Metal GPU is present (e.g. pure-CPU CI VMs)
    if (backend->enumerate_adapters().empty()) {
        return 0;
    }

    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    // --- Buffer ---
    TRUFFLE_CHECK(!device->create_buffer({}).ok()); // size==0 must fail
    auto bufResult = device->create_buffer({
        .size      = 256,
        .usage     = truffle::rhi::BufferUsage::vertex,
        .debugName = "triangle_vb",
    });
    TRUFFLE_CHECK(bufResult.ok());
    auto vb = std::move(bufResult).value();

    // --- Texture ---
    auto texResult = device->create_texture({
        .extent    = {64, 64},
        .format    = truffle::rhi::TextureFormat::rgba8_unorm,
        .debugName = "test_texture",
    });
    TRUFFLE_CHECK(texResult.ok());

    // --- Sampler ---
    TRUFFLE_CHECK(device->create_sampler({}).ok());

    // --- Shaders (MSL source as bytecode) ---
    auto vertResult = device->create_shader({
        .stage      = truffle::rhi::ShaderStage::vertex,
        .entryPoint = "vert_main",
        .bytecode   = to_bytes(kVertexMSL),
    });
    TRUFFLE_CHECK(vertResult.ok());
    auto vertShader = std::move(vertResult).value();

    auto fragResult = device->create_shader({
        .stage      = truffle::rhi::ShaderStage::fragment,
        .entryPoint = "frag_main",
        .bytecode   = to_bytes(kFragmentMSL),
    });
    TRUFFLE_CHECK(fragResult.ok());
    auto fragShader = std::move(fragResult).value();

    // Pipeline requires both shaders
    TRUFFLE_CHECK(!device->create_pipeline({.debugName = "no_shaders"}).ok());

    auto pipelineResult = device->create_pipeline({
        .debugName      = "test_pipeline",
        .vertexShader   = vertShader.get(),
        .fragmentShader = fragShader.get(),
    });
    TRUFFLE_CHECK(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    TRUFFLE_CHECK(pipeline->reflection() != nullptr);

    // --- Headless surface + swapchain ---
    auto surfaceResult = device->create_surface({
        .native        = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {320, 240},
    });
    TRUFFLE_CHECK(surfaceResult.ok());
    auto surface = std::move(surfaceResult).value();

    auto swapchainResult = device->create_swapchain(
        *surface, {.extent = {320, 240}, .framesInFlight = 2});
    TRUFFLE_CHECK(swapchainResult.ok());
    auto swapchain = std::move(swapchainResult).value();

    // --- acquire_next_texture ---
    auto* drawable = swapchain->acquire_next_texture();
    TRUFFLE_CHECK(drawable != nullptr);

    // --- Full render pass sequence ---
    auto cmd = device->create_command_buffer();
    TRUFFLE_CHECK(cmd->begin().ok());

    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent                  = {320, 240};
    passDesc.colorAttachment.texture = drawable;
    TRUFFLE_CHECK(cmd->begin_render_pass(passDesc).ok());

    TRUFFLE_CHECK(cmd->bind_pipeline(*pipeline).ok());
    TRUFFLE_CHECK(cmd->bind_vertex_buffer(0, *vb).ok());
    TRUFFLE_CHECK(cmd->set_viewport(0, 0, 320, 240).ok());
    TRUFFLE_CHECK(cmd->set_scissor(0, 0, 320, 240).ok());
    TRUFFLE_CHECK(cmd->draw(3).ok());

    TRUFFLE_CHECK(cmd->end_render_pass().ok());
    TRUFFLE_CHECK(swapchain->schedule_present(*cmd).ok());
    TRUFFLE_CHECK(cmd->end().ok());

    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics).submit(*cmd).ok());

    // --- Second frame: re-acquire + instanced draw ---
    auto* drawable2 = swapchain->acquire_next_texture();
    TRUFFLE_CHECK(drawable2 != nullptr);

    auto cmd2 = device->create_command_buffer();
    TRUFFLE_CHECK(cmd2->begin().ok());

    truffle::rhi::RenderPassDesc passDesc2;
    passDesc2.extent                  = {320, 240};
    passDesc2.colorAttachment.texture = drawable2;
    TRUFFLE_CHECK(cmd2->begin_render_pass(passDesc2).ok());
    TRUFFLE_CHECK(cmd2->bind_pipeline(*pipeline).ok());
    TRUFFLE_CHECK(cmd2->draw_instanced(3, 4).ok());
    TRUFFLE_CHECK(cmd2->end_render_pass().ok());
    TRUFFLE_CHECK(swapchain->schedule_present(*cmd2).ok());
    TRUFFLE_CHECK(cmd2->end().ok());
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics).submit(*cmd2).ok());

    // --- Frame upload ring ---
    auto ringResult = device->create_upload_ring(2, 512 * 1024);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    auto alloc = ring->allocate(64);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.mappedPtr != nullptr);
    // Write to the mapped region to verify CPU write access
    std::memset(alloc.mappedPtr, 0xFF, 64);
    ring->advance();

    // --- Fence ---
    auto fence = device->create_fence({});
    TRUFFLE_CHECK(!fence->signaled());

    auto cmd3 = device->create_command_buffer();
    TRUFFLE_CHECK(cmd3->begin().ok());
    TRUFFLE_CHECK(cmd3->end().ok());
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics)
                      .submit(*cmd3, fence.get())
                      .ok());
    fence->wait(); // async completion handler — block until GPU signals
    TRUFFLE_CHECK(fence->signaled());

    // --- Swapchain resize ---
    TRUFFLE_CHECK(swapchain->resize({640, 480}).ok());

    // --- Indexed draw ---
    auto ibResult = device->create_buffer({
        .size      = 64,
        .usage     = truffle::rhi::BufferUsage::index,
        .debugName = "index_buffer",
    });
    TRUFFLE_CHECK(ibResult.ok());
    auto ib = std::move(ibResult).value();

    auto* drawable3 = swapchain->acquire_next_texture();
    TRUFFLE_CHECK(drawable3 != nullptr);

    auto cmdIdx = device->create_command_buffer();
    TRUFFLE_CHECK(cmdIdx->begin().ok());
    truffle::rhi::RenderPassDesc passIdx;
    passIdx.extent                  = {640, 480};
    passIdx.colorAttachment.texture = drawable3;
    TRUFFLE_CHECK(cmdIdx->begin_render_pass(passIdx).ok());
    TRUFFLE_CHECK(cmdIdx->bind_pipeline(*pipeline).ok());
    TRUFFLE_CHECK(cmdIdx->bind_index_buffer(*ib, 0,
        truffle::rhi::IndexFormat::uint32).ok());
    TRUFFLE_CHECK(cmdIdx->draw_indexed(3).ok());
    TRUFFLE_CHECK(cmdIdx->draw_indexed_instanced(3, 2).ok());
    
    // --- Indirect draw ---
    auto indirectBufResult = device->create_buffer({
        .size      = 256,
        .usage     = truffle::rhi::BufferUsage::indirect,
        .debugName = "indirect_buffer",
    });
    TRUFFLE_CHECK(indirectBufResult.ok());
    auto indirectBuf = std::move(indirectBufResult).value();
    
    TRUFFLE_CHECK(cmdIdx->draw_indirect(*indirectBuf, 0).ok());
    TRUFFLE_CHECK(cmdIdx->draw_indexed_indirect(*indirectBuf, 0).ok());
    
    TRUFFLE_CHECK(cmdIdx->end_render_pass().ok());
    TRUFFLE_CHECK(swapchain->schedule_present(*cmdIdx).ok());
    TRUFFLE_CHECK(cmdIdx->end().ok());
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics)
                      .submit(*cmdIdx)
                      .ok());


    // --- Compute Backend Test ---
    auto computeShaderResult = device->create_shader({
        .stage      = truffle::rhi::ShaderStage::compute,
        .entryPoint = "compute_transforms",
        .bytecode   = to_bytes(truffle::render::kTransformComputeMSL.data()),
    });
    TRUFFLE_CHECK(computeShaderResult.ok());
    auto computeShader = std::move(computeShaderResult).value();
    
    auto computePipelineResult = device->create_compute_pipeline({
        .debugName = "test_compute_pipeline",
        .computeShader = computeShader.get(),
    });
    TRUFFLE_CHECK(computePipelineResult.ok());
    auto computePipeline = std::move(computePipelineResult).value();
    TRUFFLE_CHECK(computePipeline->reflection() != nullptr);
    TRUFFLE_CHECK(has_compute_buffer_binding(*computePipeline->reflection(), 0));
    TRUFFLE_CHECK(has_compute_buffer_binding(*computePipeline->reflection(), 1));
    TRUFFLE_CHECK(has_compute_buffer_binding(*computePipeline->reflection(), 2));

    auto localBuf = device->create_buffer({.size = 1024, .usage = truffle::rhi::BufferUsage::storage}).value();
    auto parentBuf = device->create_buffer({.size = 1024, .usage = truffle::rhi::BufferUsage::storage}).value();
    auto globalBuf = device->create_buffer({.size = 1024, .usage = truffle::rhi::BufferUsage::storage}).value();
    
    auto cmdCompute = device->create_command_buffer();
    TRUFFLE_CHECK(cmdCompute->begin().ok());
    TRUFFLE_CHECK(cmdCompute->bind_compute_pipeline(*computePipeline).ok());
    TRUFFLE_CHECK(cmdCompute->bind_storage_buffer(0, *localBuf, 0).ok());
    TRUFFLE_CHECK(cmdCompute->bind_storage_buffer(1, *parentBuf, 0).ok());
    TRUFFLE_CHECK(cmdCompute->bind_storage_buffer(2, *globalBuf, 0).ok());
    TRUFFLE_CHECK(cmdCompute->dispatch_compute(1, 1, 1).ok());
    TRUFFLE_CHECK(cmdCompute->end().ok());
    
    TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::compute).submit(*cmdCompute).ok());

    // --- Negative Reflection Validation: compute bindings mismatch ---
    truffle::render::TransformComputePass transformPass(*device, computeShader.get());
    auto cmdComputeMismatch = device->create_command_buffer();
    TRUFFLE_CHECK(cmdComputeMismatch->begin().ok());

    truffle::render::TransformComputePassDesc mismatchDesc{
        .localTransformBuffer = localBuf.get(),
        .nodeCount = 1,
    };
    auto mismatchStatus = transformPass.dispatch(*cmdComputeMismatch, mismatchDesc);
    TRUFFLE_CHECK(!mismatchStatus.ok());
    TRUFFLE_CHECK(mismatchStatus.code == truffle::core::StatusCode::invalid_argument);

    // --- Negative Reflection Validation: render bindings mismatch ---
    auto vertWithBufferResult = device->create_shader({
        .stage      = truffle::rhi::ShaderStage::vertex,
        .entryPoint = "vert_with_buffer",
        .bytecode   = to_bytes(kVertexWithBufferMSL),
    });
    TRUFFLE_CHECK(vertWithBufferResult.ok());
    auto vertWithBufferShader = std::move(vertWithBufferResult).value();

    truffle::render::PipelineCache pipelineCache(*device);
    pipelineCache.register_shaders(77, {
        .vertexShader = vertWithBufferShader.get(),
        .fragmentShader = fragShader.get(),
    });

    truffle::render::RenderBatch badBatch;
    badBatch.material = 77;
    badBatch.vertexCount = 3;
    badBatch.instanceCount = 1;

    truffle::render::FrameGraph badGraph;
    badGraph.add_node(std::make_unique<truffle::render::RenderPassNode>(
        true, std::vector<truffle::render::RenderBatch>{badBatch}));

    truffle::render::Renderer renderer(*device, &pipelineCache);
    auto badRenderStatus = renderer.render(badGraph, swapchain.get());
    TRUFFLE_CHECK(!badRenderStatus.ok());
    TRUFFLE_CHECK(badRenderStatus.code == truffle::core::StatusCode::invalid_argument);

    return 0;
}

