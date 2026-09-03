#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>

#include "truffle/rhi/metal_backend.hpp"

#include "foundation_backend.hpp"
#include "metal_backend_test.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace truffle::rhi {
namespace detail {

std::atomic<bool> gMetalDeviceLossForTesting{false};
std::atomic<MetalAcquireFault> gMetalAcquireFaultForTesting{
    MetalAcquireFault::none};

void set_metal_device_loss_for_testing(bool enabled) noexcept {
    gMetalDeviceLossForTesting.store(enabled);
}

void set_metal_acquire_fault_for_testing(MetalAcquireFault fault) noexcept {
    gMetalAcquireFaultForTesting.store(fault);
}

} // namespace detail

namespace {

struct MetalBufferResource {
    explicit MetalBufferResource(id<MTLBuffer> bufferValue)
        : buffer(bufferValue) {}
    ~MetalBufferResource() { [buffer release]; }
    MetalBufferResource(const MetalBufferResource&) = delete;
    MetalBufferResource& operator=(const MetalBufferResource&) = delete;

    id<MTLBuffer> buffer = nil;
};

struct MetalTextureResource {
    MetalTextureResource(id<MTLTexture> textureValue, TextureDesc descValue,
                         std::uint32_t bytesPerPixelValue)
        : texture(textureValue), desc(std::move(descValue)),
          bytesPerPixel(bytesPerPixelValue) {}
    ~MetalTextureResource() { [texture release]; }
    MetalTextureResource(const MetalTextureResource&) = delete;
    MetalTextureResource& operator=(const MetalTextureResource&) = delete;

    id<MTLTexture> texture = nil;
    TextureDesc desc;
    std::uint32_t bytesPerPixel = 0;
};

struct MetalTextureViewResource {
    explicit MetalTextureViewResource(id<MTLTexture> textureValue)
        : texture(textureValue) {}
    ~MetalTextureViewResource() { [texture release]; }
    MetalTextureViewResource(const MetalTextureViewResource&) = delete;
    MetalTextureViewResource& operator=(const MetalTextureViewResource&) = delete;

    id<MTLTexture> texture = nil;
};

struct MetalSamplerResource {
    explicit MetalSamplerResource(id<MTLSamplerState> samplerValue)
        : sampler(samplerValue) {}
    ~MetalSamplerResource() { [sampler release]; }
    MetalSamplerResource(const MetalSamplerResource&) = delete;
    MetalSamplerResource& operator=(const MetalSamplerResource&) = delete;

    id<MTLSamplerState> sampler = nil;
};

struct MetalShaderResource {
    MetalShaderResource(id<MTLLibrary> libraryValue,
                        id<MTLFunction> functionValue, ShaderDesc descValue)
        : library(libraryValue), function(functionValue),
          desc(std::move(descValue)) {}
    ~MetalShaderResource() {
        [function release];
        [library release];
    }
    MetalShaderResource(const MetalShaderResource&) = delete;
    MetalShaderResource& operator=(const MetalShaderResource&) = delete;

    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    ShaderDesc desc;
};

struct MetalPipelineResource {
    MetalPipelineResource(id<MTLRenderPipelineState> pipelineValue,
                          id<MTLDepthStencilState> depthStencilValue,
                          PipelineDesc descValue,
                          std::vector<ShaderBindingMap> vertexMapValue,
                          std::vector<ShaderBindingMap> fragmentMapValue)
        : pipeline(pipelineValue), depthStencil(depthStencilValue),
          desc(std::move(descValue)), vertexMap(std::move(vertexMapValue)),
          fragmentMap(std::move(fragmentMapValue)) {
        desc.vertexShader = nullptr;
        desc.fragmentShader = nullptr;
        desc.layout = nullptr;
        desc.cache = nullptr;
    }
    ~MetalPipelineResource() {
        [pipeline release];
        [depthStencil release];
    }
    MetalPipelineResource(const MetalPipelineResource&) = delete;
    MetalPipelineResource& operator=(const MetalPipelineResource&) = delete;

    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depthStencil = nil;
    PipelineDesc desc;
    std::vector<ShaderBindingMap> vertexMap;
    std::vector<ShaderBindingMap> fragmentMap;
};

struct MetalComputePipelineResource {
    MetalComputePipelineResource(id<MTLComputePipelineState> pipelineValue,
                                 ComputePipelineDesc descValue,
                                 std::vector<ShaderBindingMap> bindingMapValue)
        : pipeline(pipelineValue), desc(std::move(descValue)),
          bindingMap(std::move(bindingMapValue)) {
        desc.computeShader = nullptr;
        desc.layout = nullptr;
        desc.cache = nullptr;
    }
    ~MetalComputePipelineResource() { [pipeline release]; }
    MetalComputePipelineResource(const MetalComputePipelineResource&) = delete;
    MetalComputePipelineResource& operator=(const MetalComputePipelineResource&) =
        delete;

    id<MTLComputePipelineState> pipeline = nil;
    ComputePipelineDesc desc;
    std::vector<ShaderBindingMap> bindingMap;
};

struct MetalSemaphoreResource {
    explicit MetalSemaphoreResource(id<MTLSharedEvent> eventValue)
        : event(eventValue) {}
    ~MetalSemaphoreResource() { [event release]; }

    id<MTLSharedEvent> event = nil;
};

struct MetalSurfaceResource {
    explicit MetalSurfaceResource(CAMetalLayer* layerValue)
        : layer([layerValue retain]) {}
    ~MetalSurfaceResource() { [layer release]; }

    CAMetalLayer* layer = nil;
};

struct MetalSwapchainResource {
    MetalSwapchainResource(std::shared_ptr<MetalSurfaceResource> surfaceValue,
                           SwapchainDesc descValue)
        : surface(std::move(surfaceValue)), desc(std::move(descValue)) {}
    ~MetalSwapchainResource() { [drawable release]; }

