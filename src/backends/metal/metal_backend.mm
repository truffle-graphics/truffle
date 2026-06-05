// Metal backend — Phase 3B/3C/3D implementation
// Phase 3C/3D notes:
//   - Pipeline colour attachment pixel format driven by PipelineDesc::colorFormat.
//   - Queue::submit() uses async MTLCommandBuffer completion handler + dispatch_semaphore.
//   - All buffers use MTLResourceStorageModeShared (CPU+GPU visible).
//   - No multi-threading safety beyond what Metal's own APIs guarantee.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>

#include "truffle/rhi/metal_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/rhi/validation.hpp"

#include "../backend_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi {
namespace {

using core::Result;
using core::Status;
using core::StatusCode;

[[nodiscard]] Capabilities make_metal_capabilities(id<MTLDevice> device) {
    const bool unifiedMemory = [device hasUnifiedMemory];

    Capabilities caps;
    caps.presentation = true;
    caps.validation = true;
    caps.maxFramesInFlight = 3;
    caps.queues = {.graphics = true, .compute = true, .transfer = true};
    caps.features = {
        .headlessSurface = true,
        .nativeSurface = true,
        .presentation = true,
        .compute = true,
        .indirectDraw = true,
        .shaderReflection = true,
        .debugLabels = true,
        .validation = true,
        .unifiedMemory = unifiedMemory,
    };
    caps.limits = {
        .maxTextureDimension2D = 16384,
        .maxBufferSize = static_cast<std::size_t>([device maxBufferLength]),
        .minUniformBufferOffsetAlignment = 256,
        .minStorageBufferOffsetAlignment = 16,
        .maxColorAttachments = 8,
        .maxVertexBuffers = 31,
        .maxSamplerAnisotropy = 16,
    };
    caps.formats = {
        {.format = TextureFormat::rgba8_unorm,
         .sampled = true,
         .colorAttachment = true,
         .storageTexture = true,
         .transferSource = true,
         .transferDestination = true},
        {.format = TextureFormat::bgra8_unorm,
         .sampled = true,
         .colorAttachment = true,
         .transferSource = true,
         .transferDestination = true},
        {.format = TextureFormat::depth32_float,
         .sampled = true,
         .depthStencilAttachment = true,
         .transferSource = true,
         .transferDestination = true},
    };
    caps.memoryHeaps = {
        {.kind = unifiedMemory ? MemoryHeapKind::unified : MemoryHeapKind::device_local,
         .budgetBytes = 0,
         .dedicated = !unifiedMemory},
    };
    caps.presentModes = {
        PresentMode::immediate,
        PresentMode::fifo,
        PresentMode::mailbox,
    };
    caps.surfaceKinds = {
        NativeSurfaceKind::headless,
        NativeSurfaceKind::cocoa_layer,
    };
    caps.shaderFormats = {
        ShaderByteFormat::msl_source,
    };
    return caps;
}

// ---------------------------------------------------------------------------
// Format / enum conversions
// ---------------------------------------------------------------------------

static MTLPixelFormat to_mtl_format(TextureFormat fmt) noexcept {
    switch (fmt) {
        case TextureFormat::rgba8_unorm:   return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::bgra8_unorm:   return MTLPixelFormatBGRA8Unorm;
        case TextureFormat::depth32_float: return MTLPixelFormatDepth32Float;
    }
}

static MTLLoadAction to_mtl_load(LoadOp op) noexcept {
    switch (op) {
        case LoadOp::load:      return MTLLoadActionLoad;
        case LoadOp::clear:     return MTLLoadActionClear;
        case LoadOp::dont_care: return MTLLoadActionDontCare;
    }
}

static MTLStoreAction to_mtl_store(StoreOp op) noexcept {
    switch (op) {
        case StoreOp::store:     return MTLStoreActionStore;
        case StoreOp::dont_care: return MTLStoreActionDontCare;
    }
}

[[nodiscard]] MTLSamplerMinMagFilter to_metal_filter(SamplerFilter filter) noexcept {
    return filter == SamplerFilter::linear ? MTLSamplerMinMagFilterLinear
                                           : MTLSamplerMinMagFilterNearest;
}

[[nodiscard]] MTLSamplerMipFilter to_metal_mipmap_mode(
    SamplerMipmapMode mode) noexcept {
    return mode == SamplerMipmapMode::linear ? MTLSamplerMipFilterLinear
                                             : MTLSamplerMipFilterNearest;
}

[[nodiscard]] MTLSamplerAddressMode to_metal_address_mode(
    SamplerAddressMode mode) noexcept {
    switch (mode) {
    case SamplerAddressMode::repeat:
        return MTLSamplerAddressModeRepeat;
    case SamplerAddressMode::mirrored_repeat:
        return MTLSamplerAddressModeMirrorRepeat;
    case SamplerAddressMode::clamp_to_edge:
        return MTLSamplerAddressModeClampToEdge;
    case SamplerAddressMode::clamp_to_border:
        return MTLSamplerAddressModeClampToBorderColor;
    }
    return MTLSamplerAddressModeClampToEdge;
}

[[nodiscard]] MTLCompareFunction to_metal_compare_function(
    SamplerCompareOp op) noexcept {
    switch (op) {
    case SamplerCompareOp::never:
        return MTLCompareFunctionNever;
    case SamplerCompareOp::less:
        return MTLCompareFunctionLess;
    case SamplerCompareOp::equal:
        return MTLCompareFunctionEqual;
    case SamplerCompareOp::less_equal:
        return MTLCompareFunctionLessEqual;
    case SamplerCompareOp::greater:
        return MTLCompareFunctionGreater;
    case SamplerCompareOp::not_equal:
        return MTLCompareFunctionNotEqual;
    case SamplerCompareOp::greater_equal:
        return MTLCompareFunctionGreaterEqual;
    case SamplerCompareOp::always:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

[[nodiscard]] MTLBlendFactor to_metal_blend_factor(
    BlendFactor factor) noexcept {
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

[[nodiscard]] MTLBlendOperation to_metal_blend_op(BlendOp op) noexcept {
    switch (op) {
    case BlendOp::add:
        return MTLBlendOperationAdd;
    case BlendOp::subtract:
        return MTLBlendOperationSubtract;
    case BlendOp::reverse_subtract:
        return MTLBlendOperationReverseSubtract;
    case BlendOp::min:
        return MTLBlendOperationMin;
    case BlendOp::max:
        return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

[[nodiscard]] MTLColorWriteMask to_metal_color_write_mask(
    ColorWriteFlags mask) noexcept {
    MTLColorWriteMask result = 0;
    if (has_flag(mask, ColorWriteFlags::red)) {
        result |= MTLColorWriteMaskRed;
    }
    if (has_flag(mask, ColorWriteFlags::green)) {
        result |= MTLColorWriteMaskGreen;
    }
    if (has_flag(mask, ColorWriteFlags::blue)) {
        result |= MTLColorWriteMaskBlue;
    }
    if (has_flag(mask, ColorWriteFlags::alpha)) {
        result |= MTLColorWriteMaskAlpha;
    }
    return result;
}

[[nodiscard]] MTLCullMode to_metal_cull_mode(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none:
        return MTLCullModeNone;
    case CullMode::front:
        return MTLCullModeFront;
    case CullMode::back:
        return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

[[nodiscard]] MTLWinding to_metal_winding(FrontFace face) noexcept {
    return face == FrontFace::clockwise ? MTLWindingClockwise
                                        : MTLWindingCounterClockwise;
}

[[nodiscard]] MTLTriangleFillMode to_metal_fill_mode(FillMode mode) noexcept {
    return mode == FillMode::wireframe ? MTLTriangleFillModeLines
                                       : MTLTriangleFillModeFill;
}

[[nodiscard]] MTLSamplerBorderColor to_metal_border_color(
    SamplerBorderColor color) noexcept {
    switch (color) {
    case SamplerBorderColor::transparent_black:
        return MTLSamplerBorderColorTransparentBlack;
    case SamplerBorderColor::opaque_black:
        return MTLSamplerBorderColorOpaqueBlack;
    case SamplerBorderColor::opaque_white:
        return MTLSamplerBorderColorOpaqueWhite;
    }
    return MTLSamplerBorderColorOpaqueBlack;
}

static MTLPrimitiveType to_mtl_primitive(PrimitiveTopology topo) noexcept {
    switch (topo) {
        case PrimitiveTopology::triangle_list:  return MTLPrimitiveTypeTriangle;
        case PrimitiveTopology::triangle_strip: return MTLPrimitiveTypeTriangleStrip;
        case PrimitiveTopology::line_list:      return MTLPrimitiveTypeLine;
        case PrimitiveTopology::point_list:     return MTLPrimitiveTypePoint;
    }
}

static MTLIndexType to_mtl_index_type(IndexFormat fmt) noexcept {
    switch (fmt) {
        case IndexFormat::uint16: return MTLIndexTypeUInt16;
        case IndexFormat::uint32: return MTLIndexTypeUInt32;
    }
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

class MetalDevice;

class MetalBuffer final : public IBuffer {
public:
    // Allocate a new MTLBuffer.
    MetalBuffer(id<MTLDevice> device, const BufferDesc& desc) : desc_(desc) {
        buf_ = [device newBufferWithLength:std::max(desc.size, std::size_t{1})
                                   options:MTLResourceStorageModeShared];
        if (buf_ && desc.size != 0) {
            std::memset([buf_ contents], 0, desc.size);
        }
    }
    // Wrap an existing MTLBuffer (used by upload ring frames).
    MetalBuffer(id<MTLBuffer> buf, const BufferDesc& desc) : desc_(desc), buf_(buf) {}

    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const BufferDesc& desc() const noexcept override { return desc_; }
    bool              valid() const noexcept { return buf_ != nil; }
    id<MTLBuffer>     native() const noexcept { return buf_; }

private:
    BufferDesc    desc_;
    id<MTLBuffer> buf_ = nil;
};

class MetalTexture final : public ITexture {
public:
    // Allocate a new offscreen MTLTexture.
    MetalTexture(id<MTLDevice> device, const TextureDesc& desc) : desc_(desc) {
        auto* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:to_mtl_format(desc.format)
                                         width:desc.extent.width
                                        height:desc.extent.height
                                     mipmapped:NO];
        const auto usage = effective_texture_usage(desc);
        td.usage = MTLTextureUsageUnknown;
        if (has_flag(usage, TextureUsageFlags::sampled)) {
            td.usage |= MTLTextureUsageShaderRead;
        }
        if (has_flag(usage, TextureUsageFlags::storage)) {
            td.usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        }
        if (has_flag(usage, TextureUsageFlags::color_attachment) ||
            has_flag(usage, TextureUsageFlags::depth_stencil)) {
            td.usage |= MTLTextureUsageRenderTarget;
        }
        if (td.usage == MTLTextureUsageUnknown) {
            td.usage = MTLTextureUsageShaderRead;
        }
        td.storageMode = MTLStorageModePrivate;
        tex_ = [device newTextureWithDescriptor:td];
    }
    // Wrap an existing MTLTexture (swapchain drawable, etc.).
    MetalTexture(id<MTLTexture> tex, const TextureDesc& desc) : desc_(desc), tex_(tex) {}

    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const TextureDesc& desc() const noexcept override { return desc_; }
    id<MTLTexture>     native() const noexcept { return tex_; }

private:
    TextureDesc    desc_;
    id<MTLTexture> tex_ = nil;
};

class MetalSampler final : public ISampler {
public:
    MetalSampler(id<MTLDevice> device, SamplerDesc desc) : desc_(std::move(desc)) {
        auto* sd = [MTLSamplerDescriptor new];
        sd.minFilter = to_metal_filter(effective_min_filter(desc_));
        sd.magFilter = to_metal_filter(effective_mag_filter(desc_));
        sd.mipFilter = to_metal_mipmap_mode(effective_mipmap_mode(desc_));
        sd.sAddressMode = to_metal_address_mode(desc_.addressModeU);
        sd.tAddressMode = to_metal_address_mode(desc_.addressModeV);
        sd.rAddressMode = to_metal_address_mode(desc_.addressModeW);
        sd.lodMinClamp = desc_.minLod;
        sd.lodMaxClamp = desc_.maxLod;
        sd.maxAnisotropy = desc_.maxAnisotropy;
        sd.compareFunction = desc_.compareEnabled
            ? to_metal_compare_function(desc_.compareOp)
            : MTLCompareFunctionNever;
        sd.borderColor = to_metal_border_color(desc_.borderColor);
        sampler_ = [device newSamplerStateWithDescriptor:sd];
    }
    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const SamplerDesc& desc() const noexcept override { return desc_; }
    id<MTLSamplerState> native() const noexcept { return sampler_; }

private:
    SamplerDesc desc_;
    id<MTLSamplerState> sampler_ = nil;
};

class MetalBindGroupLayout final : public IBindGroupLayout {
public:
    explicit MetalBindGroupLayout(BindGroupLayoutDesc desc)
        : desc_(std::move(desc)) {}

    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }

    const BindGroupLayoutDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupLayoutDesc desc_;
};

class MetalBindGroup final : public IBindGroup {
public:
    explicit MetalBindGroup(BindGroupDesc desc) : desc_(std::move(desc)) {}

    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }

    const BindGroupDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupDesc desc_;
};


class MetalPipelineReflection final : public IPipelineReflection {
public:
    MetalPipelineReflection() = default;

    void add_bindings(NSArray<id<MTLBinding>>* bindings, ShaderStage stage) {
        for (id<MTLBinding> b in bindings) {
            ResourceBinding rb;
            rb.name = b.name ? b.name.UTF8String : "unknown";
            rb.stage = stage;
            rb.bindingIndex = b.index;
            
            if (b.type == MTLBindingTypeBuffer) {
                rb.type = ResourceBindingType::Buffer;
                if ([b conformsToProtocol:@protocol(MTLBufferBinding)]) {
                    rb.dataSize = ((id<MTLBufferBinding>)b).bufferDataSize;
                }
            } else if (b.type == MTLBindingTypeTexture) {
                rb.type = ResourceBindingType::Texture;
            } else if (b.type == MTLBindingTypeSampler) {
                rb.type = ResourceBindingType::Sampler;
            } else {
                rb.type = ResourceBindingType::Unknown;
            }
            
            bindings_.push_back(rb);
        }
    }

    std::size_t get_binding_count() const noexcept override {
        return bindings_.size();
    }

    const ResourceBinding& get_binding_info(std::size_t index) const override {
        return bindings_[index];
    }

    const ResourceBinding* find_binding(
        std::uint32_t bindingIndex,
        ShaderStage stage,
        ResourceBindingType type = ResourceBindingType::Unknown) const noexcept override {
        for (const auto& binding : bindings_) {
            if (binding.bindingIndex == bindingIndex &&
                binding.stage == stage &&
                (type == ResourceBindingType::Unknown || binding.type == type)) {
                return &binding;
            }
        }
        return nullptr;
    }

private:
    std::vector<ResourceBinding> bindings_;
};

class MetalShader final : public IShader {
public:
    MetalShader() = default;

    static Result<std::unique_ptr<IShader>>
    compile(id<MTLDevice> device, const ShaderDesc& desc) {
        if (!validation::shader_payload_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader descriptor payload is invalid");
        }
        if (desc.byteFormat != ShaderByteFormat::unknown &&
            desc.byteFormat != ShaderByteFormat::msl_source) {
            return Status::failure(StatusCode::unsupported,
                                   "Metal shader byte format is not supported");
        }
        NSString* src = [[NSString alloc]
            initWithBytes:desc.bytecode.data()
                   length:desc.bytecode.size()
                 encoding:NSUTF8StringEncoding];
        if (!src) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader bytecode is not valid UTF-8 MSL source");
        }
        NSError* err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:src options:nil error:&err];
        if (!lib) {
            const char* msg = err ? [err.localizedDescription UTF8String]
                                  : "unknown MSL compile error";
            return Status::failure(StatusCode::invalid_argument, msg);
        }
        id<MTLFunction> fn =
            [lib newFunctionWithName:[NSString stringWithUTF8String:desc.entryPoint.c_str()]];
        if (!fn) {
            return Status::failure(StatusCode::invalid_argument,
                                   "entry point not found in compiled library");
        }
        auto shader      = std::make_unique<MetalShader>();
        shader->desc_ = desc;
        shader->function_ = fn;
        return std::unique_ptr<IShader>(std::move(shader));
    }

    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const ShaderDesc& desc() const noexcept { return desc_; }
    id<MTLFunction> function() const noexcept { return function_; }

private:
    ShaderDesc desc_;
    id<MTLFunction> function_ = nil;
};

class MetalPipeline final : public IPipeline {
public:
    MetalPipeline() = default;

    static Result<std::unique_ptr<IPipeline>>
    create(id<MTLDevice> device, const PipelineDesc& desc) {
        if (!desc.vertexShader || !desc.fragmentShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "PipelineDesc must provide both vertex and fragment shaders");
        }
        auto* vertexShader = dynamic_cast<MetalShader*>(desc.vertexShader);
        auto* fragmentShader = dynamic_cast<MetalShader*>(desc.fragmentShader);
        if (!vertexShader || !fragmentShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "PipelineDesc shaders must be created by Metal backend");
        }
        if (vertexShader->desc().stage != ShaderStage::vertex ||
            fragmentShader->desc().stage != ShaderStage::fragment) {
            return Status::failure(StatusCode::invalid_argument,
                                   "PipelineDesc shader stages are invalid");
        }
        auto* rpd           = [MTLRenderPipelineDescriptor new];
        rpd.label           = [NSString stringWithUTF8String:desc.debugName.c_str()];
        rpd.vertexFunction  = vertexShader->function();
        rpd.fragmentFunction = fragmentShader->function();
        auto* colorAttachment = rpd.colorAttachments[0];
        colorAttachment.pixelFormat = to_mtl_format(desc.colorFormat);
        colorAttachment.writeMask =
            to_metal_color_write_mask(desc.colorBlend.writeMask);
        colorAttachment.blendingEnabled = desc.colorBlend.enabled;
        if (desc.colorBlend.enabled) {
            colorAttachment.sourceRGBBlendFactor =
                to_metal_blend_factor(desc.colorBlend.srcColor);
            colorAttachment.destinationRGBBlendFactor =
                to_metal_blend_factor(desc.colorBlend.dstColor);
            colorAttachment.rgbBlendOperation =
                to_metal_blend_op(desc.colorBlend.colorOp);
            colorAttachment.sourceAlphaBlendFactor =
                to_metal_blend_factor(desc.colorBlend.srcAlpha);
            colorAttachment.destinationAlphaBlendFactor =
                to_metal_blend_factor(desc.colorBlend.dstAlpha);
            colorAttachment.alphaBlendOperation =
                to_metal_blend_op(desc.colorBlend.alphaOp);
        }

        NSError* err = nil;
        MTLRenderPipelineReflection* reflectionInfo = nil;
        id<MTLRenderPipelineState> pso =
            [device newRenderPipelineStateWithDescriptor:rpd options:MTLPipelineOptionBufferTypeInfo reflection:&reflectionInfo error:&err];
        if (!pso) {
            const char* msg = err ? [err.localizedDescription UTF8String]
                                  : "pipeline state creation failed";
            return Status::failure(StatusCode::invalid_argument, msg);
        }
        auto pipeline      = std::make_unique<MetalPipeline>();
        pipeline->desc_    = desc;
        pipeline->pso_     = pso;
        
        auto refl = std::make_unique<MetalPipelineReflection>();
        if (reflectionInfo) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            refl->add_bindings(reflectionInfo.vertexBindings, ShaderStage::vertex);
            refl->add_bindings(reflectionInfo.fragmentBindings, ShaderStage::fragment);
#pragma clang diagnostic pop
        }
        pipeline->reflection_ = std::move(refl);
        
        return std::unique_ptr<IPipeline>(std::move(pipeline));
    }

