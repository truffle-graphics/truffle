#pragma once

#include "truffle/base/status.hpp"

#include <array>

namespace truffle::renderer {

// Column-major, column vectors, right-handed view looking down -Z.
// Projection depth is [0,1] (Metal/Vulkan/D3D), not OpenGL's native [-1,1].
using Matrix4 = std::array<float, 16>;
using Vector3 = std::array<float, 3>;
inline constexpr Matrix4 identity_matrix{
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
};

[[nodiscard]] Matrix4 multiply(const Matrix4& lhs, const Matrix4& rhs) noexcept;
[[nodiscard]] base::Result<Matrix4> perspective(
    float verticalFovRadians, float aspect, float nearPlane, float farPlane);
[[nodiscard]] base::Result<Matrix4> orthographic(
    float width, float height, float nearPlane, float farPlane);
[[nodiscard]] base::Result<Matrix4> look_at(
    Vector3 eye, Vector3 target, Vector3 up = {0, 1, 0});

} // namespace truffle::renderer
