#include "truffle/renderer/view.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace truffle::renderer {
namespace {

base::Status invalid() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Renderer view: nonfinite or degenerate camera parameters");
}

bool finite(Vector3 vector) {
    return std::all_of(vector.begin(), vector.end(),
                       [](float value) { return std::isfinite(value); });
}

float dot(Vector3 a, Vector3 b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector3 cross(Vector3 a, Vector3 b) {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

base::Result<Vector3> normalized(Vector3 vector) {
    const auto length = std::hypot(vector[0], vector[1], vector[2]);
    if (!finite(vector) || !std::isfinite(length) || length < 1e-6F) {
        return invalid();
    }
    for (auto& value : vector) {
        value /= length;
    }
    return vector;
}

base::Result<Matrix4> checked(Matrix4 matrix) {
    if (!std::all_of(matrix.begin(), matrix.end(),
                     [](float value) { return std::isfinite(value); })) {
        return invalid();
    }
    return matrix;
}

} // namespace

Matrix4 multiply(const Matrix4& lhs, const Matrix4& rhs) noexcept {
    Matrix4 result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result[column * 4 + row] += lhs[inner * 4 + row] * rhs[column * 4 + inner];
            }
        }
    }
    return result;
}

base::Result<Matrix4> perspective(
    float fov, float aspect, float nearPlane, float farPlane) {
    if (!std::isfinite(fov) || !std::isfinite(aspect) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        fov <= 0 || fov >= std::numbers::pi_v<float> || aspect <= 0 ||
        nearPlane <= 0 || farPlane <= nearPlane) {
        return invalid();
    }
    const auto scale = 1.0F / std::tan(fov * 0.5F);
    Matrix4 result{};
    result[0] = scale / aspect;
    result[5] = scale;
    result[10] = farPlane / (nearPlane - farPlane);
    result[11] = -1;
    result[14] = nearPlane * result[10];
    return checked(result);
}

base::Result<Matrix4> orthographic(
    float width, float height, float nearPlane, float farPlane) {
    if (!std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        width <= 0 || height <= 0 || nearPlane < 0 || farPlane <= nearPlane) {
        return invalid();
    }
    auto result = identity_matrix;
    result[0] = 2.0F / width;
    result[5] = 2.0F / height;
    result[10] = 1.0F / (nearPlane - farPlane);
    result[14] = nearPlane * result[10];
    return checked(result);
}

base::Result<Matrix4> look_at(Vector3 eye, Vector3 target, Vector3 up) {
    if (!finite(eye) || !finite(target) || !finite(up)) {
        return invalid();
    }
    auto forward = normalized({target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]});
    auto upUnit = normalized(up);
    if (!forward.ok() || !upUnit.ok()) {
        return invalid();
    }
    auto right = normalized(cross(forward.value(), upUnit.value()));
    if (!right.ok()) {
        return right.status();
    }
    const auto vertical = cross(right.value(), forward.value());
    const auto f = forward.value();
    const auto r = right.value();
    return checked({
        r[0], vertical[0], -f[0], 0,
        r[1], vertical[1], -f[1], 0,
        r[2], vertical[2], -f[2], 0,
        -dot(r, eye), -dot(vertical, eye), dot(f, eye), 1,
    });
}

} // namespace truffle::renderer
