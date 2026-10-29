#pragma once

#include "truffle/encom/world.hpp"
#include "truffle/renderer/components.hpp"
#include "truffle/renderer/render_batch.hpp"
#include "truffle/rhi/rhi.hpp"

#include <array>
#include <vector>

namespace truffle::encom_renderer {

// ---------------------------------------------------------------------------
// EnComRenderFrame — camera/light state + mesh batches extracted from an EnCom world
// ---------------------------------------------------------------------------

struct EnComRenderFrame {
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
    std::vector<renderer::RenderBatch> meshBatches;
};

// ---------------------------------------------------------------------------
// EnComRenderAdapter — ECS → EnComRenderFrame via UploadRing
//
// Writes transform data directly into ring-allocated upload memory.
// No per-entity intermediate copy; callers own the ring's lifetime.
// ---------------------------------------------------------------------------

class EnComRenderAdapter {
public:
    [[nodiscard]] EnComRenderFrame extract(encom::World& world,
                                     rhi::UploadRing& ring) const;
};

} // namespace truffle::encom_renderer
