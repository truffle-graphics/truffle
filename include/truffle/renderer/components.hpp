#pragma once

#include <array>
#include <cstdint>

namespace truffle::renderer {

struct Transform {
    std::array<float, 16> worldMatrix{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct Camera {
    float verticalFieldOfViewRadians = 1.0472F; // ~60 degrees
    float horizontalFieldOfViewRadians = 1.5708F; // ~90 degrees
    float nearPlane = 0.1F;
    float farPlane = 10000.0F;
    float aspectRatio = 1.7778F; // 16:9
};

struct MeshRenderer {
    std::uint64_t meshId     = 0;
    std::uint64_t materialId = 0;
    std::uint32_t vertexCount = 3;
    std::uint32_t indexCount  = 0; // 0 = non-indexed
};

struct Light {
    float intensity         = 1.0F;
    float range             = 100.0F;
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
};

struct PointLight {
    float intensity = 1.0F;
    float range     = 100.0F;
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
};

struct DirectionalLight {
    std::array<float, 3> direction{0.0F, -1.0F, 0.0F};
    float intensity = 1.0F;
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
};

} // namespace truffle::renderer

