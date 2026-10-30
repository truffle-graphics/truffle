#pragma once

#include "truffle/shader/compiler.hpp"

namespace truffle::shader::detail {

[[nodiscard]] bool native_msl_available() noexcept;
[[nodiscard]] bool offline_metallib_available() noexcept;
[[nodiscard]] rhi::Result<Compilation> compile_msl(
    const CompileRequest& request, std::vector<Compilation>& cache);

} // namespace truffle::shader::detail
