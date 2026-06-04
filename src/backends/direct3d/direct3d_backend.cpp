#include "truffle/rhi/rhi.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/rhi/validation.hpp"

#include <memory>
#include <vector>
#include <utility>

namespace truffle::rhi {

namespace {

using core::Result;
using core::Status;
using core::StatusCode;

[[nodiscard]] Capabilities make_direct3d_capabilities() {
    return Capabilities{
        .presentation = false,
        .validation = true,
        .maxFramesInFlight = 3,
        .queues = {.graphics = true, .compute = true, .transfer = true},
        .features = {
            .headlessSurface = true,
            .nativeSurface = false,
            .presentation = false,
            .compute = true,
            .indirectDraw = true,
            .shaderReflection = true,
            .debugLabels = true,
            .validation = true,
            .unifiedMemory = false,
        },
        .limits = {
            .maxTextureDimension2D = 16384,
            .maxBufferSize = 512ull * 1024ull * 1024ull,
            .minUniformBufferOffsetAlignment = 256,
            .minStorageBufferOffsetAlignment = 16,
            .maxColorAttachments = 8,
            .maxVertexBuffers = 16,
        },
        .formats = {
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
        },
        .memoryHeaps = {
            {.kind = MemoryHeapKind::device_local,
             .budgetBytes = 512ull * 1024ull * 1024ull,
             .dedicated = true},
            {.kind = MemoryHeapKind::host_visible,
             .budgetBytes = 256ull * 1024ull * 1024ull,
             .dedicated = false},
        },
        .presentModes = {
            PresentMode::immediate,
            PresentMode::fifo,
            PresentMode::mailbox,
        },
        .surfaceKinds = {
            NativeSurfaceKind::headless,
        },
        .shaderFormats = {
            ShaderByteFormat::contract,
            ShaderByteFormat::hlsl_source,
            ShaderByteFormat::dxil_binary,
        },
    };
}

class Direct3DFence final : public IFence {
public:
    explicit Direct3DFence(FenceDesc desc)
        : value_(desc.signaled && desc.initialValue == 0 ? 1 : desc.initialValue) {}

    [[nodiscard]] bool signaled() const noexcept override { return value_ != 0; }
    [[nodiscard]] std::uint64_t value() const noexcept override { return value_; }

    [[nodiscard]] Status wait_for(std::uint64_t timeoutNanoseconds) noexcept override {
        return wait_for_value(1, timeoutNanoseconds);
    }

    [[nodiscard]] Status wait_for_value(
        std::uint64_t targetValue,
        std::uint64_t timeoutNanoseconds) noexcept override {
        if (value_ >= targetValue) return Status::success();
        if (timeoutNanoseconds == 0) {
            return Status::failure(StatusCode::timeout,
                                   "Direct3DFence: wait timed out");
        }
        value_ = targetValue;
        return Status::success();
    }

    [[nodiscard]] Status reset() noexcept override {
        value_ = 0;
        return Status::success();
    }

    void wait() noexcept override {
        // Milestone 0 behavior: avoid indefinite blocking in stub mode.
        if (value_ == 0) value_ = 1;
    }

    void signal() noexcept { ++value_; }

private:
    std::uint64_t value_ = 0;
};

class Direct3DCommandBuffer final : public ICommandBuffer {
public:
    enum class State {
        initial,
        recording,
        executable,
        submitted,
    };

    [[nodiscard]] Status begin() override {
        if (state_ != State::initial) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: begin requires initial state");
        }
        state_ = State::recording;
        return Status::success();
    }