    std::shared_ptr<MetalSurfaceResource> surface;
    SwapchainDesc desc;
    id<CAMetalDrawable> drawable = nil;
    std::uint32_t imageIndex = 0;
    std::uint32_t nextImage = 0;
    std::mutex mutex;
};

[[nodiscard]] id<MTLDevice> system_device() {
    static std::once_flag once;
    static id<MTLDevice> device = nil;
    std::call_once(once, [] { device = MTLCreateSystemDefaultDevice(); });
    return device;
}

[[nodiscard]] Status metal_failure(StatusCode code, std::string message,
                                   NSInteger nativeCode = 0) {
    return Status::failure(
        code, message,
        BackendDiagnostic{
            .domain = "Metal",
            .nativeCode = nativeCode,
            .message = std::move(message),
        });
}

[[nodiscard]] std::string metal_error_message(NSError* error,
                                              std::string fallback) {
    return error != nil && error.localizedDescription != nil
               ? std::string{error.localizedDescription.UTF8String}
               : std::move(fallback);
}

[[nodiscard]] StatusCode metal_command_status_code(NSError* error) {
    return error != nil &&
                   error.code == MTLCommandBufferErrorDeviceRemoved
               ? StatusCode::device_lost
               : StatusCode::backend_error;
}

[[nodiscard]] MTLCompareFunction metal_compare(CompareOp operation) {
    switch (operation) {
    case CompareOp::never:
        return MTLCompareFunctionNever;
    case CompareOp::less:
        return MTLCompareFunctionLess;
    case CompareOp::equal:
        return MTLCompareFunctionEqual;
    case CompareOp::less_equal:
        return MTLCompareFunctionLessEqual;
    case CompareOp::greater:
        return MTLCompareFunctionGreater;
    case CompareOp::not_equal:
        return MTLCompareFunctionNotEqual;
    case CompareOp::greater_equal:
        return MTLCompareFunctionGreaterEqual;
    case CompareOp::always:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

[[nodiscard]] MTLStencilOperation metal_stencil_operation(
    StencilOp operation) {
    switch (operation) {
    case StencilOp::keep:
        return MTLStencilOperationKeep;
    case StencilOp::zero:
        return MTLStencilOperationZero;
    case StencilOp::replace:
        return MTLStencilOperationReplace;
    case StencilOp::increment_clamp:
        return MTLStencilOperationIncrementClamp;
    case StencilOp::decrement_clamp:
        return MTLStencilOperationDecrementClamp;
    case StencilOp::invert:
        return MTLStencilOperationInvert;
    case StencilOp::increment_wrap:
        return MTLStencilOperationIncrementWrap;
    case StencilOp::decrement_wrap:
        return MTLStencilOperationDecrementWrap;
    }
    return MTLStencilOperationKeep;
}

[[nodiscard]] MTLBlendFactor metal_blend_factor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::zero:
        return MTLBlendFactorZero;
    case BlendFactor::one:
        return MTLBlendFactorOne;
    case BlendFactor::source_color:
        return MTLBlendFactorSourceColor;
    case BlendFactor::one_minus_source_color:
        return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::destination_color:
        return MTLBlendFactorDestinationColor;
    case BlendFactor::one_minus_destination_color:
        return MTLBlendFactorOneMinusDestinationColor;
    case BlendFactor::source_alpha:
        return MTLBlendFactorSourceAlpha;
    case BlendFactor::one_minus_source_alpha:
        return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::destination_alpha:
        return MTLBlendFactorDestinationAlpha;
    case BlendFactor::one_minus_destination_alpha:
        return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorOne;
}

[[nodiscard]] MTLBlendOperation metal_blend_operation(BlendOp operation) {
    switch (operation) {
    case BlendOp::add:
        return MTLBlendOperationAdd;
    case BlendOp::subtract:
        return MTLBlendOperationSubtract;
    case BlendOp::reverse_subtract:
        return MTLBlendOperationReverseSubtract;
    case BlendOp::minimum:
        return MTLBlendOperationMin;
    case BlendOp::maximum:
        return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

[[nodiscard]] MTLVertexFormat metal_vertex_format(VertexFormat format) {
    switch (format) {
    case VertexFormat::float32:
        return MTLVertexFormatFloat;
    case VertexFormat::float32x2:
        return MTLVertexFormatFloat2;
    case VertexFormat::float32x3:
        return MTLVertexFormatFloat3;
    case VertexFormat::float32x4:
        return MTLVertexFormatFloat4;
    case VertexFormat::uint32:
        return MTLVertexFormatUInt;
    case VertexFormat::uint32x2:
        return MTLVertexFormatUInt2;
    case VertexFormat::uint32x3:
        return MTLVertexFormatUInt3;
    case VertexFormat::uint32x4:
        return MTLVertexFormatUInt4;
    }
    return MTLVertexFormatInvalid;
}

[[nodiscard]] MTLPrimitiveTopologyClass metal_topology_class(
    PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::point_list:
        return MTLPrimitiveTopologyClassPoint;
    case PrimitiveTopology::line_list:
        return MTLPrimitiveTopologyClassLine;
    case PrimitiveTopology::triangle_list:
    case PrimitiveTopology::triangle_strip:
        return MTLPrimitiveTopologyClassTriangle;
    case PrimitiveTopology::patch_list:
        return MTLPrimitiveTopologyClassUnspecified;
    }
    return MTLPrimitiveTopologyClassUnspecified;
}

[[nodiscard]] MTLPrimitiveType metal_primitive_type(
    PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::point_list:
        return MTLPrimitiveTypePoint;
    case PrimitiveTopology::line_list:
        return MTLPrimitiveTypeLine;
    case PrimitiveTopology::triangle_strip:
        return MTLPrimitiveTypeTriangleStrip;
    case PrimitiveTopology::triangle_list:
    case PrimitiveTopology::patch_list:
        return MTLPrimitiveTypeTriangle;
    }
    return MTLPrimitiveTypeTriangle;
}

[[nodiscard]] MTLSamplerAddressMode metal_address_mode(
    SamplerAddressMode mode) {
    switch (mode) {
    case SamplerAddressMode::clamp_to_edge:
        return MTLSamplerAddressModeClampToEdge;
    case SamplerAddressMode::repeat:
        return MTLSamplerAddressModeRepeat;
    case SamplerAddressMode::mirror_repeat:
        return MTLSamplerAddressModeMirrorRepeat;
    }
    return MTLSamplerAddressModeClampToEdge;
}

[[nodiscard]] MTLLoadAction metal_load_action(LoadOp operation) {
    switch (operation) {
    case LoadOp::load:
        return MTLLoadActionLoad;
    case LoadOp::clear:
        return MTLLoadActionClear;
    case LoadOp::dont_care:
        return MTLLoadActionDontCare;
    }
    return MTLLoadActionDontCare;
}

[[nodiscard]] MTLStoreAction metal_store_action(StoreOp operation) {
    return operation == StoreOp::store ? MTLStoreActionStore
                                       : MTLStoreActionDontCare;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_sampler(
    const std::shared_ptr<void>&, const SamplerDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        auto* descriptor = [[MTLSamplerDescriptor alloc] init];
        descriptor.minFilter = desc.minFilter == Filter::linear
                                   ? MTLSamplerMinMagFilterLinear
                                   : MTLSamplerMinMagFilterNearest;
        descriptor.magFilter = desc.magFilter == Filter::linear
                                   ? MTLSamplerMinMagFilterLinear
                                   : MTLSamplerMinMagFilterNearest;
        descriptor.mipFilter = desc.mipFilter == Filter::linear
                                   ? MTLSamplerMipFilterLinear
                                   : MTLSamplerMipFilterNearest;
        descriptor.sAddressMode = metal_address_mode(desc.addressU);
        descriptor.tAddressMode = metal_address_mode(desc.addressV);
        descriptor.rAddressMode = metal_address_mode(desc.addressW);
        descriptor.lodMinClamp = desc.lodMin;
        descriptor.lodMaxClamp = desc.lodMax;
        descriptor.maxAnisotropy = static_cast<NSUInteger>(
            std::clamp(desc.maxAnisotropy, 1.0F, 16.0F));
        descriptor.compareFunction = metal_compare(desc.compare);
        if (!desc.debugName.empty()) {
            descriptor.label =
                [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        id<MTLSamplerState> sampler =
            [device newSamplerStateWithDescriptor:descriptor];
        [descriptor release];
        if (sampler == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal sampler creation failed");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalSamplerResource>(sampler));
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_shader(
    const std::shared_ptr<void>&, const ShaderDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (desc.format != ShaderByteFormat::native_source &&
            desc.format != ShaderByteFormat::metal_library) {
            return Status::failure(
                StatusCode::unsupported,
                "Metal accepts MSL source and metallib shader variants");
        }
        NSError* error = nil;
        id<MTLLibrary> library = nil;
        if (desc.format == ShaderByteFormat::native_source) {
            auto* source = [[NSString alloc]
                initWithBytes:desc.code.data()
                       length:desc.code.size()
                     encoding:NSUTF8StringEncoding];
            if (source == nil) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal shader source is not valid UTF-8");
            }
            library = [device newLibraryWithSource:source
                                           options:nil
                                             error:&error];
            [source release];
        } else {
            const auto data = dispatch_data_create(
                desc.code.data(), desc.code.size(),
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            library = [device newLibraryWithData:data error:&error];
#if !OS_OBJECT_USE_OBJC
            dispatch_release(data);
#endif
        }
        if (library == nil) {
            const auto message =
                metal_error_message(error, "Metal shader compilation failed");
            return metal_failure(StatusCode::backend_error, message, error.code);
        }
        auto* entryPoint =
            [NSString stringWithUTF8String:desc.entryPoint.c_str()];
        id<MTLFunction> function = [library newFunctionWithName:entryPoint];
        if (function == nil) {
            [library release];
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal shader entry point was not found");
        }
        for (const auto& mapping : desc.bindingMap) {
            const auto reflected = std::find_if(
                desc.reflection.begin(), desc.reflection.end(),
                [&](const ResourceBinding& binding) {
                    return binding.group == mapping.group &&
                           binding.binding == mapping.binding;
                });
            if (reflected != desc.reflection.end() &&
                reflected->type == ResourceBindingType::buffer &&
                mapping.nativeBinding == 30) {
                [function release];
                [library release];
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Metal buffer index 30 is reserved for push constants");
            }
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalShaderResource>(library, function, desc));
    }
}

[[nodiscard]] Result<id<MTLFunction>> specialize_metal_function(
    const std::shared_ptr<MetalShaderResource>& shader,
    std::span<const SpecializationValue> values) {
    const auto hasValue = std::any_of(
        values.begin(), values.end(), [&](const SpecializationValue& value) {
            return std::any_of(
                shader->desc.specializationConstants.begin(),
                shader->desc.specializationConstants.end(),
                [&](const ShaderSpecializationConstant& constant) {
                    return constant.id == value.id;
                });
        });
    if (!hasValue) {
        [shader->function retain];
        return shader->function;
    }
    @autoreleasepool {
        auto* constants = [[MTLFunctionConstantValues alloc] init];
        for (const auto& value : values) {
            const auto reflected = std::find_if(
                shader->desc.specializationConstants.begin(),
                shader->desc.specializationConstants.end(),
                [&](const ShaderSpecializationConstant& constant) {
                    return constant.id == value.id;
                });
            if (reflected == shader->desc.specializationConstants.end()) {
                continue;
            }
            switch (value.type) {
            case ShaderValueType::boolean: {
                const bool typed = value.valueBits != 0;
                [constants setConstantValue:&typed
                                       type:MTLDataTypeBool
                                    atIndex:value.id];
                break;
            }
            case ShaderValueType::sint32: {
                const auto typed = static_cast<std::int32_t>(value.valueBits);
                [constants setConstantValue:&typed
                                       type:MTLDataTypeInt
                                    atIndex:value.id];
                break;
            }
            case ShaderValueType::uint32:
                [constants setConstantValue:&value.valueBits
                                       type:MTLDataTypeUInt
                                    atIndex:value.id];
                break;
            case ShaderValueType::float32:
                [constants setConstantValue:&value.valueBits
                                       type:MTLDataTypeFloat
                                    atIndex:value.id];
                break;
            }
        }
        NSError* error = nil;
        auto* entryPoint =
            [NSString stringWithUTF8String:shader->desc.entryPoint.c_str()];
        id<MTLFunction> function =
            [shader->library newFunctionWithName:entryPoint
                                  constantValues:constants
                                           error:&error];
        [constants release];
        if (function == nil) {
            const auto message = metal_error_message(
                error, "Metal shader specialization failed");
            return metal_failure(StatusCode::backend_error, message, error.code);
        }
        return function;
    }
}

[[nodiscard]] MTLResourceOptions buffer_options(MemoryDomain domain) {
    switch (domain) {
    case MemoryDomain::upload:
    case MemoryDomain::readback:
        return MTLResourceStorageModeShared;
    case MemoryDomain::device_local:
        return MTLResourceStorageModePrivate;
    case MemoryDomain::external:
        break;
    }
    return MTLResourceStorageModePrivate;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_buffer(
    const std::shared_ptr<void>&, const BufferDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        if (desc.memory == MemoryDomain::external || desc.shareable) {
            return Status::failure(StatusCode::unsupported,
                                   "Metal external buffer memory is not implemented");
        }
        id<MTLBuffer> buffer =
            [device newBufferWithLength:desc.size
                                options:buffer_options(desc.memory)];
        if (buffer == nil) {
            return metal_failure(StatusCode::out_of_memory,
                                 "Metal buffer allocation failed");
        }
        if (!desc.debugName.empty()) {
            buffer.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalBufferResource>(buffer));
    }
}

[[nodiscard]] Result<std::span<std::byte>> map_metal_buffer(
    const std::shared_ptr<void>& resource) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil ||
        metal->buffer.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal buffers are not host mappable");
    }
    auto* bytes = static_cast<std::byte*>(metal->buffer.contents);
    if (bytes == nullptr) {
        return metal_failure(StatusCode::backend_error,
                             "Metal buffer returned no mapped contents");
    }
    return std::span<std::byte>{bytes, metal->buffer.length};
}

[[nodiscard]] Status unmap_metal_buffer(const std::shared_ptr<void>& resource) {
    return resource ? Status::success()
                    : Status::failure(StatusCode::invalid_argument,
                                      "Metal buffer resource is invalid");
}

[[nodiscard]] Status flush_metal_buffer(const std::shared_ptr<void>& resource,
                                        std::size_t offset,
                                        std::size_t size) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil || offset > metal->buffer.length ||
        size > metal->buffer.length - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer flush range is invalid");
    }
#if TARGET_OS_OSX
    if (metal->buffer.storageMode == MTLStorageModeManaged) {
        [metal->buffer didModifyRange:NSMakeRange(offset, size)];
    }
#endif
    return Status::success();
}

[[nodiscard]] Status invalidate_metal_buffer(
    const std::shared_ptr<void>& resource, std::size_t offset,
    std::size_t size) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil || offset > metal->buffer.length ||
        size > metal->buffer.length - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer invalidate range is invalid");
    }
    return Status::success();
}

