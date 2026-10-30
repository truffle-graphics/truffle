#pragma once

#include "truffle/rhi/types.hpp"

namespace truffle::rhi::detail {

[[nodiscard]] Status validate_interfaces(ShaderStage stage,
                                        std::span<const ShaderInterfaceVariable> interfaces);
[[nodiscard]] std::string_view interface_type(std::string_view type) noexcept;
[[nodiscard]] Status validate_graphics_interfaces(const ShaderDesc* vertex,
    const ShaderDesc* fragment, const PipelineDesc& pipeline);
[[nodiscard]] Status validate_workgroup(ShaderStage stage, Extent3D defaults,
                                        const WorkgroupSpecializationIds& ids,
                                        std::span<const ShaderSpecializationConstant> constants);
[[nodiscard]] Result<Extent3D> resolve_workgroup(
    const ShaderDesc& shader, std::span<const SpecializationValue> values);

} // namespace truffle::rhi::detail
