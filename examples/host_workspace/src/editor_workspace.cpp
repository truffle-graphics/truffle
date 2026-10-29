#include "workspace.hpp"

#include "truffle/renderer/components.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace truffle::examples::host_workspace {
namespace {

using encom::Entity;
using encom::World;
using renderer::Camera;
using renderer::Light;
using renderer::MeshRenderer;
using renderer::Transform;

struct PreviewDrift {
    float unitsPerSecond = 0.0F;
};

struct SelectionPulse {
    float verticesPerSecond = 0.0F;
};

class EditorWorkspace final : public IWorkspace {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "editor";
    }

    void build_scene(World& world) const override {
        const auto levelPreview = world.create();
        world.emplace<Transform>(levelPreview);
        world.emplace<MeshRenderer>(levelPreview, MeshRenderer{110, 210, 36});

        const auto selectedActor = world.create();
        world.emplace<Transform>(selectedActor);
        world.emplace<MeshRenderer>(selectedActor, MeshRenderer{120, 220, 24});
        world.emplace<PreviewDrift>(selectedActor, PreviewDrift{1.5F});

        const auto selectionOverlay = world.create();
        world.emplace<Transform>(selectionOverlay);
        world.emplace<MeshRenderer>(selectionOverlay, MeshRenderer{130, 230, 12});
        world.emplace<SelectionPulse>(selectionOverlay, SelectionPulse{90.0F});

        const auto viewportCamera = world.create();
        world.emplace<Transform>(viewportCamera);
        world.emplace<Camera>(viewportCamera);

        const auto keyLight = world.create();
        world.emplace<Transform>(keyLight);
        world.emplace<Light>(keyLight, Light{1.75F});
    }

    [[nodiscard]] std::vector<World::System> systems() const override {
        return {
            [](World& world, double deltaSeconds) {
                world.each<Transform, PreviewDrift>(
                    [deltaSeconds](Entity, Transform& transform,
                                   PreviewDrift& drift) {
                        transform.worldMatrix[12] +=
                            drift.unitsPerSecond *
                            static_cast<float>(deltaSeconds);
                    });
            },
            [](World& world, double deltaSeconds) {
                world.each<MeshRenderer, SelectionPulse>(
                    [deltaSeconds](Entity, MeshRenderer& mesh,
                                   SelectionPulse& pulse) {
                        mesh.vertexCount += static_cast<std::uint32_t>(
                            pulse.verticesPerSecond *
                            static_cast<float>(deltaSeconds));
                    });
            },
        };
    }

    [[nodiscard]] FrameShape expected_frame_shape() const noexcept override {
        return FrameShape{1, 1, 1};
    }
};

} // namespace

std::unique_ptr<IWorkspace> make_editor_workspace() {
    return std::make_unique<EditorWorkspace>();
}

} // namespace truffle::examples::host_workspace