[[nodiscard]] Status write_metal_buffer(
    const std::shared_ptr<void>& resource, std::size_t offset,
    std::span<const std::byte> data) {
    auto mapped = map_metal_buffer(resource);
    if (!mapped.ok()) {
        return mapped.status();
    }
    auto bytes = std::move(mapped).value();
    if (offset > bytes.size() || data.size() > bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer write exceeds allocation");
    }
    std::copy(data.begin(), data.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return flush_metal_buffer(resource, offset, data.size());
}

[[nodiscard]] Status read_metal_buffer(const std::shared_ptr<void>& resource,
                                       std::size_t offset,
                                       std::span<std::byte> data) {
    if (auto status = invalidate_metal_buffer(resource, offset, data.size());
        !status.ok()) {
        return status;
    }
    auto mapped = map_metal_buffer(resource);
    if (!mapped.ok()) {
        return mapped.status();
    }
    const auto bytes = std::move(mapped).value();
    if (offset > bytes.size() || data.size() > bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer read exceeds allocation");
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                static_cast<std::ptrdiff_t>(data.size()), data.begin());
    return Status::success();
}

struct MetalFormat {
    MTLPixelFormat format = MTLPixelFormatInvalid;
    std::uint32_t bytesPerPixel = 0;
};

[[nodiscard]] MetalFormat metal_format(TextureFormat format) {
    switch (format) {
    case TextureFormat::r8_unorm:
        return {MTLPixelFormatR8Unorm, 1};
    case TextureFormat::rg8_unorm:
        return {MTLPixelFormatRG8Unorm, 2};
    case TextureFormat::rgba8_unorm:
        return {MTLPixelFormatRGBA8Unorm, 4};
    case TextureFormat::rgba8_srgb:
        return {MTLPixelFormatRGBA8Unorm_sRGB, 4};
    case TextureFormat::bgra8_unorm:
        return {MTLPixelFormatBGRA8Unorm, 4};
    case TextureFormat::bgra8_srgb:
        return {MTLPixelFormatBGRA8Unorm_sRGB, 4};
    case TextureFormat::rgba16_float:
        return {MTLPixelFormatRGBA16Float, 8};
    case TextureFormat::rgba32_float:
        return {MTLPixelFormatRGBA32Float, 16};
    case TextureFormat::depth16_unorm:
        return {MTLPixelFormatDepth16Unorm, 2};
    case TextureFormat::depth32_float:
        return {MTLPixelFormatDepth32Float, 4};
    case TextureFormat::depth32_float_stencil8:
        return {MTLPixelFormatDepth32Float_Stencil8, 8};
    default:
        return {};
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_surface(
    const SurfaceDesc& desc) {
    @autoreleasepool {
        if (desc.native.kind != NativeSurfaceKind::cocoa_layer ||
            desc.native.handle == nullptr) {
            return Status::failure(
                StatusCode::unsupported,
                "Metal surfaces require a CAMetalLayer native handle");
        }
        CAMetalLayer* layer = (__bridge CAMetalLayer*)desc.native.handle;
        if (layer == nil || ![layer isKindOfClass:[CAMetalLayer class]]) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal surface handle is not a CAMetalLayer");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalSurfaceResource>(layer));
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_swapchain(
    const std::shared_ptr<void>& nativeSurface, const SwapchainDesc& desc) {
    @autoreleasepool {
        const auto surface =
            std::static_pointer_cast<MetalSurfaceResource>(nativeSurface);
        const auto device = system_device();
        const auto format = metal_format(desc.format);
        if (!surface || surface->layer == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "Metal surface is no longer available");
        }
        if (device == nil) {
            return Status::failure(StatusCode::device_lost,
                                   "Metal device is no longer available");
        }
        if (format.format == MTLPixelFormatInvalid ||
            (desc.format != TextureFormat::bgra8_unorm &&
             desc.format != TextureFormat::bgra8_srgb &&
             desc.format != TextureFormat::rgba16_float)) {
            return Status::failure(
                StatusCode::unsupported,
                "Metal swapchain format is not CAMetalLayer-compatible");
        }
        surface->layer.device = device;
        surface->layer.pixelFormat = format.format;
        surface->layer.drawableSize =
            CGSizeMake(desc.extent.width, desc.extent.height);
        surface->layer.maximumDrawableCount =
            static_cast<NSUInteger>(std::clamp(desc.imageCount, 2u, 3u));
        surface->layer.displaySyncEnabled =
            desc.presentMode != PresentMode::immediate;
        surface->layer.allowsNextDrawableTimeout = YES;
        surface->layer.framebufferOnly = NO;
        return std::static_pointer_cast<void>(
            std::make_shared<MetalSwapchainResource>(surface, desc));
    }
}

[[nodiscard]] Result<detail::NativeSwapchainImage> acquire_metal_swapchain(
    const std::shared_ptr<void>& nativeSwapchain) {
    @autoreleasepool {
        if (detail::gMetalAcquireFaultForTesting.load() ==
            detail::MetalAcquireFault::out_of_date) {
            return Status::failure(StatusCode::out_of_date,
                                   "injected Metal out-of-date swapchain");
        }
        const auto swapchain =
            std::static_pointer_cast<MetalSwapchainResource>(nativeSwapchain);
        if (!swapchain || !swapchain->surface ||
            swapchain->surface->layer == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "Metal surface is no longer available");
        }
        std::lock_guard lock{swapchain->mutex};
        if (swapchain->drawable != nil) {
            return Status::failure(
                StatusCode::invalid_state,
                "Metal swapchain already has an acquired drawable");
        }
        const auto drawableSize = swapchain->surface->layer.drawableSize;
        if (drawableSize.width <= 0.0 || drawableSize.height <= 0.0) {
            return Status::failure(StatusCode::out_of_date,
                                   "Metal layer has a zero drawable extent");
        }
        if (swapchain->surface->layer.device == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "Metal layer is detached from its device");
        }
        id<CAMetalDrawable> drawable = [swapchain->surface->layer nextDrawable];
        if (drawable == nil) {
            return Status::failure(StatusCode::timeout,
                                   "Metal drawable acquisition timed out");
        }
        swapchain->drawable = [drawable retain];
        swapchain->imageIndex =
            swapchain->nextImage++ % swapchain->desc.imageCount;
        const auto width = static_cast<std::uint32_t>(drawable.texture.width);
        const auto height = static_cast<std::uint32_t>(drawable.texture.height);
        const auto format = metal_format(swapchain->desc.format);
        TextureDesc textureDesc{
            .extent = {width, height, 1},
            .format = swapchain->desc.format,
            .usage = TextureUsage::color_attachment | TextureUsage::present,
            .debugName = swapchain->desc.debugName + " drawable",
        };
        detail::NativeSwapchainImage result;
        result.texture = std::static_pointer_cast<void>(
            std::make_shared<MetalTextureResource>(
                [drawable.texture retain], std::move(textureDesc),
                format.bytesPerPixel));
        result.imageIndex = swapchain->imageIndex;
        result.extent = {width, height};
        result.status = width == swapchain->desc.extent.width &&
                                height == swapchain->desc.extent.height
                            ? Status::success()
                            : Status::failure(
                                  StatusCode::suboptimal,
                                  "Metal drawable extent differs from swapchain extent");
        return result;
    }
}

[[nodiscard]] Status resize_metal_swapchain(
    const std::shared_ptr<void>& nativeSwapchain, Extent2D extent) {
    @autoreleasepool {
        const auto swapchain =
            std::static_pointer_cast<MetalSwapchainResource>(nativeSwapchain);
        if (!swapchain || !swapchain->surface ||
            swapchain->surface->layer == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "Metal surface is no longer available");
        }
        std::lock_guard lock{swapchain->mutex};
        if (swapchain->drawable != nil) {
            return Status::failure(
                StatusCode::invalid_state,
                "Metal swapchain cannot resize an acquired drawable");
        }
        swapchain->surface->layer.drawableSize =
            CGSizeMake(extent.width, extent.height);
        swapchain->desc.extent = extent;
        return Status::success();
    }
}

[[nodiscard]] Status present_metal_swapchain(
    const std::shared_ptr<void>& nativeSwapchain, std::uint32_t imageIndex,
    std::span<const detail::NativeSemaphorePoint> waits) {
    @autoreleasepool {
        const auto swapchain =
            std::static_pointer_cast<MetalSwapchainResource>(nativeSwapchain);
        if (!swapchain || !swapchain->surface ||
            swapchain->surface->layer == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "Metal surface is no longer available");
        }
        std::lock_guard lock{swapchain->mutex};
        if (swapchain->drawable == nil || imageIndex != swapchain->imageIndex) {
            return Status::failure(StatusCode::invalid_state,
                                   "Metal presentation image is not acquired");
        }
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::device_lost,
                                   "Metal device is no longer available");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (queue == nil || command == nil) {
            [queue release];
            return metal_failure(StatusCode::backend_error,
                                 "Metal presentation command creation failed");
        }
        for (const auto& wait : waits) {
            const auto semaphore =
                std::static_pointer_cast<MetalSemaphoreResource>(wait.semaphore);
            if (!semaphore || semaphore->event == nil) {
                [queue release];
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal present semaphore is invalid");
            }
            [command encodeWaitForEvent:semaphore->event value:wait.value];
        }
        [command presentDrawable:swapchain->drawable];
        [command commit];
        [command waitUntilCompleted];
        const auto commandStatus = command.status;
        const auto error = command.error;
        [swapchain->drawable release];
        swapchain->drawable = nil;
        [queue release];
        if (commandStatus == MTLCommandBufferStatusError) {
            return metal_failure(metal_command_status_code(error),
                                 metal_error_message(
                                     error, "Metal presentation failed"),
                                 error.code);
        }
        return Status::success();
    }
}

