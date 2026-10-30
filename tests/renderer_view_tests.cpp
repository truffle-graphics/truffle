#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/renderer/view.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace {

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 1e-5F;
}

float projected_depth(const truffle::renderer::Matrix4& matrix, float z) {
    return (matrix[10] * z + matrix[14]) / (matrix[11] * z + matrix[15]);
}

} // namespace

int main() {
    using namespace truffle;
    constexpr float pi = std::numbers::pi_v<float>;
    const auto projection = renderer::perspective(pi / 2, 2, 1, 10);
    TRUFFLE_CHECK(projection.ok());
    TRUFFLE_CHECK(near(projection.value()[0], 0.5F));
    TRUFFLE_CHECK(near(projection.value()[5], 1));
    TRUFFLE_CHECK(near(projected_depth(projection.value(), -1), 0));
    TRUFFLE_CHECK(near(projected_depth(projection.value(), -10), 1));
    TRUFFLE_CHECK(renderer::multiply(renderer::identity_matrix, projection.value()) ==
                   projection.value());
    auto ortho = renderer::orthographic(4, 2, 0, 10);
    TRUFFLE_CHECK(ortho.ok() && near(ortho.value()[0], .5F));
    TRUFFLE_CHECK(near(projected_depth(ortho.value(), 0), 0));
    TRUFFLE_CHECK(near(projected_depth(ortho.value(), -10), 1));
    auto view = renderer::look_at({0, 0, 5}, {0, 0, 0});
    TRUFFLE_CHECK(view.ok() && near(view.value()[14], -5));
    auto combined = renderer::multiply(projection.value(), view.value());
    TRUFFLE_CHECK(near(projected_depth(combined, 4), 0));
    TRUFFLE_CHECK(near(projected_depth(combined, -5), 1));
    TRUFFLE_CHECK(renderer::look_at({0, 0, 0}, {0, 0, -1}).value() ==
                   renderer::identity_matrix);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    TRUFFLE_CHECK(!renderer::perspective(0, 1, 1, 10).ok());
    TRUFFLE_CHECK(!renderer::perspective(pi, 1, 1, 10).ok());
    TRUFFLE_CHECK(!renderer::perspective(1, 0, 1, 10).ok());
    TRUFFLE_CHECK(!renderer::perspective(1, 1, 0, 10).ok());
    TRUFFLE_CHECK(!renderer::perspective(1, 1, 10, 1).ok());
    TRUFFLE_CHECK(!renderer::perspective(nan, 1, 1, 10).ok());
    TRUFFLE_CHECK(!renderer::perspective(1, inf, 1, 10).ok());
    TRUFFLE_CHECK(!renderer::orthographic(0, 1, 0, 10).ok());
    TRUFFLE_CHECK(!renderer::orthographic(1, -1, 0, 10).ok());
    TRUFFLE_CHECK(!renderer::orthographic(1, 1, -1, 10).ok());
    TRUFFLE_CHECK(!renderer::orthographic(1, 1, 10, 10).ok());
    TRUFFLE_CHECK(!renderer::orthographic(1, 1, 0, inf).ok());
    TRUFFLE_CHECK(!renderer::look_at({0, 0, 0}, {0, 0, 0}).ok());
    TRUFFLE_CHECK(!renderer::look_at({0, 0, 0}, {0, 1, 0}, {0, 1, 0}).ok());
    TRUFFLE_CHECK(!renderer::look_at({nan, 0, 0}, {0, 0, 0}).ok());
    auto context = tests::make_null_context();
    renderer::DepthTarget depth(context.device);
    TRUFFLE_CHECK(depth.resize({16, 16}).ok());
    const auto original = depth.texture()->id();
    TRUFFLE_CHECK(depth.resize({16, 16}).ok());
    TRUFFLE_CHECK(depth.texture()->id() == original);
    TRUFFLE_CHECK(!depth.resize({0, 16}).ok());
    TRUFFLE_CHECK(!depth.resize({16, 16}, rhi::TextureFormat::rgba8_unorm).ok());
    TRUFFLE_CHECK(!depth.resize({32, 24}, rhi::TextureFormat::depth32_float, 0).ok());
    TRUFFLE_CHECK(depth.texture()->id() == original);
    TRUFFLE_CHECK(depth.resize({32, 24}).ok());
    TRUFFLE_CHECK(depth.texture()->id() != original);
    TRUFFLE_CHECK(depth.texture()->desc().extent.width == 32);
    TRUFFLE_CHECK(depth.texture()->desc().extent.height == 24);
    TRUFFLE_CHECK(depth.resize({16, 16}, rhi::TextureFormat::depth32_float_stencil8).ok());
    renderer::Renderer renderer(context.device);
    renderer::RenderBatch batch;
    batch.instanceCount = 1;
    renderer::FrameGraph missingSwapchain;
    missingSwapchain.add_node(std::make_unique<renderer::RenderPassNode>(
        rhi::RenderPassDesc{.depthStencilAttachment = {.texture = depth.texture()}},
        std::vector{batch}, true));
    TRUFFLE_CHECK(!renderer.render(missingSwapchain).ok());
    return 0;
}
