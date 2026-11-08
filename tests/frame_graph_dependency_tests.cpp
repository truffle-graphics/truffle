#include "test_support.hpp"
#include "rhi_test_utils.hpp"

#include "truffle/renderer/specialist/frame_graph.hpp"
#include "truffle/renderer/specialist/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <vector>

namespace {

std::unique_ptr<truffle::renderer::specialist::RenderPassNode> make_empty_render_node() {
    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = {1, 1};
    return std::make_unique<truffle::renderer::specialist::RenderPassNode>(
        passDesc, std::vector<truffle::renderer::specialist::RenderBatch>{});
}

} // namespace

int main() {
    auto context = truffle::tests::make_null_context();
    truffle::renderer::specialist::Renderer renderer{context.device};

    {
        truffle::renderer::specialist::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TFL_CHECK(graph.add_dependency(second, first).ok());
        TFL_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::specialist::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TFL_CHECK(graph.add_resource_usage(first, {
            .resourceId = 7,
            .access = truffle::renderer::specialist::ResourceAccess::Write,
        }).ok());
        TFL_CHECK(graph.add_resource_usage(second, {
            .resourceId = 7,
            .access = truffle::renderer::specialist::ResourceAccess::Read,
        }).ok());

        TFL_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::specialist::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TFL_CHECK(graph.add_resource_usage(first, {
            .resourceId = 9,
            .access = truffle::renderer::specialist::ResourceAccess::Write,
        }).ok());
        TFL_CHECK(graph.add_resource_usage(second, {
            .resourceId = 9,
            .access = truffle::renderer::specialist::ResourceAccess::Read,
        }).ok());

        // Explicit order should win over implicit hazard ordering.
        TFL_CHECK(graph.add_dependency(second, first).ok());
        TFL_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::renderer::specialist::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto badStatus = graph.add_dependency(first, 42);
        TFL_CHECK(!badStatus.ok());
        TFL_CHECK(badStatus.code == truffle::base::StatusCode::invalid_argument);
        const auto badUsageStatus = graph.add_resource_usage(42, {
            .resourceId = 1,
            .access = truffle::renderer::specialist::ResourceAccess::Read,
        });
        TFL_CHECK(!badUsageStatus.ok());
        TFL_CHECK(badUsageStatus.code == truffle::base::StatusCode::invalid_argument);
    }

    {
        truffle::renderer::specialist::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TFL_CHECK(graph.add_resource_usage(first, {
            .resourceId = 11,
            .access = truffle::renderer::specialist::ResourceAccess::Write,
        }).ok());
        TFL_CHECK(graph.add_resource_usage(second, {
            .resourceId = 11,
            .access = truffle::renderer::specialist::ResourceAccess::Read,
        }).ok());
        TFL_CHECK(graph.add_dependency(first, second).ok());
        TFL_CHECK(graph.add_dependency(second, first).ok());

        const auto result = renderer.render(graph);
        TFL_CHECK(!result.ok());
        TFL_CHECK(result.code == truffle::base::StatusCode::invalid_argument);
    }

    return 0;
}
