#pragma once

#include "truffle/renderer/renderer.hpp"

#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace truffle::tests {

inline void require(base::Status status) {
    if (!status.ok()) {
        throw std::runtime_error(status.message);
    }
}

template <typename T>
T require(base::Result<T> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

inline void submit_graphics(rhi::Device& device, rhi::CommandList& list) {
    auto queue = require(device.queue(rhi::QueueKind::graphics));
    const std::array<rhi::CommandList*, 1> lists{&list};
    require(queue.submit(lists));
}

inline void color_barrier(rhi::CommandList& list, rhi::Texture& texture,
                          rhi::TextureLayout from, rhi::TextureLayout to) {
    rhi::BarrierBatch batch;
    batch.textures.push_back({
        .texture = &texture,
        .oldLayout = from,
        .newLayout = to,
        .sourceStages = from == rhi::TextureLayout::undefined
                            ? rhi::PipelineStage::top
                            : from == rhi::TextureLayout::color_attachment
                                  ? rhi::PipelineStage::color_attachment_output
                                  : rhi::PipelineStage::copy,
        .destinationStages = to == rhi::TextureLayout::color_attachment
                                 ? rhi::PipelineStage::color_attachment_output
                                 : rhi::PipelineStage::copy,
        .sourceAccess = from == rhi::TextureLayout::undefined
                            ? rhi::Access::none
                            : from == rhi::TextureLayout::color_attachment
                                  ? rhi::Access::color_attachment_write
                                  : rhi::Access::transfer_read,
        .destinationAccess = to == rhi::TextureLayout::color_attachment
                                 ? rhi::Access::color_attachment_write
                                 : rhi::Access::transfer_read,
    });
    require(list.barrier(batch));
}

inline rhi::Texture make_color_target(rhi::Device& device) {
    auto target = require(device.create_texture({
        .extent = {16, 16, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
    }));
    auto pool = require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = require(pool.allocate());
    require(list.begin());
    color_barrier(list, target, rhi::TextureLayout::undefined,
                  rhi::TextureLayout::color_attachment);
    require(list.end());
    submit_graphics(device, list);
    return target;
}

inline void check_color(rhi::Device& device, rhi::Texture& target,
                        std::array<std::byte, 4> expected, const char* label) {
    constexpr std::size_t rowPitch = 256;
    auto readback = require(device.create_buffer({
        .size = rowPitch * 16,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    }));
    auto pool = require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = require(pool.allocate());
    require(list.begin());
    color_barrier(list, target, rhi::TextureLayout::color_attachment,
                  rhi::TextureLayout::transfer_source);
    auto copy = require(list.begin_copy());
    require(copy.copy_texture_to_buffer(
        target, readback,
        {.layout = {.bytesPerRow = rowPitch, .rowsPerImage = 16},
         .texture = {.extent = {16, 16, 1}}}));
    require(copy.end());
    color_barrier(list, target, rhi::TextureLayout::transfer_source,
                  rhi::TextureLayout::color_attachment);
    require(list.end());
    submit_graphics(device, list);
    std::vector<std::byte> bytes(rowPitch * 16);
    require(readback.read(0, bytes));
    for (std::size_t y = 0; y < 16; ++y) {
        for (std::size_t x = 0; x < 16; ++x) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (bytes[y * rowPitch + x * 4 + channel] != expected[channel]) {
                    throw std::runtime_error(std::string(label) + ": pixel mismatch at " +
                                             std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
    }
    std::cout << label << ": exact 256 pixels RGBA("
              << std::to_integer<unsigned>(expected[0]) << ','
              << std::to_integer<unsigned>(expected[1]) << ','
              << std::to_integer<unsigned>(expected[2]) << ','
              << std::to_integer<unsigned>(expected[3]) << ")\n";
}

inline renderer::FrameGraph color_graph(rhi::Texture& target,
                                        renderer::RenderBatch batch) {
    renderer::FrameGraph graph;
    graph.add_node(std::make_unique<renderer::RenderPassNode>(
        rhi::RenderPassDesc{
            .extent = {16, 16},
            .colorAttachments = {{.texture = &target, .clear = {0, 0, 0, 1}}},
        },
        std::vector<renderer::RenderBatch>{std::move(batch)}));
    return graph;
}

} // namespace truffle::tests