[[nodiscard]] MTLStencilDescriptor* make_metal_stencil(
    const StencilFaceState& state, std::uint32_t readMask,
    std::uint32_t writeMask) {
    auto* descriptor = [[MTLStencilDescriptor alloc] init];
    descriptor.stencilCompareFunction = metal_compare(state.compare);
    descriptor.stencilFailureOperation =
        metal_stencil_operation(state.failOp);
    descriptor.depthFailureOperation =
        metal_stencil_operation(state.depthFailOp);
    descriptor.depthStencilPassOperation =
        metal_stencil_operation(state.passOp);
    descriptor.readMask = readMask;
    descriptor.writeMask = writeMask;
    return descriptor;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_pipeline(
    const std::shared_ptr<void>&, const PipelineDesc& desc,
    const detail::NativePipelineLayout&,
    const std::shared_ptr<void>& vertexResource,
    const std::shared_ptr<void>& fragmentResource) {
    @autoreleasepool {
        const auto unsupportedDynamic =
            static_cast<std::uint32_t>(desc.dynamicState) &
            ~(static_cast<std::uint32_t>(DynamicState::viewport) |
              static_cast<std::uint32_t>(DynamicState::scissor) |
              static_cast<std::uint32_t>(DynamicState::blend_constant) |
              static_cast<std::uint32_t>(DynamicState::stencil_reference) |
              static_cast<std::uint32_t>(DynamicState::depth_bias));
        if (desc.rasterization.polygonMode == PolygonMode::point ||
            unsupportedDynamic != 0 ||
            desc.multisample.sampleMask != 0xffffffffu) {
            return Status::failure(
                StatusCode::unsupported,
                "this Metal polygon or dynamic state is not implemented");
        }
        const auto vertex =
            std::static_pointer_cast<MetalShaderResource>(vertexResource);
        const auto fragment =
            std::static_pointer_cast<MetalShaderResource>(fragmentResource);
        if (!vertex || !fragment) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal graphics shaders are invalid");
        }
        auto vertexFunction =
            specialize_metal_function(vertex, desc.specializationConstants);
        if (!vertexFunction.ok()) {
            return vertexFunction.status();
        }
        auto fragmentFunction =
            specialize_metal_function(fragment, desc.specializationConstants);
        if (!fragmentFunction.ok()) {
            [vertexFunction.value() release];
            return fragmentFunction.status();
        }
        auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = vertexFunction.value();
        descriptor.fragmentFunction = fragmentFunction.value();
        descriptor.rasterSampleCount = desc.multisample.sampleCount;
        descriptor.alphaToCoverageEnabled =
            desc.multisample.alphaToCoverageEnabled;
        descriptor.inputPrimitiveTopology =
            metal_topology_class(desc.topology);
        if (!desc.debugName.empty()) {
            descriptor.label =
                [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        for (std::size_t index = 0; index < desc.colorTargets.size(); ++index) {
            const auto& target = desc.colorTargets[index];
            auto* output = descriptor.colorAttachments[index];
            output.pixelFormat = metal_format(target.format).format;
            output.writeMask = static_cast<MTLColorWriteMask>(target.writeMask);
            output.blendingEnabled = target.blend.enabled;
            output.sourceRGBBlendFactor =
                metal_blend_factor(target.blend.color.sourceFactor);
            output.destinationRGBBlendFactor =
                metal_blend_factor(target.blend.color.destinationFactor);
            output.rgbBlendOperation =
                metal_blend_operation(target.blend.color.operation);
            output.sourceAlphaBlendFactor =
                metal_blend_factor(target.blend.alpha.sourceFactor);
            output.destinationAlphaBlendFactor =
                metal_blend_factor(target.blend.alpha.destinationFactor);
            output.alphaBlendOperation =
                metal_blend_operation(target.blend.alpha.operation);
        }
        if (desc.depthStencil.format != TextureFormat::unknown) {
            const auto depthFormat = metal_format(desc.depthStencil.format).format;
            descriptor.depthAttachmentPixelFormat = depthFormat;
            if (desc.depthStencil.format ==
                TextureFormat::depth32_float_stencil8) {
                descriptor.stencilAttachmentPixelFormat = depthFormat;
            }
        }
        auto* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
        for (std::size_t bufferIndex = 0;
             bufferIndex < desc.vertexBuffers.size(); ++bufferIndex) {
            const auto& buffer = desc.vertexBuffers[bufferIndex];
            auto* layout = vertexDescriptor.layouts[bufferIndex];
            layout.stride = buffer.stride;
            layout.stepFunction =
                buffer.stepMode == VertexStepMode::instance
                    ? MTLVertexStepFunctionPerInstance
                    : MTLVertexStepFunctionPerVertex;
            layout.stepRate = 1;
            for (const auto& attribute : buffer.attributes) {
                auto* output = vertexDescriptor.attributes[attribute.location];
                output.format = metal_vertex_format(attribute.format);
                output.offset = attribute.offset;
                output.bufferIndex = bufferIndex;
            }
        }
        descriptor.vertexDescriptor = vertexDescriptor;
        NSError* error = nil;
        id<MTLRenderPipelineState> pipeline =
            [system_device() newRenderPipelineStateWithDescriptor:descriptor
                                                             error:&error];
        [vertexDescriptor release];
        [descriptor release];
        [vertexFunction.value() release];
        [fragmentFunction.value() release];
        if (pipeline == nil) {
            const auto message = metal_error_message(
                error, "Metal render pipeline creation failed");
            return metal_failure(StatusCode::backend_error, message, error.code);
        }

        auto* depth = [[MTLDepthStencilDescriptor alloc] init];
        depth.depthWriteEnabled = desc.depthStencil.depthWriteEnabled;
        depth.depthCompareFunction =
            metal_compare(desc.depthStencil.depthCompare);
        if (desc.depthStencil.format ==
            TextureFormat::depth32_float_stencil8) {
            auto* front = make_metal_stencil(
                desc.depthStencil.front, desc.depthStencil.stencilReadMask,
                desc.depthStencil.stencilWriteMask);
            auto* back = make_metal_stencil(
                desc.depthStencil.back, desc.depthStencil.stencilReadMask,
                desc.depthStencil.stencilWriteMask);
            depth.frontFaceStencil = front;
            depth.backFaceStencil = back;
            [front release];
            [back release];
        }
        id<MTLDepthStencilState> depthStencil =
            [system_device() newDepthStencilStateWithDescriptor:depth];
        [depth release];
        if (depthStencil == nil) {
            [pipeline release];
            return metal_failure(StatusCode::backend_error,
                                 "Metal depth-stencil state creation failed");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalPipelineResource>(
                pipeline, depthStencil, desc, vertex->desc.bindingMap,
                fragment->desc.bindingMap));
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_compute_pipeline(
    const std::shared_ptr<void>&, const ComputePipelineDesc& desc,
    const detail::NativePipelineLayout&,
    const std::shared_ptr<void>& shaderResource) {
    @autoreleasepool {
        const auto shader =
            std::static_pointer_cast<MetalShaderResource>(shaderResource);
        if (!shader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal compute shader is invalid");
        }
        auto function =
            specialize_metal_function(shader, desc.specializationConstants);
        if (!function.ok()) {
            return function.status();
        }
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline =
            [system_device() newComputePipelineStateWithFunction:function.value()
                                                            error:&error];
        [function.value() release];
        if (pipeline == nil) {
            const auto message = metal_error_message(
                error, "Metal compute pipeline creation failed");
            return metal_failure(StatusCode::backend_error, message, error.code);
        }
        if (pipeline.maxTotalThreadsPerThreadgroup <
            static_cast<NSUInteger>(desc.requiredWorkgroupSize.width) *
                desc.requiredWorkgroupSize.height *
                desc.requiredWorkgroupSize.depth) {
            [pipeline release];
            return Status::failure(
                StatusCode::unsupported,
                "Metal compute workgroup exceeds compiled pipeline limits");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalComputePipelineResource>(
                pipeline, desc, shader->desc.bindingMap));
    }
}

[[nodiscard]] MTLTextureUsage metal_texture_usage(TextureUsage usage) {
    MTLTextureUsage result = MTLTextureUsagePixelFormatView;
    if (has_usage(usage, TextureUsage::sampled)) {
        result |= MTLTextureUsageShaderRead;
    }
    if (has_usage(usage, TextureUsage::storage)) {
        result |= MTLTextureUsageShaderWrite;
    }
    if (has_usage(usage, TextureUsage::color_attachment) ||
        has_usage(usage, TextureUsage::depth_stencil_attachment)) {
        result |= MTLTextureUsageRenderTarget;
    }
    return result;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_texture(
    const std::shared_ptr<void>&, const TextureDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        const auto format = metal_format(desc.format);
        if (desc.dimension != TextureDimension::d2 || desc.extent.depth != 1 ||
            desc.arrayLayers != 1 || desc.sampleCount == 0 ||
            (desc.sampleCount > 1 &&
             (![device supportsTextureSampleCount:desc.sampleCount] ||
              desc.mipLevels != 1 ||
              desc.memory != MemoryDomain::device_local)) ||
            format.format == MTLPixelFormatInvalid || desc.shareable ||
            desc.memory == MemoryDomain::external) {
            return Status::failure(
                StatusCode::unsupported,
                "this Metal texture shape, format, sample count, or memory mode is unsupported");
        }
        auto* descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.textureType = desc.sampleCount > 1
                                     ? MTLTextureType2DMultisample
                                     : MTLTextureType2D;
        descriptor.pixelFormat = format.format;
        descriptor.width = desc.extent.width;
        descriptor.height = desc.extent.height;
        descriptor.depth = 1;
        descriptor.mipmapLevelCount = desc.mipLevels;
        descriptor.arrayLength = 1;
        descriptor.sampleCount = desc.sampleCount;
        descriptor.storageMode = desc.memory == MemoryDomain::device_local
                                     ? MTLStorageModePrivate
                                     : MTLStorageModeShared;
        descriptor.usage = metal_texture_usage(desc.usage);
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        [descriptor release];
        if (texture == nil) {
            return metal_failure(StatusCode::out_of_memory,
                                 "Metal texture allocation failed");
        }
        if (!desc.debugName.empty()) {
            texture.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalTextureResource>(texture, desc,
                                                   format.bytesPerPixel));
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_texture_view(
    const std::shared_ptr<void>& resource, const TextureViewDesc& desc) {
    @autoreleasepool {
        const auto metal =
            std::static_pointer_cast<MetalTextureResource>(resource);
        const auto format = metal_format(desc.format);
        if (!metal || metal->texture == nil ||
            format.format == MTLPixelFormatInvalid ||
            desc.dimension != TextureDimension::d2) {
            return Status::failure(StatusCode::unsupported,
                                   "this Metal texture view is unsupported");
        }
        const auto textureType = metal->texture.sampleCount > 1
                                     ? MTLTextureType2DMultisample
                                     : MTLTextureType2D;
        id<MTLTexture> view = [metal->texture
            newTextureViewWithPixelFormat:format.format
                               textureType:textureType
                                    levels:NSMakeRange(desc.range.baseMipLevel,
                                                       desc.range.mipLevelCount)
                                    slices:NSMakeRange(desc.range.baseArrayLayer,
                                                       desc.range.arrayLayerCount)];
        if (view == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal texture view creation failed");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalTextureViewResource>(view));
    }
}

[[nodiscard]] bool metal_texture_region_valid(
    const MetalTextureResource& texture, const TextureRegion& region) {
    if (region.subresource.mipLevel >= texture.desc.mipLevels ||
        region.subresource.arrayLayer != 0 || region.origin.z != 0 ||
        region.extent.depth != 1 || region.extent.width == 0 ||
        region.extent.height == 0) {
        return false;
    }
    const auto width =
        std::max(1u, texture.desc.extent.width >> region.subresource.mipLevel);
    const auto height =
        std::max(1u, texture.desc.extent.height >> region.subresource.mipLevel);
    return region.origin.x <= width &&
           region.extent.width <= width - region.origin.x &&
           region.origin.y <= height &&
           region.extent.height <= height - region.origin.y;
}

[[nodiscard]] std::size_t required_texture_bytes(
    const MetalTextureResource& texture, const TextureRegion& region,
    const TextureDataLayout& layout) {
    if (!metal_texture_region_valid(texture, region)) {
        return 0;
    }
    const auto tightRow = static_cast<std::size_t>(region.extent.width) *
                          texture.bytesPerPixel;
    const auto rowBytes = layout.bytesPerRow == 0 ? tightRow : layout.bytesPerRow;
    if (rowBytes < tightRow) {
        return 0;
    }
    return layout.offset +
           (static_cast<std::size_t>(region.extent.height) - 1u) * rowBytes +
           tightRow;
}

[[nodiscard]] Status write_metal_texture(
    const std::shared_ptr<void>& resource, const TextureRegion& region,
    std::span<const std::byte> data, const TextureDataLayout& layout) {
    const auto metal = std::static_pointer_cast<MetalTextureResource>(resource);
    if (!metal || metal->texture == nil ||
        metal->texture.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal textures require transfer upload");
    }
    const auto required = required_texture_bytes(*metal, region, layout);
    if (required == 0 || required > data.size()) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal texture write layout is invalid");
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    metal->bytesPerPixel
                              : layout.bytesPerRow;
    const auto nativeRegion = MTLRegionMake2D(
        region.origin.x, region.origin.y, region.extent.width,
        region.extent.height);
    [metal->texture replaceRegion:nativeRegion
                      mipmapLevel:region.subresource.mipLevel
                        withBytes:data.data() + layout.offset
                      bytesPerRow:rowBytes];
    return Status::success();
}

[[nodiscard]] Status read_metal_texture(
    const std::shared_ptr<void>& resource, const TextureRegion& region,
    std::span<std::byte> data, const TextureDataLayout& layout) {
    const auto metal = std::static_pointer_cast<MetalTextureResource>(resource);
    if (!metal || metal->texture == nil ||
        metal->texture.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal textures require transfer readback");
    }
    const auto required = required_texture_bytes(*metal, region, layout);
    if (required == 0 || required > data.size()) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal texture read layout is invalid");
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    metal->bytesPerPixel
                              : layout.bytesPerRow;
    const auto nativeRegion = MTLRegionMake2D(
        region.origin.x, region.origin.y, region.extent.width,
        region.extent.height);
    [metal->texture getBytes:data.data() + layout.offset
                 bytesPerRow:rowBytes
                  fromRegion:nativeRegion
                 mipmapLevel:region.subresource.mipLevel];
    return Status::success();
}

[[nodiscard]] MTLOrigin metal_origin(Origin3D origin) {
    return MTLOriginMake(origin.x, origin.y, origin.z);
}

[[nodiscard]] MTLSize metal_size(Extent3D extent) {
    return MTLSizeMake(extent.width, extent.height, extent.depth);
}

[[nodiscard]] Status encode_metal_transfer(id<MTLBlitCommandEncoder> encoder,
                                           const detail::NativeTransfer& transfer) {
    switch (transfer.kind) {
    case detail::NativeTransferKind::copy_buffer: {
        const auto source =
            std::static_pointer_cast<MetalBufferResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        [encoder copyFromBuffer:source->buffer
                   sourceOffset:transfer.buffer.sourceOffset
                       toBuffer:destination->buffer
              destinationOffset:transfer.buffer.destinationOffset
                           size:transfer.buffer.size];
        return Status::success();
    }
    case detail::NativeTransferKind::fill_buffer: {
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!destination) {
            break;
        }
        [encoder fillBuffer:destination->buffer
                      range:NSMakeRange(transfer.buffer.destinationOffset,
                                        transfer.buffer.size)
                      value:std::to_integer<std::uint8_t>(transfer.fillValue)];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_buffer_to_texture: {
        const auto source =
            std::static_pointer_cast<MetalBufferResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalTextureResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        const auto rowBytes = transfer.bufferTexture.layout.bytesPerRow == 0
                                  ? transfer.bufferTexture.texture.extent.width *
                                        destination->bytesPerPixel
                                  : transfer.bufferTexture.layout.bytesPerRow;
        const auto rows = transfer.bufferTexture.layout.rowsPerImage == 0
                              ? transfer.bufferTexture.texture.extent.height
                              : transfer.bufferTexture.layout.rowsPerImage;
        [encoder copyFromBuffer:source->buffer
                   sourceOffset:transfer.bufferTexture.bufferOffset +
                                transfer.bufferTexture.layout.offset
              sourceBytesPerRow:rowBytes
            sourceBytesPerImage:rowBytes * rows
                     sourceSize:metal_size(transfer.bufferTexture.texture.extent)
                      toTexture:destination->texture
               destinationSlice:transfer.bufferTexture.texture.subresource.arrayLayer
               destinationLevel:transfer.bufferTexture.texture.subresource.mipLevel
              destinationOrigin:metal_origin(transfer.bufferTexture.texture.origin)];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_texture_to_buffer: {
        const auto source =
            std::static_pointer_cast<MetalTextureResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        const auto rowBytes = transfer.bufferTexture.layout.bytesPerRow == 0
                                  ? transfer.bufferTexture.texture.extent.width *
                                        source->bytesPerPixel
                                  : transfer.bufferTexture.layout.bytesPerRow;
        const auto rows = transfer.bufferTexture.layout.rowsPerImage == 0
                              ? transfer.bufferTexture.texture.extent.height
                              : transfer.bufferTexture.layout.rowsPerImage;
        [encoder copyFromTexture:source->texture
                    sourceSlice:transfer.bufferTexture.texture.subresource.arrayLayer
                    sourceLevel:transfer.bufferTexture.texture.subresource.mipLevel
                   sourceOrigin:metal_origin(transfer.bufferTexture.texture.origin)
                     sourceSize:metal_size(transfer.bufferTexture.texture.extent)
                       toBuffer:destination->buffer
              destinationOffset:transfer.bufferTexture.bufferOffset +
                                transfer.bufferTexture.layout.offset
         destinationBytesPerRow:rowBytes
       destinationBytesPerImage:rowBytes * rows];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_texture: {
        const auto source =
            std::static_pointer_cast<MetalTextureResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalTextureResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        [encoder copyFromTexture:source->texture
                    sourceSlice:transfer.texture.source.subresource.arrayLayer
                    sourceLevel:transfer.texture.source.subresource.mipLevel
                   sourceOrigin:metal_origin(transfer.texture.source.origin)
                     sourceSize:metal_size(transfer.texture.source.extent)
                      toTexture:destination->texture
               destinationSlice:transfer.texture.destination.subresource.arrayLayer
               destinationLevel:transfer.texture.destination.subresource.mipLevel
              destinationOrigin:metal_origin(transfer.texture.destination.origin)];
        return Status::success();
    }
    case detail::NativeTransferKind::clear_texture:
    case detail::NativeTransferKind::resolve_texture:
    case detail::NativeTransferKind::blit_texture:
        return Status::failure(StatusCode::unsupported,
                               "this Metal texture transfer is unsupported");
    }
    return Status::failure(StatusCode::invalid_argument,
                           "Metal transfer resources are invalid");
}

[[nodiscard]] NSUInteger metal_binding_index(
    std::span<const ShaderBindingMap> mappings,
    const detail::NativeBindingResource& binding) {
    const auto found = std::find_if(
        mappings.begin(), mappings.end(), [&](const ShaderBindingMap& mapping) {
            return mapping.group == binding.group &&
                   mapping.binding == binding.binding &&
                   mapping.arrayElement == binding.arrayElement;
        });
    if (found != mappings.end()) {
        return static_cast<NSUInteger>(found->nativeBinding) +
               found->nativeArrayElement;
    }
    return static_cast<NSUInteger>(binding.group) * 8u + binding.binding +
           binding.arrayElement;
}

[[nodiscard]] Status encode_metal_render_bindings(
    id<MTLRenderCommandEncoder> encoder,
    const MetalPipelineResource& pipeline,
    std::span<const detail::NativeBindingResource> bindings) {
    for (const auto& binding : bindings) {
        const auto bind_stage = [&](ShaderStageMask stage,
                                    std::span<const ShaderBindingMap> mappings) {
            if (!has_stage(binding.visibility, stage)) {
                return Status::success();
            }
            const auto index = metal_binding_index(mappings, binding);
            switch (binding.type) {
            case BindingType::uniform_buffer:
            case BindingType::storage_buffer: {
                const auto resource =
                    std::static_pointer_cast<MetalBufferResource>(binding.resource);
                if (!resource || resource->buffer == nil) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "Metal bind-group buffer is invalid");
                }
                if (stage == ShaderStageMask::vertex) {
                    [encoder setVertexBuffer:resource->buffer
                                      offset:binding.offset
                                     atIndex:index];
                } else {
                    [encoder setFragmentBuffer:resource->buffer
                                        offset:binding.offset
                                       atIndex:index];
                }
                break;
            }
            case BindingType::sampled_texture:
            case BindingType::storage_texture: {
                const auto resource = std::static_pointer_cast<
                    MetalTextureViewResource>(binding.resource);
                if (!resource || resource->texture == nil) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "Metal bind-group texture is invalid");
                }
                if (stage == ShaderStageMask::vertex) {
                    [encoder setVertexTexture:resource->texture atIndex:index];
                } else {
                    [encoder setFragmentTexture:resource->texture atIndex:index];
                }
                break;
            }
            case BindingType::sampler: {
                const auto resource =
                    std::static_pointer_cast<MetalSamplerResource>(binding.resource);
                if (!resource || resource->sampler == nil) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "Metal bind-group sampler is invalid");
                }
                if (stage == ShaderStageMask::vertex) {
                    [encoder setVertexSamplerState:resource->sampler atIndex:index];
                } else {
                    [encoder setFragmentSamplerState:resource->sampler
                                             atIndex:index];
                }
                break;
            }
            }
            return Status::success();
        };
        if (auto status = bind_stage(ShaderStageMask::vertex,
                                     pipeline.vertexMap);
            !status.ok()) {
            return status;
        }
        if (auto status = bind_stage(ShaderStageMask::fragment,
                                     pipeline.fragmentMap);
            !status.ok()) {
            return status;
        }
    }
    return Status::success();
}

[[nodiscard]] Status encode_metal_compute_bindings(
    id<MTLComputeCommandEncoder> encoder,
    const MetalComputePipelineResource& pipeline,
    std::span<const detail::NativeBindingResource> bindings) {
    for (const auto& binding : bindings) {
        if (!has_stage(binding.visibility, ShaderStageMask::compute)) {
            continue;
        }
        const auto index = metal_binding_index(pipeline.bindingMap, binding);
        switch (binding.type) {
        case BindingType::uniform_buffer:
        case BindingType::storage_buffer: {
            const auto resource =
                std::static_pointer_cast<MetalBufferResource>(binding.resource);
            if (!resource || resource->buffer == nil) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal compute buffer binding is invalid");
            }
            [encoder setBuffer:resource->buffer
                        offset:binding.offset
                       atIndex:index];
            break;
        }
        case BindingType::sampled_texture:
        case BindingType::storage_texture: {
            const auto resource =
                std::static_pointer_cast<MetalTextureViewResource>(binding.resource);
            if (!resource || resource->texture == nil) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal compute texture binding is invalid");
            }
            [encoder setTexture:resource->texture atIndex:index];
            break;
        }
        case BindingType::sampler: {
            const auto resource =
                std::static_pointer_cast<MetalSamplerResource>(binding.resource);
            if (!resource || resource->sampler == nil) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal compute sampler binding is invalid");
            }
            [encoder setSamplerState:resource->sampler atIndex:index];
            break;
        }
        }
    }
    return Status::success();
}

