#pragma once

#include "truffle/core/status.hpp"

namespace truffle::rhi {

using Status = core::Status;
using StatusCode = core::StatusCode;
using BackendDiagnostic = core::StatusDetail;

template <typename T>
using Result = core::Result<T>;

} // namespace truffle::rhi
