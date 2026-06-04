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

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi {
namespace {

using core::Result;
using core::Status;
using core::StatusCode;

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
    }
    // Wrap an existing MTLBuffer (used by upload ring frames).
    MetalBuffer(id<MTLBuffer> buf, const BufferDesc& desc) : desc_(desc), buf_(buf) {}

    const BufferDesc& desc() const noexcept override { return desc_; }
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
        td.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        tex_ = [device newTextureWithDescriptor:td];
    }
    // Wrap an existing MTLTexture (swapchain drawable, etc.).
    MetalTexture(id<MTLTexture> tex, const TextureDesc& desc) : desc_(desc), tex_(tex) {}

    const TextureDesc& desc() const noexcept override { return desc_; }
    id<MTLTexture>     native() const noexcept { return tex_; }

private:
    TextureDesc    desc_;
    id<MTLTexture> tex_ = nil;
};

class MetalSampler final : public ISampler {
public:
    MetalSampler(id<MTLDevice> device, const SamplerDesc& desc) {
        auto* sd = [MTLSamplerDescriptor new];
        const auto filter = desc.linear_filtering ? MTLSamplerMinMagFilterLinear
                                                  : MTLSamplerMinMagFilterNearest;
        sd.minFilter = filter;
        sd.magFilter = filter;
        sampler_ = [device newSamplerStateWithDescriptor:sd];
    }
    id<MTLSamplerState> native() const noexcept { return sampler_; }

private:
    id<MTLSamplerState> sampler_ = nil;
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

private:
    std::vector<ResourceBinding> bindings_;
};

class MetalShader final : public IShader {
public:
    MetalShader() = default;

    static Result<std::unique_ptr<IShader>>
    compile(id<MTLDevice> device, const ShaderDesc& desc) {
        if (desc.bytecode.empty()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader bytecode must not be empty");
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
        shader->function_ = fn;
        return std::unique_ptr<IShader>(std::move(shader));
    }

    id<MTLFunction> function() const noexcept { return function_; }

private:
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
        auto* rpd           = [MTLRenderPipelineDescriptor new];
        rpd.label           = [NSString stringWithUTF8String:desc.debugName.c_str()];
        rpd.vertexFunction  = static_cast<MetalShader*>(desc.vertexShader)->function();
        rpd.fragmentFunction = static_cast<MetalShader*>(desc.fragmentShader)->function();
        rpd.colorAttachments[0].pixelFormat = to_mtl_format(desc.colorFormat);

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

        NSError* err = nil;
        MTLComputePipelineReflection* reflectionInfo = nil;
        id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:static_cast<MetalShader*>(desc.computeShader)->function() options:MTLPipelineOptionBufferTypeInfo reflection:&reflectionInfo error:&err];
        
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
    explicit MetalFence(bool initially_signaled)
        : signaled_(std::make_shared<std::atomic<bool>>(initially_signaled))
        , sem_(dispatch_semaphore_create(initially_signaled ? 1 : 0)) {}

    ~MetalFence() { dispatch_release(sem_); }

    bool signaled() const noexcept override {
        return signaled_->load(std::memory_order_acquire);
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
        auto flagRef = signaled_;          // shared_ptr copy keeps atomic alive
        dispatch_semaphore_t sem = sem_;   // ARC retains sem for the block
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer>) {
            flagRef->store(true, std::memory_order_release);
            dispatch_semaphore_signal(sem);
        }];
    }

    MetalDevice* device_ = nullptr;

private:
    std::shared_ptr<std::atomic<bool>> signaled_;
    dispatch_semaphore_t sem_;
};

// ---------------------------------------------------------------------------
// Command buffer (forward-declared so MetalSwapchain can reference it)
// ---------------------------------------------------------------------------

class MetalCommandBuffer final : public ICommandBuffer {
public:
    explicit MetalCommandBuffer(id<MTLCommandQueue> queue) : queue_(queue) {}

    void reset() {
        state_ = State::initial;
        cmdBuf_ = nil;
        encoder_ = nil;
        compute_encoder_ = nil;
        topology_ = PrimitiveTopology::triangle_list;
        indexBuf_ = nil;
        indexBufOffset_ = 0;
        indexBufType_ = MTLIndexTypeUInt32;
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
        return Status::success();
    }

