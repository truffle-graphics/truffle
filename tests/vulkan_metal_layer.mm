#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

#include "vulkan_metal_layer.hpp"

#include <cassert>

namespace truffle::tests {
namespace {

void apply_layer_size(CAMetalLayer* layer, std::uint32_t width,
                      std::uint32_t height) {
    layer.contentsScale = 1.0;
    layer.bounds = CGRectMake(0, 0, width, height);
    layer.drawableSize = CGSizeMake(width, height);
}

} // namespace

VulkanMetalTestLayer::VulkanMetalTestLayer(std::uint32_t width,
                                           std::uint32_t height) {
    @autoreleasepool {
        CAMetalLayer* layer = [CAMetalLayer layer];
        assert(layer != nil);
        apply_layer_size(layer, width, height);
        layer_ = const_cast<void*>(CFBridgingRetain(layer));
    }
}

VulkanMetalTestLayer::~VulkanMetalTestLayer() {
    if (layer_ != nullptr) {
        CFRelease(layer_);
    }
}

void VulkanMetalTestLayer::resize(std::uint32_t width, std::uint32_t height) {
    auto* layer = (__bridge CAMetalLayer*)layer_;
    apply_layer_size(layer, width, height);
    assert(layer.drawableSize.width == width &&
           layer.drawableSize.height == height);
}

} // namespace truffle::tests
