#pragma once

#include "truffle/ecs/world.hpp"
#include "truffle/render/components.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/rhi/rhi.hpp"

#include <array>
#include <vector>

namespace truffle::scene {

// ---------------------------------------------------------------------------
// SceneFrame — camera/light state + mesh batches extracted from an ECS world
// ---------------------------------------------------------------------------

struct SceneFrame {
    struct CameraState {
        std::array<float, 16> worldMatrix{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        };
        float verticalFov   = 1.0472F;
        float nearPlane     = 0.1F;
        float farPlane      = 10000.0F;
        float aspectRatio   = 1.7778F;
    };

    struct LightState {
        std::array<float, 4> position{0.0F, 0.0F, 0.0F, 1.0F};
        float intensity = 1.0F;
        std::array<float, 3> color{1.0F, 1.0F, 1.0F};
    };

    std::vector<CameraState>       cameras;
    std::vector<LightState>        lights;

    // Mesh batches — pass directly to Renderer::render()
    std::vector<render::RenderBatch> meshBatches;
};

// ---------------------------------------------------------------------------
// SceneAdapter — ECS → SceneFrame via UploadRing
//
// Writes transform data directly into ring-allocated upload memory.
// No per-entity intermediate copy; callers own the ring's lifetime.
// ---------------------------------------------------------------------------

class SceneAdapter {
public:
    [[nodiscard]] SceneFrame extract(ecs::World& world,
                                     rhi::UploadRing& ring) const;
};

} // namespace truffle::scene
