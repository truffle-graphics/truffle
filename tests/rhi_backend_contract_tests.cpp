#include "rhi_test_utils.hpp"

#include "truffle/rhi/direct3d_backend.hpp"
#include "truffle/rhi/opengl_backend.hpp"
#include "truffle/rhi/vulkan_backend.hpp"

#ifdef TRUFFLE_HAS_METAL_BACKEND
#include "truffle/rhi/metal_backend.hpp"
#endif

#include <array>
#include <cassert>

int main() {
    using namespace truffle;
    auto context = tests::make_null_context();
    assert(context.adapter.info().validationOnly);
    assert(!context.adapter.info().native);

    auto unsupportedFeature = context.adapter.request_device({
        .requiredFeatures = {rhi::Feature::timestamp_queries},
    });
    assert(!unsupportedFeature.ok());
    assert(unsupportedFeature.status().code == core::StatusCode::unsupported);

    auto graphics = context.device.queue(rhi::QueueKind::graphics);
    auto compute = context.device.queue(rhi::QueueKind::compute);
    auto transfer = context.device.queue(rhi::QueueKind::transfer);
    assert(graphics.ok() && compute.ok() && transfer.ok());

    const auto vulkan = rhi::create_vulkan_instance();
    const auto opengl = rhi::create_opengl_instance();
    const auto direct3d = rhi::create_direct3d12_instance();
    assert(!vulkan.ok() && vulkan.status().code == core::StatusCode::unsupported);
    assert(!opengl.ok() && opengl.status().code == core::StatusCode::unsupported);
    assert(!direct3d.ok() &&
           direct3d.status().code == core::StatusCode::unsupported);

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
    return 0;
}