    const PipelineDesc&        desc()   const noexcept override { return desc_; }
    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const IPipelineReflection* reflection() const noexcept override { return reflection_.get(); }
    id<MTLRenderPipelineState> native() const noexcept { return pso_; }

private:
    PipelineDesc               desc_;
    id<MTLRenderPipelineState> pso_ = nil;
    std::unique_ptr<MetalPipelineReflection> reflection_;
};

class MetalComputePipeline final : public IComputePipeline {
public:
    MetalComputePipeline() = default;

    static Result<std::unique_ptr<IComputePipeline>>
    create(id<MTLDevice> device, const ComputePipelineDesc& desc) {
        if (!desc.computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "ComputePipelineDesc must provide compute shader");
        }

        auto* computeShader = dynamic_cast<MetalShader*>(desc.computeShader);
        if (!computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "ComputePipelineDesc shader must be created by Metal backend");
        }
        if (computeShader->desc().stage != ShaderStage::compute) {
            return Status::failure(StatusCode::invalid_argument,
                                   "ComputePipelineDesc requires compute shader stage");
        }

        NSError* err = nil;
        MTLComputePipelineReflection* reflectionInfo = nil;
        id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:computeShader->function() options:MTLPipelineOptionBufferTypeInfo reflection:&reflectionInfo error:&err];
        
        if (!pso) {
            const char* msg = err ? [err.localizedDescription UTF8String]
                                  : "compute pipeline state creation failed";
            return Status::failure(StatusCode::invalid_argument, msg);
        }

        auto pipeline   = std::make_unique<MetalComputePipeline>();
        pipeline->desc_ = desc;
        pipeline->pso_  = pso;

        auto refl = std::make_unique<MetalPipelineReflection>();
        if (reflectionInfo) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            refl->add_bindings(reflectionInfo.bindings, ShaderStage::compute);
