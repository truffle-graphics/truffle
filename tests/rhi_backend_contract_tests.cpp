#include "test_support.hpp"

#include "truffle/core/status.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"
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
    TRUFFLE_CHECK(swapchain.value()->acquire_next_texture() != nullptr);
    TRUFFLE_CHECK(swapchain.value()->resize({64, 64}).ok());
    TRUFFLE_CHECK(swapchain.value()->desc().extent.width == 64);
    TRUFFLE_CHECK(swapchain.value()->desc().extent.height == 64);

    auto uploadRing = device.create_upload_ring(2, 128);
    TRUFFLE_CHECK(uploadRing.ok());
    TRUFFLE_CHECK(uploadRing.value()->frames_in_flight() == 2);
    TRUFFLE_CHECK(uploadRing.value()->capacity_per_frame() == 128);
    auto alloc = uploadRing.value()->allocate(32, 16);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.buffer != nullptr);
    TRUFFLE_CHECK(alloc.mappedPtr != nullptr);
    TRUFFLE_CHECK(alloc.size == 32);
    auto overflowAlloc = uploadRing.value()->allocate(1024, 16);
    TRUFFLE_CHECK(!overflowAlloc.valid());
    uploadRing.value()->advance();

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

    auto commandBuffer = device.create_command_buffer();
    TRUFFLE_CHECK(commandBuffer != nullptr);
    TRUFFLE_CHECK(commandBuffer->begin().ok());
    TRUFFLE_CHECK(commandBuffer->end().ok());
    TRUFFLE_CHECK(swapchain.value()->schedule_present(*commandBuffer).ok());

    auto submitFence = device.create_fence({.signaled = false});
    TRUFFLE_CHECK(!submitFence->signaled());
    TRUFFLE_CHECK(device.queue(truffle::rhi::QueueKind::graphics)
                      .submit(*commandBuffer, submitFence.get())
                      .ok());
    submitFence->wait();
    TRUFFLE_CHECK(submitFence->signaled());

    auto notReady = device.create_command_buffer();
    TRUFFLE_CHECK(notReady != nullptr);
    auto invalidSubmit =
        device.queue(truffle::rhi::QueueKind::graphics).submit(*notReady, nullptr);
    TRUFFLE_CHECK(!invalidSubmit.ok());
    TRUFFLE_CHECK(invalidSubmit.code == truffle::core::StatusCode::invalid_state);

    return 0;
}

int verify_common_device_contract(truffle::rhi::IDevice& device,
                                  truffle::rhi::BackendKind backendKind) {
    auto badBuffer = device.create_buffer({
        .size = 0,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(!badBuffer.ok());
    TRUFFLE_CHECK(badBuffer.status().code == truffle::core::StatusCode::invalid_argument);

    auto goodBuffer = device.create_buffer({
        .size = 64,
        .usage = truffle::rhi::BufferUsage::vertex,
    });
    TRUFFLE_CHECK(goodBuffer.ok());

    auto badTexture = device.create_texture({
        .extent = {0, 0},
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
    });
    TRUFFLE_CHECK(!badTexture.ok());
    TRUFFLE_CHECK(badTexture.status().code == truffle::core::StatusCode::invalid_argument);

    auto badSurface = device.create_surface({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {0, 0},
    });
    TRUFFLE_CHECK(!badSurface.ok());
    TRUFFLE_CHECK(badSurface.status().code == truffle::core::StatusCode::invalid_argument);

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

    auto badRing = device.create_upload_ring(0, 0);
    TRUFFLE_CHECK(!badRing.ok());
    TRUFFLE_CHECK(badRing.status().code == truffle::core::StatusCode::invalid_argument);

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
    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();
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
