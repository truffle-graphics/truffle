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

struct InspectionOrbit {
    float radiansPerSecond = 0.0F;
};

struct SectionDetail {
    float verticesPerSecond = 0.0F;
};

class CadWorkspace final : public IWorkspace {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "cad";
    }

    void build_scene(World& world) const override {
        const auto machinedBody = world.create();
        world.emplace<Transform>(machinedBody);
        world.emplace<MeshRenderer>(machinedBody, MeshRenderer{310, 410, 48});
        world.emplace<InspectionOrbit>(machinedBody, InspectionOrbit{0.5F});

        const auto fixture = world.create();
        world.emplace<Transform>(fixture);
        world.emplace<MeshRenderer>(fixture, MeshRenderer{320, 420, 30});

        const auto sectionCut = world.create();
        world.emplace<Transform>(sectionCut);
        world.emplace<MeshRenderer>(sectionCut, MeshRenderer{330, 430, 18});
        world.emplace<SectionDetail>(sectionCut, SectionDetail{45.0F});

        const auto inspectionCamera = world.create();
        world.emplace<Transform>(inspectionCamera);
        world.emplace<Camera>(inspectionCamera,
                              Camera{.verticalFieldOfViewRadians = 0.7854F});

        const auto keyLight = world.create();
        world.emplace<Transform>(keyLight);
        world.emplace<Light>(keyLight, Light{2.25F});

        const auto fillLight = world.create();
        world.emplace<Transform>(fillLight);
        world.emplace<Light>(fillLight, Light{0.75F});
    }

    [[nodiscard]] std::vector<World::System> systems() const override {
        return {
            [](World& world, double deltaSeconds) {
                world.each<Transform, InspectionOrbit>(
                    [deltaSeconds](Entity, Transform& transform,
                                   InspectionOrbit& orbit) {
                        transform.worldMatrix[0] +=
                            orbit.radiansPerSecond *
                            static_cast<float>(deltaSeconds);
                    });
            },
            [](World& world, double deltaSeconds) {
                world.each<MeshRenderer, SectionDetail>(
                    [deltaSeconds](Entity, MeshRenderer& mesh,
                                   SectionDetail& detail) {
                        mesh.vertexCount += static_cast<std::uint32_t>(
                            detail.verticesPerSecond *
                            static_cast<float>(deltaSeconds));
                    });
            },
        };
    }

    [[nodiscard]] FrameShape expected_frame_shape() const noexcept override {
        return FrameShape{1, 2, 1};
    }
};

} // namespace

std::unique_ptr<IWorkspace> make_cad_workspace() {
    return std::make_unique<CadWorkspace>();
}

} // namespace truffle::examples::host_workspace