#pragma clang diagnostic pop
        }
        pipeline->reflection_ = std::move(refl);

        return std::unique_ptr<IComputePipeline>(std::move(pipeline));
    }


    const ComputePipelineDesc&  desc()   const noexcept override { return desc_; }
    std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::metal;
    }
    const IPipelineReflection* reflection() const noexcept override { return reflection_.get(); }
    id<MTLComputePipelineState> native() const noexcept { return pso_; }

private:
    ComputePipelineDesc         desc_;
    id<MTLComputePipelineState> pso_ = nil;
    std::unique_ptr<MetalPipelineReflection> reflection_;
};

// ---------------------------------------------------------------------------
// Fence
// ---------------------------------------------------------------------------

class MetalFence final : public IFence {
public:
    explicit MetalFence(FenceDesc desc)
        : completedValue_(std::make_shared<std::atomic<std::uint64_t>>(
              desc.signaled && desc.initialValue == 0 ? 1 : desc.initialValue))
        , nextSignalValue_(
              desc.signaled && desc.initialValue == 0 ? 1 : desc.initialValue)
        , sem_(dispatch_semaphore_create(signaled() ? 1 : 0)) {}

    ~MetalFence() { dispatch_release(sem_); }

    bool signaled() const noexcept override {
        const auto completed = completedValue_->load(std::memory_order_acquire);
        return completed != 0 &&
               completed >= nextSignalValue_.load(std::memory_order_acquire);
    }

    std::uint64_t value() const noexcept override {
        return completedValue_->load(std::memory_order_acquire);
    }

    Status wait_for(std::uint64_t timeoutNanoseconds) noexcept override {
        const auto target = nextSignalValue_.load(std::memory_order_acquire) == 0
            ? 1
            : nextSignalValue_.load(std::memory_order_acquire);
        return wait_for_value(target, timeoutNanoseconds);
    }

    Status wait_for_value(std::uint64_t targetValue,
                          std::uint64_t timeoutNanoseconds) noexcept override {
        if (value() >= targetValue) {
            return Status::success();
        }
        const auto when = timeoutNanoseconds == std::numeric_limits<std::uint64_t>::max()
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW,
                            static_cast<std::int64_t>(std::min<std::uint64_t>(
                                timeoutNanoseconds,
                                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))));
        if (dispatch_semaphore_wait(sem_, when) != 0) {
            return Status::failure(StatusCode::timeout,
                                   "MetalFence: wait timed out");
        }
        if (value() < targetValue) {
            dispatch_semaphore_signal(sem_);
            return Status::failure(StatusCode::timeout,
                                   "MetalFence: wait timed out");
        }
        dispatch_semaphore_signal(sem_);
        return Status::success();
    }

    Status reset() noexcept override {
        if (!signaled() &&
            nextSignalValue_.load(std::memory_order_acquire) >
                completedValue_->load(std::memory_order_acquire)) {
            return Status::failure(StatusCode::invalid_state,
                                   "MetalFence: cannot reset while GPU work is pending");
        }
        completedValue_->store(0, std::memory_order_release);
        nextSignalValue_.store(0, std::memory_order_release);
        while (dispatch_semaphore_wait(sem_, DISPATCH_TIME_NOW) == 0) {
        }
        return Status::success();
    }

    // Block until the fence is signaled (GPU completion handler has fired).
    void wait() noexcept override {
        dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER);
        // Restore semaphore count so repeated wait() calls are safe.
        dispatch_semaphore_signal(sem_);
    }

    // Called by MetalQueue::submit() before committing the command buffer.
    // Captures shared_ptr and ARC-retained semaphore — safe even if the fence
    // is destroyed before the GPU fires the handler.
    void attach_completion_handler(id<MTLCommandBuffer> cmdBuf) {
        const auto target = nextSignalValue_.fetch_add(1, std::memory_order_acq_rel) + 1;
        auto valueRef = completedValue_;   // shared_ptr copy keeps atomic alive
        dispatch_semaphore_t sem = sem_;   // ARC retains sem for the block
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer>) {
            valueRef->store(target, std::memory_order_release);
            dispatch_semaphore_signal(sem);
        }];
    }

    MetalDevice* device_ = nullptr;