    Status end_render_pass() override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state, "no active render pass");
        }
        [encoder_ endEncoding];
        encoder_ = nil;
        compute_encoder_ = nil;
        return Status::success();
    }

    Status bind_pipeline(IPipeline& pipeline) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_pipeline requires an active render pass");
        }
        auto& mp = static_cast<MetalPipeline&>(pipeline);
        [encoder_ setRenderPipelineState:mp.native()];
        topology_ = mp.desc().topology;
        return Status::success();
    }

    Status bind_compute_pipeline(IComputePipeline& pipeline) override {
        if (encoder_) return Status::failure(StatusCode::invalid_state, "Cannot bind compute pipeline during a render pass");
        if (!compute_encoder_) compute_encoder_ = [cmdBuf_ computeCommandEncoder];
        auto& mp = static_cast<MetalComputePipeline&>(pipeline);
        [compute_encoder_ setComputePipelineState:mp.native()];
        return Status::success();
    }

    Status bind_vertex_buffer(std::uint32_t binding,
                               IBuffer&      buffer,
                               std::size_t   offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_vertex_buffer requires an active render pass");
        }
        [encoder_ setVertexBuffer:static_cast<MetalBuffer&>(buffer).native()
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
        indexBuf_       = static_cast<MetalBuffer&>(buffer).native();
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
        id<MTLBuffer> mtlBuf = static_cast<MetalBuffer&>(buffer).native();
        [encoder_ setVertexBuffer:mtlBuf   offset:offset atIndex:binding];
        [encoder_ setFragmentBuffer:mtlBuf offset:offset atIndex:binding];
        return Status::success();
    }

    Status bind_storage_buffer(std::uint32_t binding,
                                IBuffer&      buffer,
                                std::size_t   offset) override {
        if (encoder_) return Status::failure(StatusCode::unsupported, "Storage buffers not supported in render passes yet");
        if (!compute_encoder_) return Status::failure(StatusCode::invalid_state, "Must bind compute pipeline before binding storage buffer");
        id<MTLBuffer> mtlBuf = static_cast<MetalBuffer&>(buffer).native();
        [compute_encoder_ setBuffer:mtlBuf offset:offset atIndex:binding];
        return Status::success();
    }

    Status set_viewport(float x, float y, float width, float height,
                         float minDepth, float maxDepth) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_viewport requires an active render pass");
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
        [encoder_ setScissorRect:MTLScissorRect{x, y, width, height}];
        return Status::success();
    }

    Status draw(std::uint32_t vertex_count) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires an active render pass");
        }
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                     vertexStart:0
                     vertexCount:vertex_count];
        return Status::success();
    }

    Status draw_instanced(std::uint32_t vertex_count,
                           std::uint32_t instance_count) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_instanced requires an active render pass");
        }
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                     vertexStart:0
                     vertexCount:vertex_count
                   instanceCount:instance_count];
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
        return Status::success();
    }

    Status draw_indirect(IBuffer& indirect_buffer, std::size_t offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect requires an active render pass");
        }
        id<MTLBuffer> mtlBuf = static_cast<MetalBuffer&>(indirect_buffer).native();
        [encoder_ drawPrimitives:to_mtl_primitive(topology_)
                 indirectBuffer:mtlBuf
           indirectBufferOffset:offset];
        return Status::success();
    }

    Status draw_indexed_indirect(IBuffer& indirect_buffer, std::size_t offset) override {
        if (!encoder_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires an active render pass");
        }
        if (!indexBuf_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires a bound index buffer");
        }
        id<MTLBuffer> mtlBuf = static_cast<MetalBuffer&>(indirect_buffer).native();
        [encoder_ drawIndexedPrimitives:to_mtl_primitive(topology_)
                              indexType:indexBufType_
                            indexBuffer:indexBuf_
                      indexBufferOffset:indexBufOffset_
                         indirectBuffer:mtlBuf
                   indirectBufferOffset:offset];
        return Status::success();
    }

    Status dispatch_compute(std::uint32_t group_count_x,
                             std::uint32_t group_count_y,
                             std::uint32_t group_count_z) override {
        if (!compute_encoder_) return Status::failure(StatusCode::invalid_state, "No compute encoder active");
        MTLSize threadgroups = MTLSizeMake(group_count_x, group_count_y, group_count_z);
        MTLSize threadsPerThreadgroup = MTLSizeMake(64, 1, 1);
        [compute_encoder_ dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
        return Status::success();
    }

    Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not recording");
        }
        if (encoder_) { // safety: close any open encoder
            [encoder_ endEncoding];
            encoder_ = nil;
        }
        if (compute_encoder_) { // close any open compute encoder
            [compute_encoder_ endEncoding];
            compute_encoder_ = nil;
        }
        state_ = State::ready;
        return Status::success();
    }

    bool ready_for_submit() const noexcept override {
        return state_ == State::ready;
    }

    // Internal: called by MetalSwapchain::schedule_present (same backend).
    void attach_drawable(id<CAMetalDrawable> drawable) {
        [cmdBuf_ presentDrawable:drawable];
    }

    id<MTLCommandBuffer> native() const noexcept { return cmdBuf_; }

