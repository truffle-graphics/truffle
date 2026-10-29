#include "test_support.hpp"
#include "rhi_test_utils.hpp"

#include "truffle/renderer/frame_graph.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <vector>

namespace {

std::unique_ptr<truffle::renderer::RenderPassNode> make_empty_render_node() {
    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = {1, 1};
    return std::make_unique<truffle::renderer::RenderPassNode>(
        passDesc, std::vector<truffle::renderer::RenderBatch>{});
}

} // namespace

int main() {
    auto context = truffle::tests::make_null_context();
    truffle::renderer::Renderer renderer{context.device};

    {
        truffle::renderer::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());
        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 7,
            .access = truffle::renderer::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 7,
            .access = truffle::renderer::ResourceAccess::Read,
        }).ok());

        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 9,
            .access = truffle::renderer::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 9,
            .access = truffle::renderer::ResourceAccess::Read,
        }).ok());

        // Explicit order should win over implicit hazard ordering.
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());
        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto badStatus = graph.add_dependency(first, 42);
        TRUFFLE_CHECK(!badStatus.ok());
        TRUFFLE_CHECK(badStatus.code == truffle::base::StatusCode::invalid_argument);
        const auto badUsageStatus = graph.add_resource_usage(42, {
            .resourceId = 1,
            .access = truffle::renderer::ResourceAccess::Read,
        });
        TRUFFLE_CHECK(!badUsageStatus.ok());
        TRUFFLE_CHECK(badUsageStatus.code == truffle::base::StatusCode::invalid_argument);
    }

    {
        truffle::renderer::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 11,
            .access = truffle::renderer::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 11,
            .access = truffle::renderer::ResourceAccess::Read,
        }).ok());
        TRUFFLE_CHECK(graph.add_dependency(first, second).ok());
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());

        const auto result = renderer.render(graph);
        TRUFFLE_CHECK(!result.ok());
        TRUFFLE_CHECK(result.code == truffle::base::StatusCode::invalid_argument);
    }

    return 0;
}