private:
    std::shared_ptr<std::atomic<std::uint64_t>> completedValue_;
    std::atomic<std::uint64_t> nextSignalValue_;
    dispatch_semaphore_t sem_;
};

// ---------------------------------------------------------------------------
// Command buffer (forward-declared so MetalSwapchain can reference it)
// ---------------------------------------------------------------------------

class MetalCommandBuffer final : public ICommandBuffer {
public:
    MetalCommandBuffer(id<MTLCommandQueue> queue, BackendDiagnosticsPtr diagnostics)
        : queue_(queue), diagnostics_(std::move(diagnostics)) {}

    void reset() {
        state_ = State::initial;
        cmdBuf_ = nil;
        encoder_ = nil;
        compute_encoder_ = nil;
        graphicsPipelineBound_ = false;
        computePipelineBound_ = false;
        topology_ = PrimitiveTopology::triangle_list;
        indexBuf_ = nil;
        indexBufOffset_ = 0;
        indexBufType_ = MTLIndexTypeUInt32;
        debugLabelDepth_ = 0;
        graphicsLayout_ = nullptr;
        computeLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        boundComputeGroups_.clear();
    }

    MetalDevice* device_ = nullptr;

    Status begin() override {
        if (state_ != State::initial) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer can begin only once");
        }
        cmdBuf_ = [queue_ commandBuffer];
        if (!cmdBuf_) {
            return Status::failure(StatusCode::unavailable,
                                   "failed to allocate Metal command buffer");
        }
        state_ = State::recording;
        return Status::success();
    }

    Status begin_render_pass(const RenderPassDesc& desc) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "begin_render_pass requires recording state");
        }
        if (!validation::is_non_zero(desc.extent)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "render pass extent must be non-zero");
        }
        if (encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "render pass is already active");
        }
        if (compute_encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "cannot begin render pass while compute encoder is active");
        }
        if (desc.colorAttachment.texture &&
            desc.colorAttachment.texture->backend_kind() != BackendKind::metal) {
            return Status::failure(StatusCode::invalid_argument,
                                   "color attachment texture must be created by the Metal backend");
        }
        if (desc.colorAttachment.texture &&
            !validation::texture_supports_usage(
                desc.colorAttachment.texture->desc(),
                TextureUsageFlags::color_attachment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "color attachment texture lacks color attachment usage");
        }
        if (desc.depthAttachment.texture &&
            desc.depthAttachment.texture->backend_kind() != BackendKind::metal) {
            return Status::failure(StatusCode::invalid_argument,
                                   "depth attachment texture must be created by the Metal backend");
        }
        if (desc.depthAttachment.texture &&
            !validation::texture_supports_usage(
                desc.depthAttachment.texture->desc(),
                TextureUsageFlags::depth_stencil)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "depth attachment texture lacks depth usage");
        }
        auto* rpd = [MTLRenderPassDescriptor new];

        if (auto* tex = dynamic_cast<MetalTexture*>(desc.colorAttachment.texture)) {
            rpd.colorAttachments[0].texture     = tex->native();
            rpd.colorAttachments[0].loadAction  = to_mtl_load(desc.colorAttachment.loadOp);
            rpd.colorAttachments[0].storeAction = to_mtl_store(desc.colorAttachment.storeOp);
            const auto& c                       = desc.colorAttachment.clearValue;
            rpd.colorAttachments[0].clearColor  = MTLClearColorMake(c.r, c.g, c.b, c.a);
        }

        if (auto* dtex = dynamic_cast<MetalTexture*>(desc.depthAttachment.texture)) {
            rpd.depthAttachment.texture     = dtex->native();
            rpd.depthAttachment.loadAction  = to_mtl_load(desc.depthAttachment.loadOp);
            rpd.depthAttachment.storeAction = to_mtl_store(desc.depthAttachment.storeOp);
            rpd.depthAttachment.clearDepth  = desc.depthAttachment.clearDepth;
        }

        encoder_ = [cmdBuf_ renderCommandEncoderWithDescriptor:rpd];
        if (!encoder_) {
            return Status::failure(StatusCode::unavailable,
                                   "failed to create Metal render command encoder");
        }

        // Default viewport/scissor from pass extent
        const double w = static_cast<double>(desc.extent.width);
        const double h = static_cast<double>(desc.extent.height);
        [encoder_ setViewport:MTLViewport{0.0, 0.0, w, h, 0.0, 1.0}];
        [encoder_ setScissorRect:MTLScissorRect{0, 0, desc.extent.width, desc.extent.height}];

        topology_ = PrimitiveTopology::triangle_list;
        graphicsPipelineBound_ = false;
        graphicsLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        return Status::success();
    }

    Status end_render_pass() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "end_render_pass requires recording state");
        }
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state, "no active render pass");
        }
        [encoder_ endEncoding];
        encoder_ = nil;
        compute_encoder_ = nil;
        graphicsPipelineBound_ = false;
        graphicsLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        return Status::success();
    }

    Status bind_pipeline(IPipeline& pipeline) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_pipeline requires an active render pass");
        }
        auto* mp = dynamic_cast<MetalPipeline*>(&pipeline);
        if (!mp) {
            return Status::failure(StatusCode::invalid_argument,
                                   "pipeline must be created by the Metal backend");
        }
        [encoder_ setRenderPipelineState:mp->native()];
        [encoder_ setCullMode:to_metal_cull_mode(mp->desc().rasterState.cullMode)];
        [encoder_ setFrontFacingWinding:to_metal_winding(mp->desc().rasterState.frontFace)];
        [encoder_ setTriangleFillMode:to_metal_fill_mode(mp->desc().rasterState.fillMode)];
        [encoder_ setDepthClipMode:mp->desc().rasterState.depthClip
                                    ? MTLDepthClipModeClip
                                    : MTLDepthClipModeClamp];
        topology_ = mp->desc().topology;
        graphicsPipelineBound_ = true;
        graphicsLayout_ = &mp->desc().layout;
        boundGraphicsGroups_.clear();
        computePipelineBound_ = false;
        computeLayout_ = nullptr;
        boundComputeGroups_.clear();
        return Status::success();
    }

    Status bind_compute_pipeline(IComputePipeline& pipeline) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_compute_pipeline requires recording state");
        }
        if (encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Cannot bind compute pipeline during a render pass");
        }
        auto* mp = dynamic_cast<MetalComputePipeline*>(&pipeline);
        if (!mp) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline must be created by the Metal backend");
        }
        if (!compute_encoder_) {
            compute_encoder_ = [cmdBuf_ computeCommandEncoder];
        }
        [compute_encoder_ setComputePipelineState:mp->native()];
        computePipelineBound_ = true;
        computeLayout_ = &mp->desc().layout;
        boundComputeGroups_.clear();
        graphicsPipelineBound_ = false;
        graphicsLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        return Status::success();
    }

    Status bind_vertex_buffer(std::uint32_t binding,
                               IBuffer&      buffer,
                               std::size_t   offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_vertex_buffer requires an active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::vertex)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks vertex usage");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "vertex buffer must be created by the Metal backend");
        }
        [encoder_ setVertexBuffer:metalBuffer->native()
                           offset:offset
                          atIndex:binding];
        return Status::success();
    }

    Status bind_index_buffer(IBuffer& buffer, std::size_t offset,
                               IndexFormat format) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_index_buffer requires an active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::index)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks index usage");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "index buffer must be created by the Metal backend");
        }
        indexBuf_       = metalBuffer->native();
        indexBufOffset_ = offset;
        indexBufType_   = to_mtl_index_type(format);
        return Status::success();
    }

    Status bind_uniform_buffer(std::uint32_t binding,
                                IBuffer&      buffer,
                                std::size_t   offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_uniform_buffer requires an active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::uniform)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks uniform usage");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "uniform buffer must be created by the Metal backend");
        }
        id<MTLBuffer> mtlBuf = metalBuffer->native();
        [encoder_ setVertexBuffer:mtlBuf   offset:offset atIndex:binding];
        [encoder_ setFragmentBuffer:mtlBuf offset:offset atIndex:binding];
        return Status::success();
    }

    Status bind_storage_buffer(std::uint32_t binding,
                                 IBuffer&      buffer,
                                 std::size_t   offset) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_storage_buffer requires recording state");
        }
        if (encoder_) {
            return Status::failure(StatusCode::unsupported,
                                   "Storage buffers not supported in render passes yet");
        }
        if (!compute_encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Must bind compute pipeline before binding storage buffer");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::storage)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks storage usage");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "storage buffer must be created by the Metal backend");
        }
        id<MTLBuffer> mtlBuf = metalBuffer->native();
        [compute_encoder_ setBuffer:mtlBuf offset:offset atIndex:binding];
        return Status::success();
    }

    Status bind_group(std::uint32_t groupIndex, IBindGroup& group) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_group requires recording state");
        }
        if (!dynamic_cast<MetalBindGroup*>(&group)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "bind group must be created by the Metal backend");
        }
        if (!validation::bind_group_desc_valid(group.desc())) {
            return Status::failure(StatusCode::invalid_argument,
                                   "bind group descriptor is invalid");
        }
        const auto* activeLayout = encoder_ ? graphicsLayout_ : computeLayout_;
        auto& boundGroups = encoder_ ? boundGraphicsGroups_ : boundComputeGroups_;
        if (!activeLayout) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_group requires a bound pipeline");
        }
        if (!validation::pipeline_layout_bind_group_compatible(
                *activeLayout, groupIndex, group.desc().layout->desc())) {
            return Status::failure(
                StatusCode::invalid_argument,
                "bind group layout is incompatible with pipeline layout");
        }
        remember_bound_group(boundGroups, groupIndex);
        return Status::success();
    }

    Status resource_barrier(const BufferBarrierDesc& barrier) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer barrier requires recording state");
        }
        if (encoder_ || compute_encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer barrier requires no active encoder");
        }
        if (!validation::buffer_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier is invalid");
        }
        if (barrier.buffer->backend_kind() != BackendKind::metal) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier resource must be created by the Metal backend");
        }
        return Status::success();
    }

    Status resource_barrier(const TextureBarrierDesc& barrier) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "texture barrier requires recording state");
        }
        if (encoder_ || compute_encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "texture barrier requires no active encoder");
        }
        if (!validation::texture_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier is invalid");
        }
        if (barrier.texture->backend_kind() != BackendKind::metal) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier resource must be created by the Metal backend");
        }
        return Status::success();
    }

    Status set_viewport(float x, float y, float width, float height,
                         float minDepth, float maxDepth) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_viewport requires an active render pass");
        }
        if (!validation::viewport_valid(x, y, width, height, minDepth, maxDepth)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "viewport descriptor is invalid");
        }
        [encoder_ setViewport:MTLViewport{x, y, width, height, minDepth, maxDepth}];
        return Status::success();
    }

    Status set_scissor(std::uint32_t x, std::uint32_t y,
                        std::uint32_t width, std::uint32_t height) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_scissor requires an active render pass");
        }
        if (!validation::scissor_valid(x, y, width, height)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "scissor rectangle is invalid");
        }
        [encoder_ setScissorRect:MTLScissorRect{x, y, width, height}];
        return Status::success();
    }

    Status draw(std::uint32_t vertex_count) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires an active render pass");
        }
        if (!graphicsPipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires a bound graphics pipeline");
        }
        if (const auto s = require_graphics_bind_groups("draw"); !s.ok()) {
            return s;
        }
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                     vertexStart:0
                     vertexCount:vertex_count];
        record_draw();
        return Status::success();
    }

    Status draw_instanced(std::uint32_t vertex_count,
                           std::uint32_t instance_count) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_instanced requires an active render pass");
        }
        if (!graphicsPipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_instanced requires a bound graphics pipeline");
        }
        if (const auto s = require_graphics_bind_groups("draw_instanced"); !s.ok()) {
            return s;
        }
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                     vertexStart:0
                     vertexCount:vertex_count
                   instanceCount:instance_count];
        record_draw();
        return Status::success();
    }

    Status draw_indexed(std::uint32_t index_count) override {
        return draw_indexed_instanced(index_count, 1);
    }

    Status draw_indexed_instanced(std::uint32_t index_count,
                                   std::uint32_t instance_count) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_instanced requires an active render pass");
        }
        if (!graphicsPipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_instanced requires a bound graphics pipeline");
        }
        if (const auto s = require_graphics_bind_groups("draw_indexed_instanced"); !s.ok()) {
            return s;
        }
        if (!indexBuf_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_instanced requires a bound index buffer");
        }
        [encoder_ drawIndexedPrimitives:to_mtl_primitive(topology_)
                             indexCount:index_count
                              indexType:indexBufType_
                            indexBuffer:indexBuf_
                      indexBufferOffset:indexBufOffset_
                          instanceCount:instance_count];
        record_draw();
        return Status::success();
    }

    Status draw_indirect(IBuffer& indirect_buffer, std::size_t offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect requires an active render pass");
        }
        if (!graphicsPipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect requires a bound graphics pipeline");
        }
        if (const auto s = require_graphics_bind_groups("draw_indirect"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks indirect usage");
        }
        if (offset % 4u != 0 ||
            !validation::range_fits(
                offset,
                sizeof(MTLDrawPrimitivesIndirectArguments),
                indirect_buffer.desc().size)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "invalid indirect draw argument range");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&indirect_buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "indirect buffer must be created by the Metal backend");
        }
        id<MTLBuffer> mtlBuf = metalBuffer->native();
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                 indirectBuffer:mtlBuf
           indirectBufferOffset:offset];
        record_draw();
        return Status::success();
    }

    Status draw_indexed_indirect(IBuffer& indirect_buffer, std::size_t offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires an active render pass");
        }
        if (!graphicsPipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires a bound graphics pipeline");
        }
        if (const auto s = require_graphics_bind_groups("draw_indexed_indirect"); !s.ok()) {
            return s;
        }
        if (!indexBuf_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires a bound index buffer");
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks indirect usage");
        }
        if (offset % 4u != 0 ||
            !validation::range_fits(
                offset,
                sizeof(MTLDrawIndexedPrimitivesIndirectArguments),
                indirect_buffer.desc().size)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "invalid indexed indirect draw argument range");
        }
        auto* metalBuffer = dynamic_cast<MetalBuffer*>(&indirect_buffer);
        if (!metalBuffer) {
            return Status::failure(StatusCode::invalid_argument,
                                   "indirect buffer must be created by the Metal backend");
        }
        id<MTLBuffer> mtlBuf = metalBuffer->native();
        [encoder_ drawIndexedPrimitives:to_mtl_primitive(topology_)
                              indexType:indexBufType_
                            indexBuffer:indexBuf_
                      indexBufferOffset:indexBufOffset_
                         indirectBuffer:mtlBuf
                   indirectBufferOffset:offset];
        record_draw();
        return Status::success();
    }

    Status dispatch_compute(std::uint32_t group_count_x,
                             std::uint32_t group_count_y,
                             std::uint32_t group_count_z) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch_compute requires recording state");
        }
        if (encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch_compute cannot run during a render pass");
        }
        if (!compute_encoder_) {
            return Status::failure(StatusCode::invalid_state, "No compute encoder active");
        }
        if (!computePipelineBound_) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch_compute requires a bound compute pipeline");
        }
        if (!validation::pipeline_layout_required_groups_bound(
                *computeLayout_, boundComputeGroups_)) {
            return Status::failure(
                StatusCode::invalid_state,
                "dispatch_compute requires all pipeline bind groups");
        }
        MTLSize threadgroups = MTLSizeMake(group_count_x, group_count_y, group_count_z);
        MTLSize threadsPerThreadgroup = MTLSizeMake(64, 1, 1);
        [compute_encoder_ dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().dispatchesRecorded;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             {}, "dispatch recorded");
        return Status::success();
    }

    Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not recording");
        }
        if (encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "cannot end command buffer with an active render pass");
        }
        if (compute_encoder_) { // close any open compute encoder
            [compute_encoder_ endEncoding];
            compute_encoder_ = nil;
            computePipelineBound_ = false;
            computeLayout_ = nullptr;
            boundComputeGroups_.clear();
        }
        if (debugLabelDepth_ != 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "cannot end command buffer with an active debug label");
        }
        state_ = State::ready;
        return Status::success();
    }

    bool ready_for_submit() const noexcept override {
        return state_ == State::ready;
    }

    CommandBufferState state() const noexcept override {
        switch (state_) {
            case State::initial: return CommandBufferState::initial;
            case State::recording: return CommandBufferState::recording;
            case State::ready: return CommandBufferState::executable;
            case State::submitted: return CommandBufferState::submitted;
        }
        return CommandBufferState::initial;
    }

    Status push_debug_label(const DebugLabelDesc& desc) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "push_debug_label requires recording state");
        }
        if (!validation::debug_label_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "debug label descriptor is invalid");
        }
        ++debugLabelDepth_;
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().debugLabelsPushed;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             desc.name, "debug label pushed");
        return Status::success();
    }

    Status pop_debug_label() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "pop_debug_label requires recording state");
        }
        if (debugLabelDepth_ == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "no active debug label to pop");
        }
        --debugLabelDepth_;
        return Status::success();
    }

    Status insert_debug_marker(const DebugLabelDesc& desc) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "insert_debug_marker requires recording state");
        }
        if (!validation::debug_label_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "debug marker descriptor is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().debugMarkersInserted;
        }
        record_backend_event(diagnostics_, BackendEventKind::debug_marker,
                             desc.name, "debug marker inserted");
        return Status::success();
    }

    void mark_submitted() noexcept {
        if (state_ == State::ready) {
            state_ = State::submitted;
        }
    }

    // Internal: called by MetalSwapchain::schedule_present (same backend).
    void attach_drawable(id<CAMetalDrawable> drawable) {
        [cmdBuf_ presentDrawable:drawable];
    }

    bool can_schedule_present() const noexcept {
        return state_ == State::recording && encoder_ == nil && compute_encoder_ == nil;
    }

    id<MTLCommandBuffer> native() const noexcept { return cmdBuf_; }