private:
    enum class State { initial, recording, ready };

    id<MTLCommandQueue>         queue_          = nil;
    id<MTLCommandBuffer>        cmdBuf_         = nil;
    id<MTLRenderCommandEncoder> encoder_        = nil;
    id<MTLComputeCommandEncoder> compute_encoder_= nil;
    State                       state_          = State::initial;
    PrimitiveTopology           topology_       = PrimitiveTopology::triangle_list;
    id<MTLBuffer>               indexBuf_       = nil;
    std::size_t                 indexBufOffset_ = 0;
    MTLIndexType                indexBufType_   = MTLIndexTypeUInt32;
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
        layer_ = surface->layer();
        if (layer_) {
            layer_.device           = device;
            layer_.pixelFormat      = MTLPixelFormatBGRA8Unorm;
            layer_.drawableSize     = CGSizeMake(desc.extent.width, desc.extent.height);
            layer_.maximumDrawableCount = std::min(desc.framesInFlight, 3u);
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
        return Status::success();
    }

    ITexture* acquire_next_texture() override {
        if (layer_) {
            drawable_ = [layer_ nextDrawable];
            if (!drawable_) return nullptr;
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
        return drawableTexture_.get();
    }

    Status schedule_present(ICommandBuffer& cmd) override {
        if (!drawable_) return Status::success(); // headless: nothing to present
        auto* mcmd = dynamic_cast<MetalCommandBuffer*>(&cmd);
        if (!mcmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "schedule_present: not a Metal command buffer");
        }
        mcmd->attach_drawable(drawable_);
        drawable_ = nil;
        return Status::success();
    }

private:
    id<MTLDevice>                 device_;
    SwapchainDesc                 desc_;
    CAMetalLayer*                 layer_           = nil;
    id<CAMetalDrawable>           drawable_        = nil;
    std::unique_ptr<MetalTexture> drawableTexture_;
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
        if (size == 0) return {};
        auto&       frame = frames_[currentFrame_];
        std::size_t base  = frame.head;
        if (alignment > 1) {
            base = (base + alignment - 1) & ~(alignment - 1);
        }
        if (base + size > capacity_) return {};
        void* ptr  = static_cast<std::byte*>(frame.buf.contents) + base;
        frame.head = base + size;
        return FrameAllocation{frame.wrapper.get(), base, ptr, size};
    }

    void advance() override {
        currentFrame_              = (currentFrame_ + 1) % framesInFlight_;
        frames_[currentFrame_].head = 0;
    }

    std::uint32_t frames_in_flight()   const noexcept override { return framesInFlight_; }
    std::size_t   capacity_per_frame() const noexcept override { return capacity_; }

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
    MetalQueue() = default;

    QueueKind kind() const noexcept override { return QueueKind::graphics; }

    Status submit(ICommandBuffer& cmd, IFence* signal_fence) override {
        if (!cmd.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not ready for submit");
        }
        auto* mcmd = dynamic_cast<MetalCommandBuffer*>(&cmd);
        if (!mcmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submit: not a Metal command buffer");
        }
        if (auto* mfence = dynamic_cast<MetalFence*>(signal_fence)) {
            mfence->attach_completion_handler(mcmd->native());
        }
        [mcmd->native() commit];
        return Status::success();
    }

