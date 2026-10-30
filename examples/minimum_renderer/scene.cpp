#include "scene.hpp"

#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace minimum_renderer {
namespace {

using namespace truffle;

static_assert(sizeof(Vertex) == 24);
static_assert(sizeof(Object) == 48 && offsetof(Object, orientation) == 16 &&
              offsetof(Object, albedo) == 32);
static_assert(sizeof(View) == 80 && offsetof(View, lightDirectionIntensity) == 64);

shader::CompileRequest request(rhi::ShaderStage stage) {
    shader::CompileRequest result;
    result.name = "minimum Renderer application shader";
    result.source = require(shader::read_source(
        std::string(TRUFFLE_MINIMUM_RENDERER_SHADER_DIR) + "/draw.metal"));
    result.language = rhi::ShaderSourceLanguage::msl;
    result.target = rhi::ShaderTarget::metal_source;
    result.allowExperimentalMetalGeneration = true;
    result.stage = stage;
    result.entryPoint = stage == rhi::ShaderStage::vertex ? "vertex_main" : "fragment_main";
    result.sourceUnits = {"types.metal", "transform.metal", "lighting.metal"};
    result.resolver = [](std::string_view name, std::string_view, bool) -> rhi::Result<shader::Source> {
        if (name != "types.metal" && name != "transform.metal" && name != "lighting.metal") {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "minimum Renderer: unexpected shader unit");
        }
        return shader::read_source(std::string(TRUFFLE_MINIMUM_RENDERER_SHADER_DIR) +
                                    "/" + std::string(name));
    };
    rhi::ShaderReflection reflection;
    const auto add = [&](rhi::ShaderInterfaceDirection direction, std::uint32_t location,
                          const char* name, const char* type) {
        reflection.interfaces.push_back({stage, direction, location, name, type, "smooth"});
    };
    if (stage == rhi::ShaderStage::vertex) {
        add(rhi::ShaderInterfaceDirection::input, 0, "coordinate", "vec3");
        add(rhi::ShaderInterfaceDirection::input, 1, "surfaceDirection", "vec3");
        add(rhi::ShaderInterfaceDirection::input, 2, "centerScale", "vec4");
        add(rhi::ShaderInterfaceDirection::input, 3, "orientation", "vec4");
        add(rhi::ShaderInterfaceDirection::input, 4, "albedo", "vec4");
        add(rhi::ShaderInterfaceDirection::output, 0, "color", "vec4");
        reflection.bindings = {{
            .name = "parameters", .stage = stage, .group = 0, .binding = 0,
            .minimumSize = sizeof(View),
        }};
    } else {
        add(rhi::ShaderInterfaceDirection::input, 0, "color", "vec4");
        add(rhi::ShaderInterfaceDirection::output, 0, "color", "vec4");
    }
    result.interfaceDeclaration = std::move(reflection);
    return result;
}

} // namespace

Scene::Scene(rhi::Device& device, rhi::TextureFormat colorFormat)
    : device_(&device), programs_(device), renderer_(device, &programs_) {
    bindings_ = require(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::vertex,
                     .minimumBufferSize = sizeof(View)}},
    }));
    layout_ = require(device.create_pipeline_layout({.bindGroupLayouts = {&bindings_}}));
    arena_ = require(device.create_descriptor_arena());
    view_ = require(device.create_buffer({
        .size = sizeof(View), .usage = rhi::BufferUsage::uniform,
        .memory = rhi::MemoryDomain::upload,
    }));
    group_ = require(device.create_bind_group({
        .layout = &bindings_, .arena = &arena_,
        .entries = {{.binding = 0, .buffer = &view_, .size = sizeof(View)}},
    }));
    rhi::PipelineDesc state;
    state.layout = &layout_;
    state.vertexBuffers = {
        {.stride = sizeof(Vertex),
         .attributes = {{0, rhi::VertexFormat::float32x3, offsetof(Vertex, coordinate)},
                        {1, rhi::VertexFormat::float32x3, offsetof(Vertex, surfaceDirection)}}},
        {.stride = sizeof(Object), .stepMode = rhi::VertexStepMode::instance,
         .attributes = {{2, rhi::VertexFormat::float32x4, offsetof(Object, centerScale)},
                        {3, rhi::VertexFormat::float32x4, offsetof(Object, orientation)},
                        {4, rhi::VertexFormat::float32x4, offsetof(Object, albedo)}}},
    };
    state.colorTargets = {{.format = colorFormat}};
    state.depthStencil = {.format = rhi::TextureFormat::depth32_float,
                           .depthWriteEnabled = true, .depthCompare = rhi::CompareOp::less};
    renderer::SourceProgram program(programs_, 1, state);
    require(program.rebuild(compiler_, {
        .vertex = request(rhi::ShaderStage::vertex),
        .fragment = request(rhi::ShaderStage::fragment),
    }));
    set_view({});
}