private:
    enum class State { initial, recording, ready, submitted };

    void record_draw() {
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().drawsRecorded;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             {}, "draw recorded");
    }

    static void remember_bound_group(std::vector<std::uint32_t>& boundGroups,
                                     std::uint32_t groupIndex) {
        for (const auto bound : boundGroups) {
            if (bound == groupIndex) {
                return;
            }
        }
        boundGroups.push_back(groupIndex);
    }

    [[nodiscard]] Status require_graphics_bind_groups(const char* op) const {
        if (!graphicsLayout_) {
            return Status::failure(
                StatusCode::invalid_state,
                std::string{op} + " requires a bound graphics pipeline");
        }
        if (!validation::pipeline_layout_required_groups_bound(
                *graphicsLayout_, boundGraphicsGroups_)) {
            return Status::failure(
                StatusCode::invalid_state,
                std::string{op} + " requires all pipeline bind groups");
        }
        return Status::success();
    }

    id<MTLCommandQueue>         queue_          = nil;
    id<MTLCommandBuffer>        cmdBuf_         = nil;
    id<MTLRenderCommandEncoder> encoder_        = nil;
    id<MTLComputeCommandEncoder> compute_encoder_= nil;
    State                       state_          = State::initial;
    PrimitiveTopology           topology_       = PrimitiveTopology::triangle_list;
    id<MTLBuffer>               indexBuf_       = nil;
    std::size_t                 indexBufOffset_ = 0;
    MTLIndexType                indexBufType_   = MTLIndexTypeUInt32;
    bool                        graphicsPipelineBound_ = false;
    bool                        computePipelineBound_ = false;
    std::uint32_t               debugLabelDepth_ = 0;
    const PipelineLayoutDesc*    graphicsLayout_ = nullptr;
    const PipelineLayoutDesc*    computeLayout_ = nullptr;
    std::vector<std::uint32_t>   boundGraphicsGroups_;
    std::vector<std::uint32_t>   boundComputeGroups_;
    BackendDiagnosticsPtr       diagnostics_;
};