[[nodiscard]] MTLRenderPassDescriptor* make_metal_render_pass(
    const detail::NativeCommand& command) {
    auto* descriptor = [[MTLRenderPassDescriptor alloc] init];
    for (std::size_t index = 0; index < command.colorAttachments.size(); ++index) {
        const auto& attachment = command.colorAttachments[index];
        const auto texture =
            std::static_pointer_cast<MetalTextureResource>(attachment.texture);
        if (!texture || texture->texture == nil) {
            [descriptor release];
            return nil;
        }
        auto* output = descriptor.colorAttachments[index];
        output.texture = texture->texture;
        output.loadAction = metal_load_action(attachment.loadOp);
        output.storeAction = metal_store_action(attachment.storeOp);
        output.clearColor = MTLClearColorMake(
            attachment.clear.r, attachment.clear.g, attachment.clear.b,
            attachment.clear.a);
        if (attachment.resolveTexture) {
            const auto resolve = std::static_pointer_cast<MetalTextureResource>(
                attachment.resolveTexture);
            if (!resolve || resolve->texture == nil) {
                [descriptor release];
                return nil;
            }
            output.resolveTexture = resolve->texture;
            output.storeAction = attachment.storeOp == StoreOp::store
                                     ? MTLStoreActionStoreAndMultisampleResolve
                                     : MTLStoreActionMultisampleResolve;
        }
    }
    if (command.depthStencilAttachment.texture) {
        const auto texture = std::static_pointer_cast<MetalTextureResource>(
            command.depthStencilAttachment.texture);
        if (!texture || texture->texture == nil) {
            [descriptor release];
            return nil;
        }
        const auto& attachment = command.depthStencilAttachment;
        descriptor.depthAttachment.texture = texture->texture;
        descriptor.depthAttachment.loadAction =
            metal_load_action(attachment.depthLoadOp);
        descriptor.depthAttachment.storeAction =
            metal_store_action(attachment.depthStoreOp);
        descriptor.depthAttachment.clearDepth = attachment.clearDepth;
        if (texture->desc.format == TextureFormat::depth32_float_stencil8) {
            descriptor.stencilAttachment.texture = texture->texture;
            descriptor.stencilAttachment.loadAction =
                metal_load_action(attachment.stencilLoadOp);
            descriptor.stencilAttachment.storeAction =
                metal_store_action(attachment.stencilStoreOp);
            descriptor.stencilAttachment.clearStencil =
                attachment.clearStencil;
        }
    }
    return descriptor;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_semaphore(
    const SemaphoreDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::device_lost,
                                   "Metal device is no longer available");
        }
        id<MTLSharedEvent> event = [device newSharedEvent];
        if (event == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal shared-event creation failed");
        }
        event.signaledValue = desc.initialValue;
        if (!desc.debugName.empty()) {
            event.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalSemaphoreResource>(event));
    }
}