private:
    // Note: submit() drives the command buffer directly; no queue_ field needed.
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

class MetalDevice final : public IDevice {
public:
    explicit MetalDevice(id<MTLDevice> device)
        : device_(device)
        , cmdQueue_([device newCommandQueueWithMaxCommandBufferCount:64]) {}

    const Capabilities& capabilities() const noexcept override { return caps_; }
    IQueue&             queue(QueueKind /*kind*/) override { return queue_; }

    Result<std::unique_ptr<IBuffer>> create_buffer(const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size must be non-zero");
        }
        return std::unique_ptr<IBuffer>(std::make_unique<MetalBuffer>(device_, desc));
    }

    Result<std::unique_ptr<ITexture>> create_texture(const TextureDesc& desc) override {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture extent must be non-zero");
        }
        return std::unique_ptr<ITexture>(
            std::make_unique<MetalTexture>(device_, desc));
    }

    Result<std::unique_ptr<ISampler>> create_sampler(const SamplerDesc& desc) override {
        return std::unique_ptr<ISampler>(
            std::make_unique<MetalSampler>(device_, desc));
    }

    Result<std::unique_ptr<IShader>> create_shader(const ShaderDesc& desc) override {
        return MetalShader::compile(device_, desc);
    }

    Result<std::unique_ptr<IPipeline>> create_pipeline(const PipelineDesc& desc) override {
        return MetalPipeline::create(device_, desc);
    }

    Result<std::unique_ptr<IComputePipeline>> create_compute_pipeline(const ComputePipelineDesc& desc) override {
        return MetalComputePipeline::create(device_, desc);
    }

    Result<std::unique_ptr<ISurface>> create_surface(const SurfaceDesc& desc) override {
        if (desc.initialExtent.width == 0 || desc.initialExtent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface extent must be non-zero");
        }
        return std::unique_ptr<ISurface>(std::make_unique<MetalSurface>(desc));
    }

    Result<std::unique_ptr<ISwapchain>>
    create_swapchain(ISurface& surface, const SwapchainDesc& desc) override {
        if (desc.extent.width == 0 || desc.extent.height == 0 ||
            desc.framesInFlight == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain description is invalid");
        }
        auto* ms = dynamic_cast<MetalSurface*>(&surface);
        if (!ms) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface is not a Metal surface");
        }
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
            cmd = new MetalCommandBuffer(cmdQueue_);
            cmd->device_ = this;
        }
        return CommandBufferPtr(cmd, [](ICommandBuffer* p) {
            auto* obj = static_cast<MetalCommandBuffer*>(p);
            if (obj->device_) { obj->device_->recycle_command_buffer(obj); }
            else { delete obj; }
        });
    }

    FencePtr create_fence(const FenceDesc& desc) override {
        MetalFence* f = new MetalFence(desc.signaled);
        f->device_ = this;
        return FencePtr(f, [](IFence* p) {
            auto* obj = static_cast<MetalFence*>(p);
            if (obj->device_) { obj->device_->recycle_fence(obj); }
            else { delete obj; }
        });
    }

    Result<std::unique_ptr<IFrameUploadRing>>
    create_upload_ring(std::uint32_t frames_in_flight,
                       std::size_t   capacity_per_frame) override {
        if (frames_in_flight == 0 || capacity_per_frame == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "frames_in_flight and capacity must be non-zero");
        }
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
    MetalQueue         queue_;
    Capabilities       caps_{.presentation = true, .validation = false, .maxFramesInFlight = 3};
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

    std::vector<AdapterInfo> enumerate_adapters() const override {
        if (!device_) return {};
        return {{
            0,
            std::string([device_.name UTF8String]),
            BackendKind::metal,
            Capabilities{.presentation = true, .validation = false, .maxFramesInFlight = 3},
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
        return std::unique_ptr<IDevice>(std::make_unique<MetalDevice>(device_));
    }

private:
    id<MTLDevice> device_ = nil;
};

} // namespace

std::unique_ptr<IBackend> create_metal_backend() {
    return std::make_unique<MetalBackend>();
}

} // namespace truffle::rhi
