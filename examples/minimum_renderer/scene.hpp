#pragma once

#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/renderer/source_program.hpp"
#include "truffle/renderer/view.hpp"

#include <optional>
#include <stdexcept>

namespace minimum_renderer {

inline void require(truffle::base::Status status) {
    if (!status.ok()) {
        throw std::runtime_error(status.message);
    }
}

template <typename T>
T require(truffle::base::Result<T> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

// These types belong to this application, not to the Renderer library.
struct Vertex {
    std::array<float, 3> coordinate;
    std::array<float, 3> surfaceDirection;
};

struct alignas(16) Object {
    std::array<float, 4> centerScale{0, 0, 0, 1};
    std::array<float, 4> orientation{0, 0, 0, 1};
    std::array<float, 4> albedo{1, 1, 1, 1};
};

struct alignas(16) View {
    truffle::renderer::Matrix4 viewProjection = truffle::renderer::identity_matrix;
    std::array<float, 4> lightDirectionIntensity{0, 0, -1, 1};
};

// A deliberately serialized example, shared with the offscreen native proof.
// The completion fence makes subsequent CPU writes safe, not merely fast enough.
class Scene {
public:
    Scene(truffle::rhi::Device& device, truffle::rhi::TextureFormat colorFormat);
    void set_mesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void set_cube();
    void set_objects(std::span<const Object> objects);
    void set_view(const View& view);
    [[nodiscard]] truffle::renderer::RenderBatch batch();
    void render(const truffle::renderer::FrameGraph& graph,
                truffle::rhi::Swapchain* swapchain = nullptr);
    [[nodiscard]] const truffle::renderer::RendererFrameStats& stats() const;

private:
    truffle::rhi::Device* device_;
    truffle::renderer::PipelineCache programs_;
    truffle::renderer::Renderer renderer_;
    truffle::shader::Compiler compiler_;
    truffle::rhi::BindGroupLayout bindings_;
    truffle::rhi::PipelineLayout layout_;
    truffle::rhi::DescriptorArena arena_;
    truffle::rhi::Buffer view_;
    truffle::rhi::BindGroup group_;
    truffle::rhi::Buffer objects_;
    std::uint32_t objectCount_ = 0;
    std::optional<truffle::renderer::Mesh> mesh_;
};

} // namespace minimum_renderer