// ---------------------------------------------------------------------------
// Surface + Swapchain
// ---------------------------------------------------------------------------

class MetalSurface final : public ISurface {
public:
    explicit MetalSurface(const SurfaceDesc& desc) : desc_(desc) {
        if (desc.native.kind == NativeSurfaceKind::cocoa_layer && desc.native.handle) {
            layer_ = (__bridge CAMetalLayer*)desc.native.handle;
        }
    }
    const SurfaceDesc& desc() const noexcept override { return desc_; }
    CAMetalLayer*      layer() const noexcept { return layer_; }

private:
    SurfaceDesc   desc_;
    CAMetalLayer* layer_ = nil;
};

class MetalSwapchain final : public ISwapchain {
public:
    MetalSwapchain(id<MTLDevice> device, MetalSurface* surface,
                   const SwapchainDesc& desc)
        : device_(device), desc_(desc) {
        if (desc_.imageCount == 0) {
            desc_.imageCount = desc_.framesInFlight;
        }
        layer_ = surface->layer();
        if (layer_) {
            layer_.device           = device;
            layer_.pixelFormat      = MTLPixelFormatBGRA8Unorm;
            layer_.drawableSize     = CGSizeMake(desc.extent.width, desc.extent.height);
            layer_.maximumDrawableCount = std::min(image_count(), 3u);
        }
    }

    const SwapchainDesc& desc() const noexcept override { return desc_; }

    Status resize(Extent2D extent) override {
        if (extent.width == 0 || extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain extent must be non-zero");
        }
        desc_.extent = extent;
        if (layer_) {
            layer_.drawableSize = CGSizeMake(extent.width, extent.height);
        }
        drawable_        = nil;
        drawableTexture_.reset();
        acquired_ = false;
        currentImageIndex_ = 0;
        nextImageIndex_ = 0;
        return Status::success();
    }

    std::uint32_t image_count() const noexcept override {
        return effective_swapchain_image_count(desc_);
    }

    std::uint32_t current_image_index() const noexcept override {
        return currentImageIndex_;
    }

    bool has_acquired_texture() const noexcept override {
        return acquired_;
    }

    SwapchainAcquireResult acquire_next_texture_result() override {
        if (layer_) {
            drawable_ = [layer_ nextDrawable];
            if (!drawable_) {
                acquired_ = false;
                return {
                    .status = Status::failure(StatusCode::unavailable,
                                              "MetalSwapchain: no drawable is currently available"),
                };
            }
            TextureDesc td{
                .extent    = {static_cast<std::uint32_t>(drawable_.texture.width),
                              static_cast<std::uint32_t>(drawable_.texture.height)},
                .format    = desc_.format,
                .debugName = "swapchain_drawable",
            };
            drawableTexture_ = std::make_unique<MetalTexture>(drawable_.texture, td);
        } else {
            // Headless: lazily create / resize an offscreen render target.
            if (!drawableTexture_ ||
                drawableTexture_->desc().extent.width  != desc_.extent.width ||
                drawableTexture_->desc().extent.height != desc_.extent.height) {
                drawableTexture_ = std::make_unique<MetalTexture>(
                    device_, TextureDesc{
                        .extent    = desc_.extent,
                        .format    = desc_.format,
                        .debugName = "headless_drawable",
                    });
                }
        }
        currentImageIndex_ = nextImageIndex_;
        nextImageIndex_ = (nextImageIndex_ + 1) % image_count();
        acquired_ = true;
        return {
            .texture = drawableTexture_.get(),
            .imageIndex = currentImageIndex_,
        };
    }

    Status schedule_present(ICommandBuffer& cmd) override {
        auto* mcmd = dynamic_cast<MetalCommandBuffer*>(&cmd);
        if (!mcmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "schedule_present: not a Metal command buffer");
        }
        if (!mcmd->can_schedule_present()) {
            return Status::failure(StatusCode::invalid_state,
                                   "schedule_present requires recording state outside active encoder");
        }
        if (!acquired_) {
            return Status::failure(StatusCode::invalid_state,
                                   "schedule_present requires an acquired drawable");
        }
        if (!drawable_) {
            acquired_ = false;
            return Status::success(); // headless: nothing to present
        }
        mcmd->attach_drawable(drawable_);
        drawable_ = nil;
        acquired_ = false;
        return Status::success();
    }

