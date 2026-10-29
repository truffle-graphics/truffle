#include "truffle/base/config.hpp"
#include "truffle/base/handle.hpp"
#include "truffle/base/status.hpp"
#include "truffle/base/version.hpp"
#include "truffle/rhi/rhi.hpp"
#include "truffle/rhi/validation.hpp"

#include <cassert>
#include <type_traits>
#include <utility>

int main() {
    using namespace truffle;

    static_assert(std::is_same_v<rhi::Status, base::Status>);
    static_assert(std::is_same_v<rhi::Result<int>, base::Result<int>>);
    static_assert(base::kApiVersionPacked == base::pack_api_version(base::kApiVersion));

    struct TestHandleTag {};
    using TestHandle = base::Handle<TestHandleTag>;
    static_assert(!TestHandle{}.valid());
    constexpr TestHandle handle{42};
    static_assert(handle.valid() && handle.value() == 42);

    const base::RuntimeConfig config;
    assert(config.application_name == "Truffle Consumer");
    assert(config.enable_validation);

    const base::Result<int> result{7};
    assert(result.ok() && result.value() == 7);

    static_assert(!std::is_copy_constructible_v<rhi::Instance>);
    static_assert(!std::is_copy_constructible_v<rhi::Device>);
    static_assert(!std::is_copy_constructible_v<rhi::CommandList>);
    static_assert(!std::is_copy_constructible_v<rhi::RenderEncoder>);
    static_assert(std::is_nothrow_move_constructible_v<rhi::Instance>);
    static_assert(std::is_nothrow_move_constructible_v<rhi::Device>);
    static_assert(std::is_nothrow_move_constructible_v<rhi::Buffer>);

    constexpr auto usage = rhi::BufferUsage::vertex |
                           rhi::BufferUsage::copy_destination;
    static_assert(rhi::has_usage(usage, rhi::BufferUsage::vertex));
    static_assert(!rhi::has_usage(usage, rhi::BufferUsage::storage));

    assert(rhi::validation::is_non_zero(rhi::Extent2D{1, 1}));
    assert(!rhi::validation::is_non_zero(rhi::Extent2D{}));
    assert(rhi::validation::buffer_desc_valid({
        .size = 4,
        .usage = rhi::BufferUsage::uniform,
    }));
    assert(!rhi::validation::buffer_desc_valid({}));
    assert(rhi::validation::native_surface_handles_valid({}));
    assert(!rhi::validation::native_surface_handles_valid({
        .kind = rhi::NativeSurfaceKind::external,
    }));

    rhi::PipelineReflection reflection{{
        {
            .name = "frame",
            .stage = rhi::ShaderStage::vertex,
            .type = rhi::ResourceBindingType::buffer,
            .group = 0,
            .binding = 2,
        },
    }};
    assert(reflection.bindings().size() == 1);
    assert(reflection.find(0, 2, rhi::ShaderStage::vertex) != nullptr);
    assert(reflection.find(1, 2, rhi::ShaderStage::vertex) == nullptr);

    const auto status = base::Status::failure(
        base::StatusCode::backend_validation_failed, "validation failed",
        {
            .domain = "test",
            .nativeCode = 42,
            .objectLabel = "buffer",
            .message = "native detail",
        });
    assert(!status.ok());
    assert(status.detail.has_value());
    assert(status.detail->nativeCode == 42);
    return 0;
}