[[nodiscard]] Status submit_metal_commands(
    const std::shared_ptr<void>&,
    std::span<const detail::NativeCommand> commands,
    std::span<const detail::NativeSemaphorePoint> waits,
    std::span<const detail::NativeSemaphorePoint> signals) {
    @autoreleasepool {
        if (detail::gMetalDeviceLossForTesting.load()) {
            return Status::failure(StatusCode::device_lost,
                                   "injected Metal device loss");
        }
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "Metal device is no longer available");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal command queue creation failed");
        }
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (command == nil) {
            [queue release];
            return metal_failure(StatusCode::backend_error,
                                 "Metal command buffer creation failed");
        }
        for (const auto& wait : waits) {
            const auto semaphore =
                std::static_pointer_cast<MetalSemaphoreResource>(wait.semaphore);
            if (!semaphore || semaphore->event == nil) {
                [queue release];
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal wait semaphore is invalid");
            }
            [command encodeWaitForEvent:semaphore->event value:wait.value];
        }

        id<MTLRenderCommandEncoder> render = nil;
        id<MTLComputeCommandEncoder> compute = nil;
        std::shared_ptr<MetalPipelineResource> graphicsPipeline;
        std::shared_ptr<MetalComputePipelineResource> computePipeline;
        std::shared_ptr<MetalBufferResource> indexBuffer;
        NSUInteger indexBufferOffset = 0;
        MTLIndexType indexType = MTLIndexTypeUInt32;
        Extent2D renderExtent;
        std::vector<MTLViewport> viewports;
        std::vector<MTLScissorRect> scissors;
        std::array<std::byte, 256> renderPushConstants{};
        std::array<std::byte, 256> computePushConstants{};
        std::size_t renderPushConstantBytes = 0;
        std::size_t computePushConstantBytes = 0;

        const auto fail = [&](Status status) {
            if (render != nil) {
                [render endEncoding];
                render = nil;
            }
            if (compute != nil) {
                [compute endEncoding];
                compute = nil;
            }
            [queue release];
            return status;
        };

        for (const auto& encoded : commands) {
            switch (encoded.kind) {
            case detail::NativeCommandKind::transfer: {
                if (render != nil || compute != nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal transfer commands cannot overlap an encoder"));
                }
                id<MTLBlitCommandEncoder> blit =
                    [command blitCommandEncoder];
                if (blit == nil) {
                    return fail(metal_failure(
                        StatusCode::backend_error,
                        "Metal blit encoder creation failed"));
                }
                const auto status = encode_metal_transfer(blit, encoded.transfer);
                [blit endEncoding];
                if (!status.ok()) {
                    return fail(status);
                }
                break;
            }
            case detail::NativeCommandKind::barrier:
                if (render != nil || compute != nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal barriers require an encoder boundary"));
                }
                break;
            case detail::NativeCommandKind::begin_render: {
                if (render != nil || compute != nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal command encoders overlap"));
                }
                auto* pass = make_metal_render_pass(encoded);
                if (pass == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal render-pass attachments are invalid"));
                }
                render = [command renderCommandEncoderWithDescriptor:pass];
                [pass release];
                if (render == nil) {
                    return fail(metal_failure(
                        StatusCode::backend_error,
                        "Metal render command encoder creation failed"));
                }
                renderExtent = encoded.extent;
                graphicsPipeline.reset();
                indexBuffer.reset();
                viewports.clear();
                scissors.clear();
                renderPushConstants.fill(std::byte{});
                renderPushConstantBytes = 0;
                break;
            }
            case detail::NativeCommandKind::end_render:
                if (render == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal has no active render encoder"));
                }
                [render endEncoding];
                render = nil;
                graphicsPipeline.reset();
                break;
            case detail::NativeCommandKind::begin_compute:
                if (render != nil || compute != nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal command encoders overlap"));
                }
                compute = [command computeCommandEncoder];
                if (compute == nil) {
                    return fail(metal_failure(
                        StatusCode::backend_error,
                        "Metal compute command encoder creation failed"));
                }
                computePipeline.reset();
                computePushConstants.fill(std::byte{});
                computePushConstantBytes = 0;
                break;
            case detail::NativeCommandKind::end_compute:
                if (compute == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal has no active compute encoder"));
                }
                [compute endEncoding];
                compute = nil;
                computePipeline.reset();
                break;
            case detail::NativeCommandKind::bind_graphics_pipeline: {
                graphicsPipeline = std::static_pointer_cast<MetalPipelineResource>(
                    encoded.object);
                if (render == nil || !graphicsPipeline ||
                    graphicsPipeline->pipeline == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal graphics pipeline binding is invalid"));
                }
                [render setRenderPipelineState:graphicsPipeline->pipeline];
                [render setDepthStencilState:graphicsPipeline->depthStencil];
                switch (graphicsPipeline->desc.rasterization.cullMode) {
                case CullMode::none:
                    [render setCullMode:MTLCullModeNone];
                    break;
                case CullMode::front:
                    [render setCullMode:MTLCullModeFront];
                    break;
                case CullMode::back:
                    [render setCullMode:MTLCullModeBack];
                    break;
                }
                [render setFrontFacingWinding:
                            graphicsPipeline->desc.rasterization.frontFace ==
                                    FrontFace::clockwise
                                ? MTLWindingClockwise
                                : MTLWindingCounterClockwise];
                [render setTriangleFillMode:
                            graphicsPipeline->desc.rasterization.polygonMode ==
                                    PolygonMode::line
                                ? MTLTriangleFillModeLines
                                : MTLTriangleFillModeFill];
                [render setDepthClipMode:
                            graphicsPipeline->desc.rasterization
                                    .depthClampEnabled
                                ? MTLDepthClipModeClamp
                                : MTLDepthClipModeClip];
                [render setDepthBias:
                            graphicsPipeline->desc.rasterization.depthBias
                      slopeScale:graphicsPipeline->desc.rasterization
                                     .depthBiasSlopeScale
                           clamp:graphicsPipeline->desc.rasterization
                                     .depthBiasClamp];
                [render
                    setBlendColorRed:graphicsPipeline->desc.blendConstant[0]
                              green:graphicsPipeline->desc.blendConstant[1]
                               blue:graphicsPipeline->desc.blendConstant[2]
                              alpha:graphicsPipeline->desc.blendConstant[3]];
                [render setStencilReferenceValue:
                            graphicsPipeline->desc.stencilReference];
                if (!graphicsPipeline->desc.viewports.empty()) {
                    viewports.clear();
                    for (const auto& viewport :
                         graphicsPipeline->desc.viewports) {
                        viewports.push_back({viewport.x, viewport.y,
                                             viewport.width, viewport.height,
                                             viewport.minimumDepth,
                                             viewport.maximumDepth});
                    }
                    [render setViewports:viewports.data()
                                   count:viewports.size()];
                } else if (!has_dynamic_state(
                               graphicsPipeline->desc.dynamicState,
                               DynamicState::viewport)) {
                    const MTLViewport viewport{0.0, 0.0,
                                               static_cast<double>(renderExtent.width),
                                               static_cast<double>(renderExtent.height),
                                               0.0, 1.0};
                    [render setViewport:viewport];
                }
                if (!graphicsPipeline->desc.scissors.empty()) {
                    scissors.clear();
                    for (const auto& scissor :
                         graphicsPipeline->desc.scissors) {
                        scissors.push_back(
                            {static_cast<NSUInteger>(scissor.x),
                             static_cast<NSUInteger>(scissor.y), scissor.width,
                             scissor.height});
                    }
                    [render setScissorRects:scissors.data()
                                      count:scissors.size()];
                } else if (!has_dynamic_state(
                               graphicsPipeline->desc.dynamicState,
                               DynamicState::scissor)) {
                    const MTLScissorRect scissor{0, 0, renderExtent.width,
                                                  renderExtent.height};
                    [render setScissorRect:scissor];
                }
                break;
            }
            case detail::NativeCommandKind::bind_compute_pipeline:
                computePipeline =
                    std::static_pointer_cast<MetalComputePipelineResource>(
                        encoded.object);
                if (compute == nil || !computePipeline ||
                    computePipeline->pipeline == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal compute pipeline binding is invalid"));
                }
                [compute setComputePipelineState:computePipeline->pipeline];
                break;
            case detail::NativeCommandKind::bind_vertex_buffer:
            case detail::NativeCommandKind::bind_uniform_buffer:
            case detail::NativeCommandKind::bind_storage_buffer: {
                const auto buffer =
                    std::static_pointer_cast<MetalBufferResource>(encoded.object);
                if (!buffer || buffer->buffer == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal direct buffer binding is invalid"));
                }
                if (encoded.kind ==
                    detail::NativeCommandKind::bind_vertex_buffer) {
                    [render setVertexBuffer:buffer->buffer
                                     offset:encoded.arguments[1]
                                    atIndex:encoded.arguments[0]];
                } else if (encoded.kind ==
                           detail::NativeCommandKind::bind_uniform_buffer) {
                    [render setVertexBuffer:buffer->buffer
                                     offset:encoded.arguments[1]
                                    atIndex:encoded.arguments[0]];
                    [render setFragmentBuffer:buffer->buffer
                                       offset:encoded.arguments[1]
                                      atIndex:encoded.arguments[0]];
                } else {
                    [compute setBuffer:buffer->buffer
                                offset:encoded.arguments[1]
                               atIndex:encoded.arguments[0]];
                }
                break;
            }
            case detail::NativeCommandKind::bind_index_buffer:
                indexBuffer =
                    std::static_pointer_cast<MetalBufferResource>(encoded.object);
                if (!indexBuffer || indexBuffer->buffer == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal index buffer binding is invalid"));
                }
                indexBufferOffset = encoded.arguments[0];
                indexType = encoded.arguments[1] ==
                                    static_cast<std::uint64_t>(IndexFormat::uint16)
                                ? MTLIndexTypeUInt16
                                : MTLIndexTypeUInt32;
                break;
            case detail::NativeCommandKind::bind_group:
                if (render != nil && graphicsPipeline) {
                    if (auto status = encode_metal_render_bindings(
                            render, *graphicsPipeline, encoded.bindings);
                        !status.ok()) {
                        return fail(status);
                    }
                } else if (compute != nil && computePipeline) {
                    if (auto status = encode_metal_compute_bindings(
                            compute, *computePipeline, encoded.bindings);
                        !status.ok()) {
                        return fail(status);
                    }
                } else {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal bind group requires a pipeline"));
                }
                break;
            case detail::NativeCommandKind::push_constants: {
                const auto offset =
                    static_cast<std::size_t>(encoded.arguments[1]);
                if (offset > renderPushConstants.size() ||
                    encoded.bytes.size() >
                        renderPushConstants.size() - offset) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal push constants exceed the reserved range"));
                }
                const auto stages =
                    static_cast<ShaderStageMask>(encoded.arguments[0]);
                if (render != nil) {
                    std::copy(encoded.bytes.begin(), encoded.bytes.end(),
                              renderPushConstants.begin() +
                                  static_cast<std::ptrdiff_t>(offset));
                    renderPushConstantBytes = std::max(
                        renderPushConstantBytes, offset + encoded.bytes.size());
                    if (has_stage(stages, ShaderStageMask::vertex)) {
                        [render setVertexBytes:renderPushConstants.data()
                                       length:renderPushConstantBytes
                                      atIndex:30];
                    }
                    if (has_stage(stages, ShaderStageMask::fragment)) {
                        [render setFragmentBytes:renderPushConstants.data()
                                         length:renderPushConstantBytes
                                        atIndex:30];
                    }
                } else if (compute != nil) {
                    std::copy(encoded.bytes.begin(), encoded.bytes.end(),
                              computePushConstants.begin() +
                                  static_cast<std::ptrdiff_t>(offset));
                    computePushConstantBytes = std::max(
                        computePushConstantBytes, offset + encoded.bytes.size());
                    [compute setBytes:computePushConstants.data()
                                length:computePushConstantBytes
                               atIndex:30];
                }
                break;
            }
            case detail::NativeCommandKind::set_viewports: {
                const auto first =
                    static_cast<std::size_t>(encoded.arguments[0]);
                if (viewports.size() < first + encoded.viewports.size()) {
                    viewports.resize(first + encoded.viewports.size());
                }
                for (std::size_t index = 0; index < encoded.viewports.size();
                     ++index) {
                    const auto& viewport = encoded.viewports[index];
                    viewports[first + index] =
                        {viewport.x, viewport.y, viewport.width, viewport.height,
                         viewport.minimumDepth, viewport.maximumDepth};
                }
                [render setViewports:viewports.data() count:viewports.size()];
                break;
            }
            case detail::NativeCommandKind::set_scissors: {
                const auto first =
                    static_cast<std::size_t>(encoded.arguments[0]);
                if (scissors.size() < first + encoded.scissors.size()) {
                    scissors.resize(first + encoded.scissors.size());
                }
                for (std::size_t index = 0; index < encoded.scissors.size();
                     ++index) {
                    const auto& scissor = encoded.scissors[index];
                    scissors[first + index] =
                        {static_cast<NSUInteger>(scissor.x),
                         static_cast<NSUInteger>(scissor.y), scissor.width,
                         scissor.height};
                }
                [render setScissorRects:scissors.data() count:scissors.size()];
                break;
            }
            case detail::NativeCommandKind::set_blend_constant: {
                if (render == nil || encoded.bytes.size() != 4 * sizeof(float)) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal blend-constant command is invalid"));
                }
                std::array<float, 4> color{};
                std::memcpy(color.data(), encoded.bytes.data(),
                            encoded.bytes.size());
                [render setBlendColorRed:color[0]
                                   green:color[1]
                                    blue:color[2]
                                   alpha:color[3]];
                break;
            }
            case detail::NativeCommandKind::set_stencil_reference:
                if (render == nil) {
                    return fail(Status::failure(
                        StatusCode::invalid_state,
                        "Metal stencil-reference command has no encoder"));
                }
                [render setStencilReferenceValue:encoded.arguments[0]];
                break;
            case detail::NativeCommandKind::set_depth_bias: {
                if (render == nil || encoded.bytes.size() != 3 * sizeof(float)) {
                    return fail(Status::failure(
                        StatusCode::invalid_argument,
                        "Metal depth-bias command is invalid"));
                }
                std::array<float, 3> values{};
                std::memcpy(values.data(), encoded.bytes.data(),
                            encoded.bytes.size());
                [render setDepthBias:values[0]
                          slopeScale:values[1]
                               clamp:values[2]];
                break;
            }
            case detail::NativeCommandKind::draw:
                [render drawPrimitives:metal_primitive_type(
                                           graphicsPipeline->desc.topology)
                              vertexStart:encoded.arguments[2]
                              vertexCount:encoded.arguments[0]
                            instanceCount:encoded.arguments[1]
                             baseInstance:encoded.arguments[3]];
                break;
            case detail::NativeCommandKind::draw_indexed: {
                const auto indexSize =
                    indexType == MTLIndexTypeUInt16 ? NSUInteger{2}
                                                    : NSUInteger{4};
                [render
                    drawIndexedPrimitives:metal_primitive_type(
                                              graphicsPipeline->desc.topology)
                               indexCount:encoded.arguments[0]
                                indexType:indexType
                              indexBuffer:indexBuffer->buffer
                        indexBufferOffset:indexBufferOffset +
                                          encoded.arguments[2] * indexSize
                            instanceCount:encoded.arguments[1]
                               baseVertex:static_cast<std::int32_t>(
                                              encoded.arguments[3])
                             baseInstance:encoded.arguments[4]];
                break;
            }
            case detail::NativeCommandKind::draw_indirect: {
                const auto indirect =
                    std::static_pointer_cast<MetalBufferResource>(encoded.object);
                const auto indexed = encoded.arguments[1] != 0;
                for (std::uint64_t drawIndex = 0;
                     drawIndex < encoded.arguments[2]; ++drawIndex) {
                    const auto offset = encoded.arguments[0] +
                                        drawIndex * encoded.arguments[3];
                    if (indexed) {
                        [render
                            drawIndexedPrimitives:metal_primitive_type(
                                                      graphicsPipeline->desc.topology)
                                       indexType:indexType
                                     indexBuffer:indexBuffer->buffer
                               indexBufferOffset:indexBufferOffset
                                    indirectBuffer:indirect->buffer
                              indirectBufferOffset:offset];
                    } else {
                        [render drawPrimitives:metal_primitive_type(
                                                   graphicsPipeline->desc.topology)
                                    indirectBuffer:indirect->buffer
                              indirectBufferOffset:offset];
                    }
                }
                break;
            }
            case detail::NativeCommandKind::draw_indirect_count:
                return fail(Status::failure(
                    StatusCode::unsupported,
                    "Metal indirect-count drawing is not exposed"));
            case detail::NativeCommandKind::dispatch: {
                const auto threads = MTLSizeMake(
                    computePipeline->desc.requiredWorkgroupSize.width,
                    computePipeline->desc.requiredWorkgroupSize.height,
                    computePipeline->desc.requiredWorkgroupSize.depth);
                [compute dispatchThreadgroups:MTLSizeMake(
                                                  encoded.arguments[0],
                                                  encoded.arguments[1],
                                                  encoded.arguments[2])
                      threadsPerThreadgroup:threads];
                break;
            }
            case detail::NativeCommandKind::dispatch_indirect: {
                const auto indirect =
                    std::static_pointer_cast<MetalBufferResource>(encoded.object);
                const auto threads = MTLSizeMake(
                    computePipeline->desc.requiredWorkgroupSize.width,
                    computePipeline->desc.requiredWorkgroupSize.height,
                    computePipeline->desc.requiredWorkgroupSize.depth);
                [compute dispatchThreadgroupsWithIndirectBuffer:indirect->buffer
                                            indirectBufferOffset:encoded.arguments[0]
                                           threadsPerThreadgroup:threads];
                break;
            }
            }
        }
        if (render != nil || compute != nil) {
            return fail(Status::failure(StatusCode::invalid_state,
                                        "Metal command encoder was not ended"));
        }
        for (const auto& signal : signals) {
            const auto semaphore = std::static_pointer_cast<MetalSemaphoreResource>(
                signal.semaphore);
            if (!semaphore || semaphore->event == nil) {
                [queue release];
                return Status::failure(StatusCode::invalid_argument,
                                       "Metal signal semaphore is invalid");
            }
            [command encodeSignalEvent:semaphore->event value:signal.value];
        }
        [command commit];
        [command waitUntilCompleted];
        const auto commandStatus = command.status;
        const auto error = command.error;
        [queue release];
        if (commandStatus == MTLCommandBufferStatusError) {
            const auto message = error.localizedDescription != nil
                                     ? std::string{error.localizedDescription.UTF8String}
                                     : std::string{"Metal command submission failed"};
            return metal_failure(metal_command_status_code(error), message,
                                 error.code);
        }
    }
    return Status::success();
}

} // namespace

