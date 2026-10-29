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

struct LinearVelocity {
    float xUnitsPerSecond = 0.0F;
    float yUnitsPerSecond = 0.0F;
};

struct SensorTrail {
    float verticesPerSecond = 0.0F;
};

class SimulationWorkspace final : public IWorkspace {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "simulation";
    }

    void build_scene(World& world) const override {
        const auto environment = world.create();
        world.emplace<Transform>(environment);
        world.emplace<MeshRenderer>(environment, MeshRenderer{510, 610, 60});

        const auto rover = world.create();
        world.emplace<Transform>(rover);
        world.emplace<MeshRenderer>(rover, MeshRenderer{520, 620, 24});
        world.emplace<LinearVelocity>(rover, LinearVelocity{2.0F, 0.25F});

        const auto probe = world.create();
        world.emplace<Transform>(probe);
        world.emplace<MeshRenderer>(probe, MeshRenderer{530, 630, 12});
        world.emplace<LinearVelocity>(probe, LinearVelocity{-0.5F, 1.0F});

        const auto sensorTrail = world.create();
        world.emplace<Transform>(sensorTrail);
        world.emplace<MeshRenderer>(sensorTrail, MeshRenderer{540, 640, 9});
        world.emplace<SensorTrail>(sensorTrail, SensorTrail{120.0F});

        const auto simulationCamera = world.create();
        world.emplace<Transform>(simulationCamera);
        world.emplace<Camera>(simulationCamera);

        const auto sunLight = world.create();
        world.emplace<Transform>(sunLight);
        world.emplace<Light>(sunLight, Light{1.5F});
    }

    [[nodiscard]] std::vector<World::System> systems() const override {
        return {
            [](World& world, double deltaSeconds) {
                world.each<Transform, LinearVelocity>(
                    [deltaSeconds](Entity, Transform& transform,
                                   LinearVelocity& velocity) {
                        transform.worldMatrix[12] +=
                            velocity.xUnitsPerSecond *
                            static_cast<float>(deltaSeconds);
                        transform.worldMatrix[13] +=
                            velocity.yUnitsPerSecond *
                            static_cast<float>(deltaSeconds);
                    });
            },
            [](World& world, double deltaSeconds) {
                world.each<MeshRenderer, SensorTrail>(
                    [deltaSeconds](Entity, MeshRenderer& mesh,
                                   SensorTrail& trail) {
                        mesh.vertexCount += static_cast<std::uint32_t>(
                            trail.verticesPerSecond *
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

std::unique_ptr<IWorkspace> make_simulation_workspace() {
    return std::make_unique<SimulationWorkspace>();
}

} // namespace truffle::examples::host_workspace
