#include "truffle/rhi/metal_backend.hpp"

#include <array>
#include <cassert>

int main() {
    using namespace truffle;
    auto instanceResult = rhi::create_metal_instance();
    if (!instanceResult.ok()) {
        assert(instanceResult.status().code == core::StatusCode::unavailable);
        return 0;
    }
    auto instance = std::move(instanceResult).value();
    assert(instance.backend() == rhi::BackendKind::metal);
    assert(instance.adapter_count() == 1);

    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().native);
    assert(!adapter.info().validationOnly);

    assert(!adapter.info().presentation);
    assert(adapter.info().supportedFeatures.empty());
    assert(adapter.info().queueKinds.size() == 1);
    assert(adapter.info().queueKinds.front() == rhi::QueueKind::graphics);

    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    assert(!device.lost());

    auto bufferResult = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::storage,
    });
    assert(!bufferResult.ok());
    assert(bufferResult.status().code == core::StatusCode::unsupported);

    auto computeQueueResult = device.queue(rhi::QueueKind::compute);
    assert(!computeQueueResult.ok());
    assert(computeQueueResult.status().code == core::StatusCode::unsupported);

    auto fenceResult = device.create_fence();
    assert(!fenceResult.ok());
    assert(fenceResult.status().code == core::StatusCode::unsupported);

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
    assert(instance.stats().submissions == 1);
    return 0;
}
