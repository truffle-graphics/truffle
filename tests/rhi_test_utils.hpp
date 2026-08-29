#pragma once

#include "truffle/rhi/null_backend.hpp"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace truffle::tests {

struct NullContext {
    rhi::Instance instance;
    rhi::Adapter adapter;
    rhi::Device device;
};

[[nodiscard]] inline NullContext make_null_context() {
    auto instanceResult = rhi::create_null_instance();
    assert(instanceResult.ok());
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    return {
        .instance = std::move(instance),
        .adapter = std::move(adapter),
        .device = std::move(deviceResult).value(),
    };
}

[[nodiscard]] inline rhi::Shader make_shader(
    rhi::Device& device, rhi::ShaderStage stage,
    std::vector<rhi::ResourceBinding> reflection = {}) {
    auto result = device.create_shader({
        .stage = stage,
        .code = {std::byte{0x54}, std::byte{0x52}, std::byte{0x46}},
        .reflection = std::move(reflection),
    });
    assert(result.ok());
    return std::move(result).value();
}

[[nodiscard]] inline rhi::Buffer make_buffer(
    rhi::Device& device, std::size_t size,
    rhi::BufferUsage usage = rhi::BufferUsage::storage,
    rhi::MemoryDomain memory = rhi::MemoryDomain::upload) {
    auto result = device.create_buffer({
        .size = size,
        .usage = usage,
        .memory = memory,
    });
    assert(result.ok());
    return std::move(result).value();
}

} // namespace truffle::tests
