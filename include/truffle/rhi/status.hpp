#pragma once

#include "truffle/base/status.hpp"

namespace truffle::rhi {

using Status = base::Status;
using StatusCode = base::StatusCode;
using BackendDiagnostic = base::StatusDetail;

template <typename T>
using Result = base::Result<T>;

} // namespace truffle::rhi
