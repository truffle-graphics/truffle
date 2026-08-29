#include "test_support.hpp"
#include "rhi_test_utils.hpp"

#include "truffle/render/frame_graph.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <vector>

namespace {

std::unique_ptr<truffle::render::RenderPassNode> make_empty_render_node() {
    truffle::rhi::RenderPassDesc passDesc;
    passDesc.extent = {1, 1};
    return std::make_unique<truffle::render::RenderPassNode>(
        passDesc, std::vector<truffle::render::RenderBatch>{});
}

} // namespace

int main() {
    auto context = truffle::tests::make_null_context();
    truffle::render::Renderer renderer{context.device};

    {
        truffle::render::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());
        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::render::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 7,
            .access = truffle::render::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 7,
            .access = truffle::render::ResourceAccess::Read,
        }).ok());

        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::render::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());

        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 9,
            .access = truffle::render::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 9,
            .access = truffle::render::ResourceAccess::Read,
        }).ok());

        // Explicit order should win over implicit hazard ordering.
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());
        TRUFFLE_CHECK(renderer.render(graph).ok());
    }

    {
        truffle::render::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto badStatus = graph.add_dependency(first, 42);
        TRUFFLE_CHECK(!badStatus.ok());
        TRUFFLE_CHECK(badStatus.code == truffle::core::StatusCode::invalid_argument);
        const auto badUsageStatus = graph.add_resource_usage(42, {
            .resourceId = 1,
            .access = truffle::render::ResourceAccess::Read,
        });
        TRUFFLE_CHECK(!badUsageStatus.ok());
        TRUFFLE_CHECK(badUsageStatus.code == truffle::core::StatusCode::invalid_argument);
    }

    {
        truffle::render::FrameGraph graph;
        const auto first = graph.add_node(make_empty_render_node());
        const auto second = graph.add_node(make_empty_render_node());
        TRUFFLE_CHECK(graph.add_resource_usage(first, {
            .resourceId = 11,
            .access = truffle::render::ResourceAccess::Write,
        }).ok());
        TRUFFLE_CHECK(graph.add_resource_usage(second, {
            .resourceId = 11,
            .access = truffle::render::ResourceAccess::Read,
        }).ok());
        TRUFFLE_CHECK(graph.add_dependency(first, second).ok());
        TRUFFLE_CHECK(graph.add_dependency(second, first).ok());

        const auto result = renderer.render(graph);
        TRUFFLE_CHECK(!result.ok());
        TRUFFLE_CHECK(result.code == truffle::core::StatusCode::invalid_argument);
    }

    return 0;
}
