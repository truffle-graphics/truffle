#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/specialist/data_layout.hpp"
#include "truffle/renderer/specialist/mesh.hpp"

#include <array>
#include <limits>

int main() {
    using namespace truffle;
    auto context = tests::make_null_context();
    // Deliberately domain-specific, with no Renderer channel semantics.
    struct Sample { float east, north, altitude, confidence; };
    const std::array samples{Sample{-1, -1, 0, 1}, Sample{3, -1, 0, 1},
                             Sample{-1, 3, 0, 1}};
    const auto bytes = std::as_bytes(std::span{samples});
    const std::array streams{renderer::specialist::MeshStream{bytes, sizeof(Sample)}};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    renderer::specialist::MeshUpload desc{streams, 3, std::as_bytes(std::span{indices}),
                               rhi::IndexFormat::uint16};
    auto uploaded = renderer::specialist::Mesh::upload(context.device, desc);
    TFL_CHECK(uploaded.ok());
    auto mesh = std::move(uploaded).value();
    auto batch = mesh.batch(12, 4);
    TFL_CHECK(batch.kind == renderer::specialist::DrawKind::Indexed);
    TFL_CHECK(batch.vertexCount == 3 && batch.vertexElementCount == 3);
    TFL_CHECK(batch.material == 12 && batch.variantHash == 4);
    std::array<std::byte, sizeof(samples)> readback{};
    TFL_CHECK(batch.bindings[0].buffer->read(0, readback).ok());
    TFL_CHECK(std::equal(bytes.begin(), bytes.end(), readback.begin()));
    std::vector<rhi::VertexBufferLayoutDesc> layout{{
        .stride = sizeof(Sample), .attributes = {{.location = 7}},
    }};
    std::vector<rhi::ShaderInterfaceVariable> reflection{{
        .location = 7, .name = "arbitrary.shader.name", .type = "vec4",
    }};
    TFL_CHECK(renderer::specialist::validate_vertex_layout(layout, reflection).ok());
    TFL_CHECK(renderer::specialist::validate_batch_data(batch, layout).ok());
    auto badLayout = layout;
    badLayout[0].attributes[0].format = rhi::VertexFormat::float32x3;
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, reflection).ok());
    badLayout[0].attributes[0].format = rhi::VertexFormat::uint32x4;
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, reflection).ok());
    badLayout = layout;
    badLayout[0].attributes[0].location = 8;
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, reflection).ok());
    badLayout = layout;
    badLayout[0].attributes.push_back(badLayout[0].attributes[0]);
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, {}).ok());
    badLayout = layout;
    badLayout[0].attributes[0].offset = 4;
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, {}).ok());
    badLayout = layout;
    badLayout[0].stride = 17;
    TFL_CHECK(!renderer::specialist::validate_vertex_layout(badLayout, {}).ok());
    auto bad = batch;
    bad.bindings[0] = {};
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.bindings[0].size -= 1;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.bindings[0].offset = std::numeric_limits<std::size_t>::max();
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.indexBuffer = {};
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.indexBuffer.offset = 1;
    --bad.indexBuffer.size;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.vertexCount = 4;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad = batch;
    bad.kind = renderer::specialist::DrawKind::IndirectIndexed;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    auto args = tests::make_buffer(context.device, 20, rhi::BufferUsage::indirect);
    bad.indirectBuffer = {&args, 0, 20};
    TFL_CHECK(renderer::specialist::validate_batch_data(bad, layout).ok());
    bad.instanceCount = 0;
    bad.vertexCount = 0;
    TFL_CHECK(renderer::specialist::validate_batch_data(bad, layout).ok());
    layout[0].stepMode = rhi::VertexStepMode::instance;
    bad = batch;
    bad.instanceCount = 4;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    bad.instanceCount = 3;
    TFL_CHECK(renderer::specialist::validate_batch_data(bad, layout).ok());
    // A nonzero offset and explicit view length must be respected independently.
    auto padded = tests::make_buffer(context.device, 80, rhi::BufferUsage::vertex);
    bad.bindings[0] = {&padded, 16, 48};
    TFL_CHECK(renderer::specialist::validate_batch_data(bad, layout).ok());
    bad.bindings[0].size = 32;
    TFL_CHECK(!renderer::specialist::validate_batch_data(bad, layout).ok());
    desc.vertexCount = 4;
    TFL_CHECK(!renderer::specialist::Mesh::upload(context.device, desc).ok());
    desc.vertexCount = 3;
    const std::array<std::uint16_t, 3> badIndices{0, 1, 3};
    desc.indices = std::as_bytes(std::span{badIndices});
    TFL_CHECK(!renderer::specialist::Mesh::upload(context.device, desc).ok());
    desc.indices = bytes.first(3);
    TFL_CHECK(!renderer::specialist::Mesh::upload(context.device, desc).ok());
    desc.indices = {};
    auto direct = renderer::specialist::Mesh::upload(context.device, desc);
    TFL_CHECK(direct.ok());
    TFL_CHECK(direct.value().batch(1).kind == renderer::specialist::DrawKind::Direct);
    return 0;
}