Result<Instance> create_metal_instance(const InstanceDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        const auto* name = device.name.UTF8String;
        const auto deviceBudget =
            static_cast<std::size_t>(device.recommendedMaxWorkingSetSize);
        return detail::create_foundation_instance(
            desc,
            {
                .kind = BackendKind::metal,
                .platform = PlatformKind::macos,
                .maturity = BackendMaturity::native_smoke,
                .adapterName = name != nullptr ? name : "Metal adapter",
                .queueKinds = {QueueKind::graphics, QueueKind::compute,
                               QueueKind::transfer},
                .supportedFeatures = {
                    Feature::presentation, Feature::compute, Feature::transfer,
                    Feature::memory_budget,
                    Feature::descriptor_arrays, Feature::dynamic_offsets,
                    Feature::push_constants},
                .resourceCapabilities = {
                    .bufferViews = true,
                    .textureViews = true,
                    .hostCoherent = true,
                    .bufferCopy = true,
                    .bufferFill = true,
                    .bufferTextureCopy = true,
                    .textureCopy = true,
                    .textureClear = false,
                    .textureResolve = false,
                    .textureBlitNearest = false,
                    .textureBlitLinear = false,
                    .externalImport = false,
                    .externalExport = false,
                },
                .bindingCapabilities = {
                    .ordinaryBindGroups = true,
                    .descriptorArrays = true,
                    .dynamicOffsets = true,
                    .immutableSamplers = true,
                    .pushConstants = true,
                    .bindlessTables = false,
                    .updateAfterBind = false,
                    .maxBindGroups = 2,
                    .maxBindingsPerGroup = 8,
                    .maxDescriptorsPerGroup = 8,
                    .maxPushConstantBytes = 256,
                    .minUniformBufferOffsetAlignment = 256,
                    .minStorageBufferOffsetAlignment = 16,
                },
                .pipelineCapabilities = {
                    .graphics = true,
                    .compute = true,
                    .multipleRenderTargets = true,
                    .depthStencil = true,
                    .multisample = true,
                    .tessellation = false,
                    .indirect = true,
                    .indirectCount = false,
                    .pipelineCache = false,
                    .maxColorAttachments = 8,
                    .maxVertexBuffers = 16,
                    .maxViewports = 16,
                    .maxComputeWorkgroupSize = {1024, 1024, 64},
                    .maxComputeInvocations = 1024,
                },
                .uploadBudgetBytes = 512u * 1024u * 1024u,
                .readbackBudgetBytes = 512u * 1024u * 1024u,
                .deviceLocalBudgetBytes =
                    deviceBudget != 0 ? deviceBudget : 1024u * 1024u * 1024u,
                .native = true,
                .validationOnly = false,
                .presentation = true,
                .logicalResources = false,
                .nativeContext = {},
                .createBuffer = &create_metal_buffer,
                .mapBuffer = &map_metal_buffer,
                .unmapBuffer = &unmap_metal_buffer,
                .flushBuffer = &flush_metal_buffer,
                .invalidateBuffer = &invalidate_metal_buffer,
                .writeBuffer = &write_metal_buffer,
                .readBuffer = &read_metal_buffer,
                .createTexture = &create_metal_texture,
                .createTextureView = &create_metal_texture_view,
                .writeTexture = &write_metal_texture,
                .readTexture = &read_metal_texture,
                .createSampler = &create_metal_sampler,
                .createShader = &create_metal_shader,
                .createPipeline = &create_metal_pipeline,
                .createComputePipeline = &create_metal_compute_pipeline,
                .createSemaphore = &create_metal_semaphore,
                .createSurface = &create_metal_surface,
                .createSwapchain = &create_metal_swapchain,
                .acquireSwapchain = &acquire_metal_swapchain,
                .resizeSwapchain = &resize_metal_swapchain,
                .presentSwapchain = &present_metal_swapchain,
                .nativeSubmit = &submit_metal_commands,
            });
    }
}

} // namespace truffle::rhi
