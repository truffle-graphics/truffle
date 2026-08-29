#pragma once

#include "truffle/rhi/rhi.hpp"

#include <array>
#include <cassert>
#include <utility>

namespace truffle::tests {

inline void verify_native_backend_smoke(rhi::Result<rhi::Instance> result,
                                        rhi::BackendKind backend,
                                        rhi::PlatformKind platform) {
    assert(result.ok());
    auto instance = std::move(result).value();
    assert(instance.backend() == backend);
    assert(instance.adapter_count() == 1);
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().backend == backend);
    assert(adapter.info().platform == platform);
    assert(adapter.info().maturity == rhi::BackendMaturity::native_smoke);
    assert(adapter.info().native);
    assert(!adapter.info().validationOnly);

    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    assert(list.end().ok());
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());
}

inline void verify_unavailable_backend(const rhi::Result<rhi::Instance>& result) {
    assert(!result.ok());
    assert(result.status().code == core::StatusCode::unsupported ||
           result.status().code == core::StatusCode::unavailable);
}

} // namespace truffle::tests