    [[nodiscard]] Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: end requires recording state");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: cannot end while render pass is active");
        }
        state_ = State::executable;
        return Status::success();
    }

    [[nodiscard]] bool ready_for_submit() const noexcept override {
        return state_ == State::executable;
    }

    [[nodiscard]] CommandBufferState state() const noexcept override {
        switch (state_) {
            case State::initial: return CommandBufferState::initial;
            case State::recording: return CommandBufferState::recording;
            case State::executable: return CommandBufferState::executable;
            case State::submitted: return CommandBufferState::submitted;
        }
        return CommandBufferState::initial;
    }

    [[nodiscard]] Status begin_render_pass(const RenderPassDesc& desc) override {
        if (const auto s = require_recording("begin_render_pass"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: render pass already active");
        }
        if (!validation::is_non_zero(desc.extent)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: render pass extent must be non-zero");
        }
        if (desc.colorAttachment.texture &&
            !validation::texture_supports_usage(
                desc.colorAttachment.texture->desc(),
                TextureUsageFlags::color_attachment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: color attachment texture lacks color attachment usage");
        }
        if (desc.depthAttachment.texture &&
            !validation::texture_supports_usage(
                desc.depthAttachment.texture->desc(),
                TextureUsageFlags::depth_stencil)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: depth attachment texture lacks depth usage");
        }
        inRenderPass_ = true;
        return Status::success();
    }

    [[nodiscard]] Status end_render_pass() override {
        if (const auto s = require_recording("end_render_pass"); !s.ok()) {
            return s;
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: no active render pass");
        }
        inRenderPass_ = false;
        return Status::success();
    }

    [[nodiscard]] Status bind_pipeline(IPipeline& /*pipeline*/) override {
        return require_render_pass("bind_pipeline");
    }

    [[nodiscard]] Status bind_compute_pipeline(IComputePipeline& /*pipeline*/) override {
        if (const auto s = require_recording("bind_compute_pipeline"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: bind_compute_pipeline cannot run inside render pass");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_vertex_buffer(
        std::uint32_t /*binding*/, IBuffer& buffer, std::size_t /*offset*/) override {
        if (const auto s = require_render_pass("bind_vertex_buffer"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::vertex)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks vertex usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_index_buffer(
        IBuffer& buffer, std::size_t /*offset*/, IndexFormat /*format*/) override {
        if (const auto s = require_render_pass("bind_index_buffer"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::index)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks index usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_uniform_buffer(
        std::uint32_t /*binding*/, IBuffer& buffer, std::size_t /*offset*/) override {
        if (const auto s = require_render_pass("bind_uniform_buffer"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::uniform)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks uniform usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_storage_buffer(
        std::uint32_t /*binding*/, IBuffer& buffer, std::size_t /*offset*/) override {
        if (const auto s = require_recording("bind_storage_buffer"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: bind_storage_buffer cannot run inside render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::storage)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks storage usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status resource_barrier(
        const BufferBarrierDesc& barrier) override {
        if (const auto s = require_recording("resource_barrier"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: buffer barrier cannot run inside render pass");
        }
        if (!validation::buffer_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer barrier is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status resource_barrier(
        const TextureBarrierDesc& barrier) override {
        if (const auto s = require_recording("resource_barrier"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: texture barrier cannot run inside render pass");
        }
        if (!validation::texture_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: texture barrier is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_viewport(
        float /*x*/, float /*y*/, float /*width*/, float /*height*/,
        float /*minDepth*/, float /*maxDepth*/) override {
        return require_recording("set_viewport");
    }

    [[nodiscard]] Status set_scissor(
        std::uint32_t /*x*/, std::uint32_t /*y*/,
        std::uint32_t /*width*/, std::uint32_t /*height*/) override {
        return require_recording("set_scissor");
    }

    [[nodiscard]] Status draw(std::uint32_t /*vertex_count*/) override {
        return require_render_pass("draw");
    }

    [[nodiscard]] Status draw_instanced(
        std::uint32_t /*vertex_count*/, std::uint32_t /*instance_count*/) override {
        return require_render_pass("draw_instanced");
    }

    [[nodiscard]] Status draw_indexed(std::uint32_t /*index_count*/) override {
        return require_render_pass("draw_indexed");
    }

    [[nodiscard]] Status draw_indexed_instanced(
        std::uint32_t /*index_count*/, std::uint32_t /*instance_count*/) override {
        return require_render_pass("draw_indexed_instanced");
    }

    [[nodiscard]] Status draw_indirect(
        IBuffer& indirect_buffer, std::size_t /*offset*/) override {
        if (const auto s = require_render_pass("draw_indirect"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks indirect usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed_indirect(
        IBuffer& indirect_buffer, std::size_t /*offset*/) override {
        if (const auto s = require_render_pass("draw_indexed_indirect"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer lacks indirect usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status dispatch_compute(
        std::uint32_t /*group_count_x*/,
        std::uint32_t /*group_count_y*/,
        std::uint32_t /*group_count_z*/) override {
        if (const auto s = require_recording("dispatch_compute"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: dispatch_compute cannot run inside render pass");
        }
        return Status::success();
    }

    void mark_submitted() noexcept {
        if (state_ == State::executable) {
            state_ = State::submitted;
        }
    }

    [[nodiscard]] bool can_schedule_present() const noexcept {
        return state_ == State::recording && !inRenderPass_;
    }

private:
    [[nodiscard]] Status require_recording(const char* op) const {
        if (state_ != State::recording) {
            return Status::failure(
                StatusCode::invalid_state,
                std::string{"Direct3DCommandBuffer: "} + op + " requires recording state");
        }
        return Status::success();
    }

    [[nodiscard]] Status require_render_pass(const char* op) const {
        if (const auto s = require_recording(op); !s.ok()) {
            return s;
        }
        if (!inRenderPass_) {
            return Status::failure(
                StatusCode::invalid_state,
                std::string{"Direct3DCommandBuffer: "} + op + " requires active render pass");
        }
        return Status::success();
    }

    State state_ = State::initial;
    bool inRenderPass_ = false;
};

class Direct3DQueue final : public IQueue {
public:
    explicit Direct3DQueue(QueueKind kind) : kind_(kind) {}

    [[nodiscard]] QueueKind kind() const noexcept override { return kind_; }

    [[nodiscard]] Status submit(ICommandBuffer& command_buffer,
                                IFence* signal_fence = nullptr) override {
        if (!command_buffer.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DQueue: command buffer is not ready for submit");
        }

        if (auto* cmd = dynamic_cast<Direct3DCommandBuffer*>(&command_buffer)) {
            cmd->mark_submitted();
        }

        if (signal_fence) {
            if (auto* fence = dynamic_cast<Direct3DFence*>(signal_fence)) {
                fence->signal();
            }
        }

        return Status::success();
    }

private:
    QueueKind kind_;
};

class Direct3DBuffer final : public IBuffer {
public:
    explicit Direct3DBuffer(BufferDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const BufferDesc& desc() const noexcept override { return desc_; }

private:
    BufferDesc desc_;
};

class Direct3DTexture final : public ITexture {
public:
    explicit Direct3DTexture(TextureDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const TextureDesc& desc() const noexcept override { return desc_; }

private:
    TextureDesc desc_;
};

class Direct3DSampler final : public ISampler {
public:
    Direct3DSampler() = default;
};

class Direct3DShader final : public IShader {
public:
    explicit Direct3DShader(ShaderDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] const ShaderDesc& desc() const noexcept { return desc_; }

private:
    ShaderDesc desc_;
};

class Direct3DPipelineReflection final : public IPipelineReflection {
public:
    Direct3DPipelineReflection() = default;

    void add_binding(ResourceBinding binding) {
        bindings_.push_back(std::move(binding));
    }

    [[nodiscard]] std::size_t get_binding_count() const noexcept override {
        return bindings_.size();
    }

    [[nodiscard]] const ResourceBinding& get_binding_info(std::size_t index) const override {
        return bindings_.at(index);
    }

    [[nodiscard]] const ResourceBinding* find_binding(
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

class Direct3DPipeline final : public IPipeline {
public:
    Direct3DPipeline(PipelineDesc desc,
                   std::unique_ptr<Direct3DPipelineReflection> reflection)
        : desc_(std::move(desc)), reflection_(std::move(reflection)) {}

    [[nodiscard]] const PipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override {
        return reflection_.get();
    }

private:
    PipelineDesc desc_;
    std::unique_ptr<Direct3DPipelineReflection> reflection_;
};

class Direct3DComputePipeline final : public IComputePipeline {
public:
    Direct3DComputePipeline(ComputePipelineDesc desc,
                          std::unique_ptr<Direct3DPipelineReflection> reflection)
        : desc_(std::move(desc)), reflection_(std::move(reflection)) {}

    [[nodiscard]] const ComputePipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override {
        return reflection_.get();
    }

private:
    ComputePipelineDesc desc_;
    std::unique_ptr<Direct3DPipelineReflection> reflection_;
};

class Direct3DSurface final : public ISurface {
public:
    explicit Direct3DSurface(SurfaceDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] const SurfaceDesc& desc() const noexcept override { return desc_; }

private:
    SurfaceDesc desc_;
};

class Direct3DSwapchain final : public ISwapchain {
public:
    explicit Direct3DSwapchain(SwapchainDesc desc) : desc_(std::move(desc)) {
        if (desc_.imageCount == 0) {
            desc_.imageCount = desc_.framesInFlight;
        }
    }

    [[nodiscard]] const SwapchainDesc& desc() const noexcept override { return desc_; }

    [[nodiscard]] Status resize(Extent2D extent) override {
        if (extent.width == 0 || extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: swapchain extent must be non-zero");
        }
        desc_.extent = extent;
        drawable_.reset();
        acquired_ = false;
        currentImageIndex_ = 0;
        nextImageIndex_ = 0;
        return Status::success();
    }

    [[nodiscard]] std::uint32_t image_count() const noexcept override {
        return effective_swapchain_image_count(desc_);
    }

    [[nodiscard]] std::uint32_t current_image_index() const noexcept override {
        return currentImageIndex_;
    }

    [[nodiscard]] bool has_acquired_texture() const noexcept override {
        return acquired_;
    }

    [[nodiscard]] SwapchainAcquireResult acquire_next_texture_result() override {
        if (!drawable_ || drawable_->desc().extent.width != desc_.extent.width ||
            drawable_->desc().extent.height != desc_.extent.height) {
            drawable_ = std::make_unique<Direct3DTexture>(TextureDesc{
                .extent = desc_.extent,
                .format = desc_.format,
                .debugName = "direct3d_swapchain_drawable",
            });
        }
        currentImageIndex_ = nextImageIndex_;
        nextImageIndex_ = (nextImageIndex_ + 1) % image_count();
        acquired_ = true;
        return {
            .texture = drawable_.get(),
            .imageIndex = currentImageIndex_,
        };
    }

    [[nodiscard]] Status schedule_present(ICommandBuffer& cmd) override {
        // Milestone 3 behavior: headless no-op present path.
        auto* direct3DCmd = dynamic_cast<Direct3DCommandBuffer*>(&cmd);
        if (!direct3DCmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: schedule_present requires Direct3D command buffer");
        }
        if (!direct3DCmd->can_schedule_present()) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3D backend: schedule_present requires recording state outside render pass");
        }
        if (!acquired_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3D backend: schedule_present requires an acquired drawable");
        }
        acquired_ = false;
        return Status::success();
    }

private:
    SwapchainDesc                    desc_;
    std::unique_ptr<Direct3DTexture> drawable_;
    bool                             acquired_ = false;
    std::uint32_t                    currentImageIndex_ = 0;
    std::uint32_t                    nextImageIndex_ = 0;
};

class Direct3DFrameUploadRing final : public IFrameUploadRing {
public:
    Direct3DFrameUploadRing(std::uint32_t framesInFlight, std::size_t capacityPerFrame)
        : framesInFlight_(framesInFlight)
        , capacityPerFrame_(capacityPerFrame)
        , storage_(static_cast<std::size_t>(framesInFlight) * capacityPerFrame)
        , backingBuffer_(BufferDesc{
              .size = static_cast<std::size_t>(framesInFlight) * capacityPerFrame,
              .usage = BufferUsage::storage,
              .debugName = "direct3d_upload_ring",
          }) {}

    [[nodiscard]] FrameAllocation allocate(std::size_t size,
                                           std::size_t alignment = 16) override {
        std::size_t base = 0;
        if (!validation::align_up(frameOffset_, alignment, base)) {
            return {};
        }

        const std::size_t frameEnd =
            (static_cast<std::size_t>(currentFrame_) + 1) * capacityPerFrame_;
        if (!validation::range_fits(base, size, frameEnd)) {
            return {};
        }

        void* ptr = storage_.data() + base;
        frameOffset_ = base + size;
        return FrameAllocation{&backingBuffer_, base, ptr, size};
    }

    void advance() override {
        currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
        frameOffset_ = static_cast<std::size_t>(currentFrame_) * capacityPerFrame_;
    }

    [[nodiscard]] Status advance_if_ready(const IFence& completedFence) override {
        if (!completedFence.signaled()) {
            return Status::failure(StatusCode::timeout,
                                   "Direct3DFrameUploadRing: frame is not ready for reuse");
        }
        advance();
        return Status::success();
    }

    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept override {
        return framesInFlight_;
    }

    [[nodiscard]] std::size_t capacity_per_frame() const noexcept override {
        return capacityPerFrame_;
    }

    [[nodiscard]] std::uint32_t current_frame_index() const noexcept override {
        return currentFrame_;
    }

private:
    std::uint32_t framesInFlight_;
    std::size_t capacityPerFrame_;
    std::vector<std::byte> storage_;
    Direct3DBuffer backingBuffer_;
    std::uint32_t currentFrame_ = 0;
    std::size_t frameOffset_ = 0;
};

class Direct3DDevice final : public IDevice {
public:
    Direct3DDevice()
        : caps_(make_direct3d_capabilities())
        , graphicsQueue_(QueueKind::graphics)
        , computeQueue_(QueueKind::compute)
        , transferQueue_(QueueKind::transfer) {}

    [[nodiscard]] const Capabilities& capabilities() const noexcept override {
        return caps_;
    }

    [[nodiscard]] core::Result<std::unique_ptr<ISwapchain>> create_swapchain(
        ISurface& /*surface*/, const SwapchainDesc& desc) override {
        if (!validation::swapchain_supported(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                    "Direct3D backend: swapchain description is invalid");
        }
        return std::unique_ptr<ISwapchain>(std::make_unique<Direct3DSwapchain>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBuffer>> create_buffer(
        const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: buffer size must be non-zero");
        }
        if (desc.size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: buffer size exceeds device limit");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "Direct3D backend: buffer memory domain is not supported");
        }
        return std::unique_ptr<IBuffer>(std::make_unique<Direct3DBuffer>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<ITexture>> create_texture(
        const TextureDesc& desc) override {
        if (!validation::texture_shape_valid(
                desc, caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: texture extent exceeds device limits");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "Direct3D backend: texture memory domain is not supported");
        }
        if (!validation::texture_usage_supported_by_format(caps_, desc)) {
            return Status::failure(StatusCode::unsupported,
                                   "Direct3D backend: texture format does not support requested usage");
        }
        return std::unique_ptr<ITexture>(std::make_unique<Direct3DTexture>(desc));
    }
    
    [[nodiscard]] core::Result<std::unique_ptr<ISampler>> create_sampler(
        const SamplerDesc& /*desc*/) override {
        return std::unique_ptr<ISampler>(std::make_unique<Direct3DSampler>());
    }

    [[nodiscard]] core::Result<std::unique_ptr<IShader>> create_shader(
        const ShaderDesc& desc) override {
        if (!validation::shader_payload_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: shader descriptor payload is invalid");
        }
        if (!validation::shader_byte_format_supported(desc.byteFormat, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "Direct3D backend: shader byte format is not supported");
        }
        return std::unique_ptr<IShader>(std::make_unique<Direct3DShader>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IPipeline>> create_pipeline(
        const PipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline layout is invalid");
        }
        if (!validation::pipeline_render_state_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline render state is invalid");
        }
        if (!desc.vertexShader || !desc.fragmentShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline requires vertex and fragment shaders");
        }

        auto* vertexShader = dynamic_cast<Direct3DShader*>(desc.vertexShader);
        auto* fragmentShader = dynamic_cast<Direct3DShader*>(desc.fragmentShader);
        if (!vertexShader || !fragmentShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline shaders must be created by Direct3D backend");
        }

        auto reflection = std::make_unique<Direct3DPipelineReflection>();
        reflection->add_binding(ResourceBinding{
            .name = "vertex_buffer_0",
            .stage = ShaderStage::vertex,
            .type = ResourceBindingType::Buffer,
            .bindingIndex = 0,
            .dataSize = vertexShader->desc().bytecode.size(),
        });
        reflection->add_binding(ResourceBinding{
            .name = "fragment_buffer_1",
            .stage = ShaderStage::fragment,
            .type = ResourceBindingType::Buffer,
            .bindingIndex = 1,
            .dataSize = fragmentShader->desc().bytecode.size(),
        });

        return std::unique_ptr<IPipeline>(
            std::make_unique<Direct3DPipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IComputePipeline>> create_compute_pipeline(
        const ComputePipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: compute pipeline layout is invalid");
        }
        if (!desc.computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: compute pipeline requires compute shader");
        }

        auto* computeShader = dynamic_cast<Direct3DShader*>(desc.computeShader);
        if (!computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: compute shader must be created by Direct3D backend");
        }

        auto reflection = std::make_unique<Direct3DPipelineReflection>();
        reflection->add_binding(ResourceBinding{
            .name = "compute_buffer_0",
            .stage = ShaderStage::compute,
            .type = ResourceBindingType::Buffer,
            .bindingIndex = 0,
            .dataSize = computeShader->desc().bytecode.size(),
        });
        reflection->add_binding(ResourceBinding{
            .name = "compute_buffer_1",
            .stage = ShaderStage::compute,
            .type = ResourceBindingType::Buffer,
            .bindingIndex = 1,
            .dataSize = 0,
        });
        reflection->add_binding(ResourceBinding{
            .name = "compute_buffer_2",
            .stage = ShaderStage::compute,
            .type = ResourceBindingType::Buffer,
            .bindingIndex = 2,
            .dataSize = 0,
        });

        return std::unique_ptr<IComputePipeline>(
            std::make_unique<Direct3DComputePipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] CommandBufferPtr create_command_buffer() override {
        return CommandBufferPtr(new Direct3DCommandBuffer(), [](ICommandBuffer* cmd) {
            delete cmd;
        });
    }

    IQueue& queue(QueueKind kind) override {
        switch (kind) {
            case QueueKind::graphics: return graphicsQueue_;
            case QueueKind::compute: return computeQueue_;
            case QueueKind::transfer: return transferQueue_;
        }

        return graphicsQueue_;
    }

    [[nodiscard]] FencePtr create_fence(const FenceDesc& desc) override {
        return FencePtr(new Direct3DFence(desc), [](IFence* fence) {
            delete fence;
        });
    }


    [[nodiscard]] core::Result<std::unique_ptr<ISurface>> create_surface(
        const SurfaceDesc& desc) override {
        if (!validation::extent_within(desc.initialExtent,
                                       caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: surface extent exceeds device limits");
        }
        if (!validation::native_surface_handles_valid(desc.native)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: native surface handles are invalid for the surface kind");
        }
        if (!validation::native_surface_kind_supported(desc.native.kind, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "Direct3D backend: native surface kind is not supported");
        }
        return std::unique_ptr<ISurface>(std::make_unique<Direct3DSurface>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IFrameUploadRing>> create_upload_ring(
        std::uint32_t frames_in_flight, std::size_t buffer_size) override {
        if (!validation::frame_count_supported(frames_in_flight, caps_) ||
            buffer_size == 0 || buffer_size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                    "Direct3D backend: upload ring frames/capacity must be non-zero");
        }
        return std::unique_ptr<IFrameUploadRing>(
            std::make_unique<Direct3DFrameUploadRing>(frames_in_flight, buffer_size));
    }

private:
    Capabilities caps_{};
    Direct3DQueue graphicsQueue_;
    Direct3DQueue computeQueue_;
    Direct3DQueue transferQueue_;
};

class Direct3DBackend final : public IBackend {
public:
    Direct3DBackend() = default;

    BackendKind kind() const noexcept override { return BackendKind::direct3d; }

    std::vector<AdapterInfo> enumerate_adapters() const override {
        return {AdapterInfo{
            .id = 0,
            .name = "Direct3D Stub Adapter",
            .backend = BackendKind::direct3d,
            .capabilities = make_direct3d_capabilities(),
            .type = AdapterType::virtual_gpu,
            .driverDescription = "Direct3D contract adapter",
        }};
    }

    core::Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& desc) override {
        if (desc.adapterId != 0) {
            return Status::failure(StatusCode::unavailable,
                                   "Direct3D backend currently exposes one adapter (id=0)");
        }
        return std::unique_ptr<IDevice>(std::make_unique<Direct3DDevice>());
    }
};

} // namespace

std::unique_ptr<IBackend> create_direct3d_backend() {
    return std::make_unique<Direct3DBackend>();
}

} // namespace truffle::rhi