void Scene::set_mesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("minimum Renderer: too many vertices");
    }
    const std::array streams{
        renderer::MeshStream{std::as_bytes(vertices), sizeof(Vertex)},
    };
    mesh_ = require(renderer::Mesh::upload(*device_, {
        .streams = streams, .vertexCount = static_cast<std::uint32_t>(vertices.size()),
        .indices = std::as_bytes(indices),
    }));
}

void Scene::set_cube() {
    struct Face { renderer::Vector3 normal, u, v; };
    const std::array faces{
        Face{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        Face{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
        Face{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        Face{{0, -1, 0}, {0, 0, -1}, {1, 0, 0}},
        Face{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        Face{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const auto& face : faces) {
        const auto first = static_cast<std::uint32_t>(vertices.size());
        for (const auto corner : {std::array{-1.0F, -1.0F}, std::array{1.0F, -1.0F},
                                   std::array{-1.0F, 1.0F}, std::array{1.0F, 1.0F}}) {
            Vertex vertex{.surfaceDirection = face.normal};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                vertex.coordinate[axis] =
                    0.5F * (face.normal[axis] + face.u[axis] * corner[0] +
                            face.v[axis] * corner[1]);
            }
            vertices.push_back(vertex);
        }
        for (const auto offset : {0u, 1u, 2u, 2u, 1u, 3u}) {
            indices.push_back(first + offset);
        }
    }
    set_mesh(vertices, indices);
}

void Scene::set_objects(std::span<const Object> objects) {
    if (objects.empty() || objects.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("minimum Renderer: invalid object count");
    }
    auto next = require(device_->create_buffer({
        .size = objects.size_bytes(), .usage = rhi::BufferUsage::vertex,
        .memory = rhi::MemoryDomain::upload,
    }));
    require(next.write(0, std::as_bytes(objects)));
    objects_ = std::move(next);
    objectCount_ = static_cast<std::uint32_t>(objects.size());
}

void Scene::set_view(const View& view) {
    require(view_.write(0, std::as_bytes(std::span{&view, 1})));
}

renderer::RenderBatch Scene::batch() {
    if (!mesh_ || !objects_.valid()) {
        throw std::runtime_error("minimum Renderer: mesh and objects must be supplied");
    }
    auto result = mesh_->batch(1);
    result.instanceCount = objectCount_;
    result.bindings[1] = {&objects_, 0, objects_.desc().size};
    result.bindGroups = {{.group = 0, .bindings = &group_}};
    return result;
}

base::Status Scene::render(const renderer::FrameGraph& graph, rhi::Swapchain* swapchain) {
    // Even a recoverable present failure can follow a successful submission.
    const auto rendered = renderer_.render(graph, swapchain);
    auto poolResult = device_->create_command_pool(rhi::QueueKind::graphics);
    if (!poolResult.ok()) {
        return poolResult.status();
    }
    auto pool = std::move(poolResult).value();
    auto markerResult = pool.allocate();
    if (!markerResult.ok()) {
        return markerResult.status();
    }
    auto marker = std::move(markerResult).value();
    if (auto status = marker.begin(); !status.ok()) {
        return status;
    }
    if (auto status = marker.end(); !status.ok()) {
        return status;
    }
    auto fence = device_->create_fence();
    if (!fence.ok()) {
        return fence.status();
    }
    auto queue = device_->queue(rhi::QueueKind::graphics);
    if (!queue.ok()) {
        return queue.status();
    }
    const std::array<rhi::CommandList*, 1> lists{&marker};
    if (auto status = queue.value().submit({
            .commandLists = lists, .signalFence = &fence.value(), .signalFenceValue = 1});
        !status.ok()) {
        return status;
    }
    if (auto status = fence.value().wait(1, std::chrono::seconds(10)); !status.ok()) {
        return status;
    }
    return rendered;
}

const renderer::RendererFrameStats& Scene::stats() const {
    return renderer_.last_frame_stats();
}

} // namespace minimum_renderer
