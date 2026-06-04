#include "test_support.hpp"
#include "truffle/diagnostics/diagnostics.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestBuffer final : public truffle::rhi::IBuffer {
public:
    explicit TestBuffer(truffle::rhi::BufferDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] const truffle::rhi::BufferDesc& desc() const noexcept override {
        return desc_;
    }

private:
    truffle::rhi::BufferDesc desc_;
};

} // namespace

int main() {
    using namespace truffle;

    TestBuffer instanceBuffer{{
        .size = 32'000'000,
        .usage = rhi::BufferUsage::vertex,
        .debugName = "dense_detection_instances",
    }};

    render::RenderBatch batch;
    batch.instanceCount = 1'000'000;
    batch.vertexCount = 1;
    batch.material = 42;
    batch.layout.bindingModel = render::BindingModel::Interleaved;
    batch.layout.channels.push_back({
        .kind = render::ChannelKind::Transform,
        .binding = 0,
        .offset = 0,
        .stride = 80,
    });
    batch.layout.channels.push_back({
        .kind = render::ChannelKind::Color,
        .binding = 0,
        .offset = 64,
        .stride = 80,
    });
    batch.bindings[0] = {
        .buffer = &instanceBuffer,
        .offset = 0,
        .size = 32'000'000,
    };

    const auto batchSummary = diagnostics::summarize_render_batch(batch, {
        .name = "dense-detection-batch",
    });
    TRUFFLE_CHECK(batchSummary.name == "dense-detection-batch");
    TRUFFLE_CHECK(batchSummary.instanceCount == 1'000'000);
    TRUFFLE_CHECK(batchSummary.vertexCount == 1);
    TRUFFLE_CHECK(batchSummary.material == 42);
    TRUFFLE_CHECK(batchSummary.channels.size() == 2);
    TRUFFLE_CHECK(batchSummary.bindings.size() == 1);
    TRUFFLE_CHECK(batchSummary.totalBindingBytes == 32'000'000);

    const auto batchReport =
        diagnostics::format_render_batch_summary(batchSummary);
    TRUFFLE_CHECK(batchReport.find("name=dense-detection-batch") !=
                  std::string::npos);
    TRUFFLE_CHECK(batchReport.find("instances=1000000") != std::string::npos);
    TRUFFLE_CHECK(batchReport.find("bindingBytes=32000000") !=
                  std::string::npos);

    rhi::RenderPassDesc passDesc;
    passDesc.extent = {640, 480};

    render::FrameGraph graph;
    const auto nodeId = graph.add_node(std::make_unique<render::RenderPassNode>(
        passDesc, std::vector<render::RenderBatch>{batch}));
    TRUFFLE_CHECK(nodeId == 0);

    diagnostics::FrameGraphInspectionOptions options;
    options.nodeLabels.push_back({
        .id = nodeId,
        .name = "dense-detection-render",
    });
    options.resourceLabels.push_back({
        .resourceId = 99,
        .name = "dense-detection-buffer",
    });

    auto graphSummaryResult = diagnostics::summarize_frame_graph(graph, options);
    TRUFFLE_CHECK(graphSummaryResult.ok());
    const auto& graphSummary = graphSummaryResult.value();
    TRUFFLE_CHECK(graphSummary.nodes.size() == 1);
    TRUFFLE_CHECK(graphSummary.nodes.front().name == "dense-detection-render");
    TRUFFLE_CHECK(graphSummary.resourceLabels.size() == 1);
    TRUFFLE_CHECK(graphSummary.executionOrder.size() == 1);
    TRUFFLE_CHECK(graphSummary.renderNodeCount == 1);
    TRUFFLE_CHECK(graphSummary.renderBatchCount == 1);
    TRUFFLE_CHECK(graphSummary.totalInstanceCount == 1'000'000);

    const auto graphReport =
        diagnostics::format_frame_graph_summary(graphSummary);
    TRUFFLE_CHECK(graphReport.find("name=dense-detection-render") !=
                  std::string::npos);
    TRUFFLE_CHECK(graphReport.find("name=dense-detection-buffer") !=
                  std::string::npos);
    TRUFFLE_CHECK(graphReport.find("instances=1000000") != std::string::npos);

    const auto statsSummary = diagnostics::summarize_renderer_stats({
        .computeNodesExecuted = 1,
        .renderNodesExecuted = 2,
        .renderBatchesExecuted = 3,
        .presented = true,
    });
    TRUFFLE_CHECK(statsSummary.computeNodesExecuted == 1);
    TRUFFLE_CHECK(statsSummary.presented);
    const auto statsReport =
        diagnostics::format_renderer_stats_summary(statsSummary);
    TRUFFLE_CHECK(statsReport.find("presented=true") != std::string::npos);

    return 0;
}
