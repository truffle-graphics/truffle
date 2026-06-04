#include "test_support.hpp"

#include "truffle/core/config.hpp"
#include "truffle/core/handle.hpp"
#include "truffle/core/status.hpp"
#include "truffle/rhi/validation.hpp"

#include <cstddef>

namespace {

struct TestHandleTag {};

} // namespace

int main() {
    const auto ok = truffle::core::Status::success();
    TRUFFLE_CHECK(ok.ok());

    const auto failure = truffle::core::Status::failure(
        truffle::core::StatusCode::invalid_argument, "bad argument");
    TRUFFLE_CHECK(!failure.ok());
    TRUFFLE_CHECK(failure.code == truffle::core::StatusCode::invalid_argument);
    TRUFFLE_CHECK(failure.message == "bad argument");

    truffle::core::Result<int> valueResult{42};
    TRUFFLE_CHECK(valueResult.ok());
    TRUFFLE_CHECK(valueResult.value() == 42);

    truffle::core::Result<int> errorResult{failure};
    TRUFFLE_CHECK(!errorResult.ok());
    TRUFFLE_CHECK(errorResult.status().code ==
                  truffle::core::StatusCode::invalid_argument);

    using TestHandle = truffle::core::Handle<TestHandleTag>;
    constexpr TestHandle defaultHandle;
    static_assert(!defaultHandle.valid());
    static_assert(defaultHandle.value() == TestHandle::invalid_value);

    constexpr TestHandle explicitHandle{7};
    static_assert(explicitHandle.valid());
    static_assert(explicitHandle.value() == 7);
    static_assert(explicitHandle == TestHandle{7});

    const truffle::core::RuntimeConfig config;
    TRUFFLE_CHECK(config.application_name == "Truffle Consumer");
    TRUFFLE_CHECK(config.enable_validation);

    TRUFFLE_CHECK(truffle::rhi::validation::is_non_zero({1, 1}));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_non_zero({0, 1}));
    TRUFFLE_CHECK(truffle::rhi::validation::extent_within({8, 8}, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::extent_within({17, 8}, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::extent_within({8, 8}, 0));
    TRUFFLE_CHECK(truffle::rhi::validation::is_power_of_two(1));
    TRUFFLE_CHECK(truffle::rhi::validation::is_power_of_two(64));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_power_of_two(0));
    TRUFFLE_CHECK(!truffle::rhi::validation::is_power_of_two(3));

    std::size_t aligned = 0;
    TRUFFLE_CHECK(truffle::rhi::validation::align_up(17, 8, aligned));
    TRUFFLE_CHECK(aligned == 24);
    TRUFFLE_CHECK(!truffle::rhi::validation::align_up(17, 0, aligned));
    TRUFFLE_CHECK(!truffle::rhi::validation::align_up(17, 3, aligned));

    TRUFFLE_CHECK(truffle::rhi::validation::range_fits(8, 4, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::range_fits(8, 9, 16));
    TRUFFLE_CHECK(!truffle::rhi::validation::range_fits(
        static_cast<std::size_t>(-4), 8, static_cast<std::size_t>(-1)));

    truffle::rhi::BufferDesc legacyStorage;
    legacyStorage.usage = truffle::rhi::BufferUsage::storage;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(legacyStorage),
        truffle::rhi::BufferUsageFlags::storage));

    truffle::rhi::BufferDesc multiUseBuffer;
    multiUseBuffer.usageFlags = truffle::rhi::BufferUsageFlags::vertex |
                                truffle::rhi::BufferUsageFlags::uniform;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(multiUseBuffer),
        truffle::rhi::BufferUsageFlags::vertex));
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_buffer_usage(multiUseBuffer),
        truffle::rhi::BufferUsageFlags::uniform));
    TRUFFLE_CHECK(truffle::rhi::validation::buffer_supports_state(
        multiUseBuffer, truffle::rhi::ResourceState::shader_read));
    TRUFFLE_CHECK(!truffle::rhi::validation::buffer_supports_state(
        multiUseBuffer, truffle::rhi::ResourceState::copy_destination));

    truffle::rhi::TextureDesc colorTexture;
    colorTexture.format = truffle::rhi::TextureFormat::rgba8_unorm;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_texture_usage(colorTexture),
        truffle::rhi::TextureUsageFlags::color_attachment));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_supports_state(
        colorTexture, truffle::rhi::ResourceState::color_attachment));
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_supports_state(
        colorTexture, truffle::rhi::ResourceState::copy_destination));

    truffle::rhi::TextureDesc depthTexture;
    depthTexture.format = truffle::rhi::TextureFormat::depth32_float;
    TRUFFLE_CHECK(truffle::rhi::has_flag(
        truffle::rhi::effective_texture_usage(depthTexture),
        truffle::rhi::TextureUsageFlags::depth_stencil));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));
    depthTexture.mipLevels = 0;
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));
    depthTexture.mipLevels = 1;
    depthTexture.dimension = truffle::rhi::TextureDimension::cube;
    depthTexture.arrayLayers = 5;
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_shape_valid(depthTexture, 4096));

    truffle::rhi::Capabilities capabilities;
    capabilities.maxFramesInFlight = 2;
    capabilities.queues = {.graphics = true, .compute = true};
    capabilities.limits.maxTextureDimension2D = 64;
    capabilities.memoryHeaps = {{
        .kind = truffle::rhi::MemoryHeapKind::unified,
    }};
    capabilities.formats = {{
        .format = truffle::rhi::TextureFormat::rgba8_unorm,
        .sampled = true,
        .colorAttachment = true,
    }};
    capabilities.surfaceKinds = {truffle::rhi::NativeSurfaceKind::headless};
    TRUFFLE_CHECK(truffle::rhi::validation::frame_count_supported(2, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::frame_count_supported(0, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::frame_count_supported(3, capabilities));
    TRUFFLE_CHECK(truffle::rhi::supports_queue(
        capabilities, truffle::rhi::QueueKind::graphics));
    TRUFFLE_CHECK(!truffle::rhi::supports_queue(
        capabilities, truffle::rhi::QueueKind::transfer));
    TRUFFLE_CHECK(truffle::rhi::supports_texture_format(
        capabilities, truffle::rhi::TextureFormat::rgba8_unorm));
    TRUFFLE_CHECK(!truffle::rhi::supports_texture_format(
        capabilities, truffle::rhi::TextureFormat::depth32_float));
    TRUFFLE_CHECK(truffle::rhi::validation::memory_domain_supported(
        truffle::rhi::MemoryDomain::upload, capabilities));
    TRUFFLE_CHECK(truffle::rhi::validation::texture_usage_supported_by_format(
        capabilities, colorTexture));
    TRUFFLE_CHECK(!truffle::rhi::validation::texture_usage_supported_by_format(
        capabilities, depthTexture));

    TRUFFLE_CHECK(truffle::rhi::supports_native_surface_kind(
        capabilities, truffle::rhi::NativeSurfaceKind::headless));
    TRUFFLE_CHECK(!truffle::rhi::supports_native_surface_kind(
        capabilities, truffle::rhi::NativeSurfaceKind::win32));
    TRUFFLE_CHECK(truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::headless,
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::headless,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::cocoa_layer,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(!truffle::rhi::validation::native_surface_handles_valid({
        .kind = truffle::rhi::NativeSurfaceKind::xcb,
        .handle = reinterpret_cast<void*>(0x1),
    }));
    TRUFFLE_CHECK(truffle::rhi::validation::surface_supported({
        .native = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {32, 32},
    }, capabilities));
    TRUFFLE_CHECK(!truffle::rhi::validation::surface_supported({
        .native = {
            .kind = truffle::rhi::NativeSurfaceKind::win32,
            .handle = reinterpret_cast<void*>(0x1),
        },
        .initialExtent = {32, 32},
    }, capabilities));

    return 0;
}
