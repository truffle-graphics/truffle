#include "rhi_test_utils.hpp"

#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
#include "truffle/rhi/direct3d_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_METAL_BACKEND
#include "truffle/rhi/metal_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
#include "truffle/rhi/opengles_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
#include "truffle/rhi/opengl_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
#include "truffle/rhi/vulkan_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
#include "truffle/rhi/webgl2_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
#include "truffle/rhi/webgpu_backend.hpp"
#endif

#include <array>
#include <cassert>

int main() {
    using namespace truffle;
    auto context = tests::make_null_context();
    assert(context.adapter.info().validationOnly);
    assert(!context.adapter.info().native);

    auto unsupportedFeature = context.adapter.request_device({
        .requiredFeatures = {rhi::Feature::external_memory},
    });
    assert(!unsupportedFeature.ok());
    assert(unsupportedFeature.status().code == core::StatusCode::unsupported);

    auto graphics = context.device.queue(rhi::QueueKind::graphics);
    auto compute = context.device.queue(rhi::QueueKind::compute);
    auto transfer = context.device.queue(rhi::QueueKind::transfer);
    assert(graphics.ok() && compute.ok() && transfer.ok());

#ifdef TRUFFLE_HAS_METAL_BACKEND
    auto metalResult = rhi::create_metal_instance();
    if (metalResult.ok()) {
        auto metal = std::move(metalResult).value();
        auto metalAdapter = metal.adapter(0);
        assert(metalAdapter.ok());
        assert(metalAdapter.value().info().native);
        assert(metalAdapter.value().info().backend == rhi::BackendKind::metal);
    } else {
        assert(metalResult.status().code == core::StatusCode::unavailable);
    }
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
    auto vulkanResult = rhi::create_vulkan_instance();
    if (vulkanResult.ok()) {
        auto vulkan = std::move(vulkanResult).value();
        auto adapter = vulkan.adapter(0);
        assert(adapter.ok() && adapter.value().info().native);
        assert(adapter.value().info().backend == rhi::BackendKind::vulkan);
    } else {
        assert(vulkanResult.status().code == core::StatusCode::unavailable);
    }
#endif
#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
    auto direct3dResult = rhi::create_direct3d12_instance();
    if (direct3dResult.ok()) {
        auto direct3d = std::move(direct3dResult).value();
        auto adapter = direct3d.adapter(0);
        assert(adapter.ok() && adapter.value().info().native);
        assert(adapter.value().info().backend == rhi::BackendKind::direct3d12);
    } else {
        assert(direct3dResult.status().code == core::StatusCode::unsupported);
    }
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
    auto openglResult = rhi::create_opengl_instance();
    if (openglResult.ok()) {
        auto opengl = std::move(openglResult).value();
        auto adapter = opengl.adapter(0);
        assert(adapter.ok() && adapter.value().info().native);
        assert(adapter.value().info().backend == rhi::BackendKind::opengl);
    } else {
        assert(openglResult.status().code == core::StatusCode::unsupported ||
               openglResult.status().code == core::StatusCode::unavailable);
    }
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
    auto openglesResult = rhi::create_opengles_instance();
    if (openglesResult.ok()) {
        auto opengles = std::move(openglesResult).value();
        auto adapter = opengles.adapter(0);
        assert(adapter.ok() && adapter.value().info().native);
        assert(adapter.value().info().backend == rhi::BackendKind::opengles);
    } else {
        assert(openglesResult.status().code == core::StatusCode::unsupported ||
               openglesResult.status().code == core::StatusCode::unavailable);
    }
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
    const auto webgpu = rhi::create_webgpu_instance();
    assert(!webgpu.ok() && webgpu.status().code == core::StatusCode::unsupported);
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
    const auto webgl2 = rhi::create_webgl2_instance();
    if (!webgl2.ok()) {
        assert(webgl2.status().code == core::StatusCode::unsupported ||
               webgl2.status().code == core::StatusCode::unavailable);
    }
#endif
    return 0;
}