private:
    id<MTLDevice>                 device_;
    SwapchainDesc                 desc_;
    CAMetalLayer*                 layer_           = nil;
    id<CAMetalDrawable>           drawable_        = nil;
    std::unique_ptr<MetalTexture> drawableTexture_;
    bool                          acquired_ = false;
    std::uint32_t                 currentImageIndex_ = 0;
    std::uint32_t                 nextImageIndex_ = 0;
};

// ---------------------------------------------------------------------------
// Frame upload ring
// ---------------------------------------------------------------------------

class MetalFrameUploadRing final : public IFrameUploadRing {
public:
    MetalFrameUploadRing(id<MTLDevice> device,
                          std::uint32_t  framesInFlight,
                          std::size_t    capacityPerFrame)
        : framesInFlight_(framesInFlight), capacity_(capacityPerFrame) {
        frames_.reserve(framesInFlight);
        for (std::uint32_t i = 0; i < framesInFlight; ++i) {
            id<MTLBuffer> buf =
                [device newBufferWithLength:std::max(capacityPerFrame, std::size_t{1})
                                    options:MTLResourceStorageModeShared];
            BufferDesc bd{
                .size      = capacityPerFrame,
                .usage     = BufferUsage::storage,
                .debugName = "upload_ring_" + std::to_string(i),
            };
            frames_.push_back({buf, std::make_unique<MetalBuffer>(buf, bd), 0});
        }
    }

    FrameAllocation allocate(std::size_t size, std::size_t alignment) override {
        auto&       frame = frames_[currentFrame_];
        std::size_t base  = 0;
        if (!validation::align_up(frame.head, alignment, base)) {
            return {};
        }
        if (!validation::range_fits(base, size, capacity_)) return {};
        void* ptr  = static_cast<std::byte*>(frame.buf.contents) + base;
        frame.head = base + size;
        return FrameAllocation{frame.wrapper.get(), base, ptr, size};
    }

    void advance() override {
        currentFrame_              = (currentFrame_ + 1) % framesInFlight_;
        frames_[currentFrame_].head = 0;
    }

    Status advance_if_ready(const IFence& completedFence) override {
        if (!completedFence.signaled()) {
            return Status::failure(StatusCode::timeout,
                                   "MetalFrameUploadRing: frame is not ready for reuse");
        }
        advance();
        return Status::success();
    }

    std::uint32_t frames_in_flight()   const noexcept override { return framesInFlight_; }
    std::size_t   capacity_per_frame() const noexcept override { return capacity_; }
    std::uint32_t current_frame_index() const noexcept override { return currentFrame_; }

private:
    struct Frame {
        id<MTLBuffer>                buf;
        std::unique_ptr<MetalBuffer> wrapper;
        std::size_t                  head = 0;
    };

    std::uint32_t      framesInFlight_;
    std::size_t        capacity_;
    std::vector<Frame> frames_;
    std::uint32_t      currentFrame_ = 0;
};

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

class MetalQueue final : public IQueue {
public:
    MetalQueue(QueueKind kind, BackendDiagnosticsPtr diagnostics)
        : kind_(kind), diagnostics_(std::move(diagnostics)) {}

    QueueKind kind() const noexcept override { return kind_; }

    Status submit(ICommandBuffer& cmd, IFence* signal_fence) override {
        auto* mcmd = dynamic_cast<MetalCommandBuffer*>(&cmd);
        if (!mcmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submit: not a Metal command buffer");
        }
        if (!cmd.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not ready for submit");
        }
        if (signal_fence) {
            auto* mfence = dynamic_cast<MetalFence*>(signal_fence);
            if (!mfence) {
                return Status::failure(StatusCode::invalid_argument,
                                       "signal fence does not belong to Metal backend");
            }
            mfence->attach_completion_handler(mcmd->native());
        }
        [mcmd->native() commit];
        mcmd->mark_submitted();
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().submissions;
        }
        record_backend_event(diagnostics_, BackendEventKind::submitted,
                             {}, "command buffer submitted");
        return Status::success();
    }

private:
    // Note: submit() drives the command buffer directly; no queue_ field needed.
    QueueKind kind_ = QueueKind::graphics;
    BackendDiagnosticsPtr diagnostics_;
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

class MetalDevice final : public IDevice {
public:
    MetalDevice(id<MTLDevice> device, BackendDiagnosticsPtr diagnostics)
        : device_(device)
        , cmdQueue_([device newCommandQueueWithMaxCommandBufferCount:64])
        , graphicsQueue_(QueueKind::graphics, diagnostics)
        , computeQueue_(QueueKind::compute, diagnostics)
        , transferQueue_(QueueKind::transfer, diagnostics)
        , caps_(make_metal_capabilities(device))
        , diagnostics_(std::move(diagnostics)) {}

    const Capabilities& capabilities() const noexcept override { return caps_; }
    IQueue& queue(QueueKind kind) override {
        switch (kind) {
            case QueueKind::graphics: return graphicsQueue_;
            case QueueKind::compute: return computeQueue_;
            case QueueKind::transfer: return transferQueue_;
        }
        return graphicsQueue_;
    }

