#import <Metal/Metal.h>

#include "truffle/rhi/metal_backend.hpp"

#include "foundation_backend.hpp"

#include <string>

namespace truffle::rhi {
namespace {

Status submit_empty_metal_command() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "Metal device is no longer available");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil) {
            return Status::failure(StatusCode::backend_error,
                                   "Metal command queue creation failed");
        }
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (command == nil) {
            return Status::failure(StatusCode::backend_error,
                                   "Metal command buffer creation failed");
        }
        [command commit];
        [command waitUntilCompleted];
        if (command.status == MTLCommandBufferStatusError) {
            const auto message = command.error.localizedDescription != nil
                                     ? std::string{
                                           command.error.localizedDescription.UTF8String}
                                     : std::string{"Metal command submission failed"};
            return Status::failure(
                StatusCode::backend_error, message,
                BackendDiagnostic{
                    .domain = "Metal",
                    .nativeCode = command.error.code,
                    .message = message,
                });
        }
    }
    return Status::success();
}

} // namespace

Result<Instance> create_metal_instance(const InstanceDesc& desc) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        const auto* name = device.name.UTF8String;
        return detail::create_foundation_instance(
            desc,
            {
                .kind = BackendKind::metal,
                .adapterName = name != nullptr ? name : "Metal adapter",
                .queueKinds = {QueueKind::graphics},
                .supportedFeatures = {},
                .native = true,
                .validationOnly = false,
                .presentation = false,
                .logicalResources = false,
                .nativeSubmit = &submit_empty_metal_command,
            });
    }
}

} // namespace truffle::rhi