    Result<std::unique_ptr<IBuffer>> create_buffer(const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size must be non-zero");
        }
        if (desc.size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size exceeds device limit");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "buffer memory domain is not supported");
        }
        auto buffer = std::make_unique<MetalBuffer>(device_, desc);
        if (!buffer->valid()) {
            return Status::failure(StatusCode::backend_error,
                                    "failed to allocate Metal buffer");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().buffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "buffer created");
        return std::unique_ptr<IBuffer>(std::move(buffer));
    }

    Result<std::unique_ptr<ITexture>> create_texture(const TextureDesc& desc) override {
        if (!validation::texture_shape_valid(
                desc, caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture extent exceeds device limits");
        }
        if (desc.dimension != TextureDimension::two_d || desc.depth != 1 ||
            desc.mipLevels != 1 || desc.arrayLayers != 1 ||
            desc.sampleCount != 1) {
            return Status::failure(StatusCode::unsupported,
                                   "Metal backend currently supports 2D single-subresource textures");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "texture memory domain is not supported");
        }
        if (!validation::texture_usage_supported_by_format(caps_, desc)) {
            return Status::failure(StatusCode::unsupported,
                                   "texture format does not support requested usage");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().texturesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "texture created");
        return std::unique_ptr<ITexture>(
            std::make_unique<MetalTexture>(device_, desc));
    }

    Result<std::unique_ptr<ISampler>> create_sampler(const SamplerDesc& desc) override {
        if (!validation::sampler_desc_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "sampler descriptor is invalid");
        }
        if (desc.mipLodBias != 0.0f) {
            return Status::failure(
                StatusCode::unsupported,
                "Metal sampler LOD bias is not supported by this backend");
        }
        auto sampler = std::make_unique<MetalSampler>(device_, desc);
        if (!static_cast<MetalSampler*>(sampler.get())->native()) {
            return Status::failure(StatusCode::backend_error,
                                   "failed to create Metal sampler");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().samplersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "sampler created");
        return std::unique_ptr<ISampler>(std::move(sampler));
    }

    Result<std::unique_ptr<IShader>> create_shader(const ShaderDesc& desc) override {
        auto result = MetalShader::compile(device_, desc);
        if (result.ok()) {
            if (diagnostics_) {
                ++diagnostics_->mutable_stats().shadersCreated;
            }
            record_backend_event(diagnostics_, BackendEventKind::resource_created,
                                 desc.entryPoint, "shader created");
        }
        return result;
    }

    Result<std::unique_ptr<IPipeline>> create_pipeline(const PipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal pipeline layout is invalid");
        }
        if (!validation::pipeline_render_state_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal pipeline render state is invalid");
        }
        auto result = MetalPipeline::create(device_, desc);
        if (result.ok()) {
            if (diagnostics_) {
                ++diagnostics_->mutable_stats().graphicsPipelinesCreated;
            }
            record_backend_event(diagnostics_, BackendEventKind::pipeline_created,
                                 desc.debugName, "graphics pipeline created");
        }
        return result;
    }

    Result<std::unique_ptr<IBindGroupLayout>>
    create_bind_group_layout(const BindGroupLayoutDesc& desc) override {
        if (!validation::bind_group_layout_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal bind group layout is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupLayoutsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group layout created");
        return std::unique_ptr<IBindGroupLayout>(
            std::make_unique<MetalBindGroupLayout>(desc));
    }

    Result<std::unique_ptr<IBindGroup>>
    create_bind_group(const BindGroupDesc& desc) override {
        if (!validation::bind_group_desc_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal bind group descriptor is invalid");
        }
        if (!dynamic_cast<MetalBindGroupLayout*>(desc.layout)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "bind group layout must be created by the Metal backend");
        }
        for (const auto& entry : desc.entries) {
            switch (entry.type) {
                case BindingResourceType::uniform_buffer:
                case BindingResourceType::storage_buffer:
                    if ((entry.buffer.buffer &&
                         !dynamic_cast<MetalBuffer*>(entry.buffer.buffer)) ||
                        std::any_of(entry.buffers.begin(), entry.buffers.end(),
                                    [](const BufferBindingDesc& binding) {
                                        return !dynamic_cast<MetalBuffer*>(binding.buffer);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "bind group buffer must be created by the Metal backend");
                    }
                    break;
                case BindingResourceType::sampled_texture:
                case BindingResourceType::storage_texture:
                    if ((entry.texture &&
                         !dynamic_cast<MetalTexture*>(entry.texture)) ||
                        std::any_of(entry.textures.begin(), entry.textures.end(),
                                    [](const ITexture* texture) {
                                        return !dynamic_cast<const MetalTexture*>(texture);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "bind group texture must be created by the Metal backend");
                    }
                    break;
                case BindingResourceType::sampler:
                    if ((entry.sampler &&
                         !dynamic_cast<MetalSampler*>(entry.sampler)) ||
                        std::any_of(entry.samplers.begin(), entry.samplers.end(),
                                    [](const ISampler* sampler) {
                                        return !dynamic_cast<const MetalSampler*>(sampler);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "bind group sampler must be created by the Metal backend");
                    }
                    break;
            }
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group created");
        return std::unique_ptr<IBindGroup>(std::make_unique<MetalBindGroup>(desc));
    }

    Result<std::unique_ptr<IComputePipeline>> create_compute_pipeline(const ComputePipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Metal compute pipeline layout is invalid");
        }
        auto result = MetalComputePipeline::create(device_, desc);
        if (result.ok()) {
            if (diagnostics_) {
                ++diagnostics_->mutable_stats().computePipelinesCreated;
            }
            record_backend_event(diagnostics_, BackendEventKind::pipeline_created,
                                 desc.debugName, "compute pipeline created");
        }
        return result;
    }

    Result<std::unique_ptr<ISurface>> create_surface(const SurfaceDesc& desc) override {
        if (!validation::extent_within(desc.initialExtent,
                                       caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface extent exceeds device limits");
        }
        if (!validation::native_surface_handles_valid(desc.native)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "native surface handles are invalid for the surface kind");
        }
        if (!validation::native_surface_kind_supported(desc.native.kind, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                    "native surface kind is not supported by the Metal backend");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().surfacesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::surface_created,
                             {}, "surface created");
        return std::unique_ptr<ISurface>(std::make_unique<MetalSurface>(desc));
    }

    Result<std::unique_ptr<ISwapchain>>
    create_swapchain(ISurface& surface, const SwapchainDesc& desc) override {
        if (!validation::swapchain_supported(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain description is invalid");
        }
        auto* ms = dynamic_cast<MetalSurface*>(&surface);
        if (!ms) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface is not a Metal surface");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().swapchainsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::swapchain_created,
                             {}, "swapchain created");
        return std::unique_ptr<ISwapchain>(
            std::make_unique<MetalSwapchain>(device_, ms, desc));
    }

    CommandBufferPtr create_command_buffer() override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        MetalCommandBuffer* cmd = nullptr;
        if (!cmd_pool_.empty()) {
            cmd = cmd_pool_.back();
            cmd_pool_.pop_back();
        } else {
            cmd = new MetalCommandBuffer(cmdQueue_, diagnostics_);
            cmd->device_ = this;
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().commandBuffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_buffer_created,
                             {}, "command buffer created");
        return CommandBufferPtr(cmd, [](ICommandBuffer* p) {
            auto* obj = static_cast<MetalCommandBuffer*>(p);
            if (obj->device_) { obj->device_->recycle_command_buffer(obj); }
            else { delete obj; }
        });
    }

    FencePtr create_fence(const FenceDesc& desc) override {
        MetalFence* f = new MetalFence(desc);
        f->device_ = this;
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().fencesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::fence_created,
                             {}, "fence created");
        return FencePtr(f, [](IFence* p) {
            auto* obj = static_cast<MetalFence*>(p);
            if (obj->device_) { obj->device_->recycle_fence(obj); }
            else { delete obj; }
        });
    }

    Result<std::unique_ptr<IFrameUploadRing>>
    create_upload_ring(std::uint32_t frames_in_flight,
                       std::size_t   capacity_per_frame) override {
        if (!validation::frame_count_supported(frames_in_flight, caps_) ||
            capacity_per_frame == 0 || capacity_per_frame > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "frames_in_flight and capacity must be non-zero");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().uploadRingsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::upload_ring_created,
                             {}, "upload ring created");
        return std::unique_ptr<IFrameUploadRing>(std::make_unique<MetalFrameUploadRing>(
            device_, frames_in_flight, capacity_per_frame));
    }

    void recycle_command_buffer(MetalCommandBuffer* cmd) {
        cmd->reset();
        std::lock_guard<std::mutex> lock(pool_mutex_);
        cmd_pool_.push_back(cmd);
    }

    void recycle_fence(MetalFence* f) {
        // Safe fence recycling requires tracking pending GPU handlers.
        // For Phase 4A, we avoid polling the sync object and recreate it.
        delete f;
    }

    ~MetalDevice() {
        for (auto* c : cmd_pool_) { delete c; }
    }

private:
    id<MTLDevice>      device_;
    id<MTLCommandQueue> cmdQueue_;
    MetalQueue         graphicsQueue_;
    MetalQueue         computeQueue_;
    MetalQueue         transferQueue_;
    Capabilities       caps_;
    BackendDiagnosticsPtr diagnostics_;
    std::mutex pool_mutex_;
    std::vector<MetalCommandBuffer*> cmd_pool_;
};

// ---------------------------------------------------------------------------
// Backend
// ---------------------------------------------------------------------------

class MetalBackend final : public IBackend {
public:
    MetalBackend() { device_ = MTLCreateSystemDefaultDevice(); }

    BackendKind kind() const noexcept override { return BackendKind::metal; }

    [[nodiscard]] BackendStats backend_stats() const noexcept override {
        return diagnostics_->stats();
    }

    [[nodiscard]] std::vector<BackendEvent> recent_events() const override {
        return diagnostics_->recent_events();
    }

    void clear_diagnostics() noexcept override {
        diagnostics_->clear();
    }

    std::vector<AdapterInfo> enumerate_adapters() const override {
        if (!device_) return {};
        return {{
            0,
            std::string([device_.name UTF8String]),
            BackendKind::metal,
            make_metal_capabilities(device_),
            [device_ isLowPower] ? AdapterType::integrated_gpu : AdapterType::discrete_gpu,
            0,
            0,
            "Metal native adapter",
        }};
    }

    Result<std::unique_ptr<IDevice>> create_device(const DeviceDesc& desc) override {
        if (!device_) {
            return Status::failure(StatusCode::unavailable,
                                   "no Metal-capable GPU found on this system");
        }
        if (desc.adapterId != 0) {
            return Status::failure(StatusCode::unavailable,
                                   "Metal backend currently exposes one adapter (id=0)");
        }
        ++diagnostics_->mutable_stats().devicesCreated;
        record_backend_event(diagnostics_, BackendEventKind::device_created,
                             {}, "device created");
        return std::unique_ptr<IDevice>(
            std::make_unique<MetalDevice>(device_, diagnostics_));
    }

private:
    id<MTLDevice> device_ = nil;
    BackendDiagnosticsPtr diagnostics_ = make_backend_diagnostics(BackendKind::metal);
};

} // namespace

std::unique_ptr<IBackend> create_metal_backend() {
    return std::make_unique<MetalBackend>();
}

} // namespace truffle::rhi
