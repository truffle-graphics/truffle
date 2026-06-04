#include "truffle/rhi/null_backend.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/rhi/validation.hpp"

#include <memory>
#include <utility>

namespace truffle::rhi {
namespace {

using core::Result;
using core::Status;
using core::StatusCode;

[[nodiscard]] Capabilities make_null_capabilities() {
    return Capabilities{
        .presentation = true,
        .validation = true,
        .maxFramesInFlight = 3,
        .queues = {.graphics = true, .compute = true, .transfer = true},
        .features = {
            .headlessSurface = true,
            .nativeSurface = false,
            .presentation = true,
            .compute = true,
            .indirectDraw = true,
            .shaderReflection = false,
            .debugLabels = true,
            .validation = true,
            .unifiedMemory = true,
        },
        .limits = {
            .maxTextureDimension2D = 16384,
            .maxBufferSize = 256ull * 1024ull * 1024ull,
            .minUniformBufferOffsetAlignment = 16,
            .minStorageBufferOffsetAlignment = 16,
            .maxColorAttachments = 8,
            .maxVertexBuffers = 16,
        },
        .formats = {
            {.format = TextureFormat::rgba8_unorm,
             .sampled = true,
             .colorAttachment = true,
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
            {.kind = MemoryHeapKind::unified,
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
            ShaderByteFormat::msl_source,
            ShaderByteFormat::spirv_binary,
            ShaderByteFormat::glsl_source,
            ShaderByteFormat::hlsl_source,
            ShaderByteFormat::dxil_binary,
        },
    };
}

struct SharedStats {
    NullBackendStats value;
};
class NullDevice;
class NullBuffer final : public IBuffer {
public:
    explicit NullBuffer(BufferDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const BufferDesc& desc() const noexcept override { return desc_; }

private:
    BufferDesc desc_;
};

class NullTexture final : public ITexture {
public:
    explicit NullTexture(TextureDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const TextureDesc& desc() const noexcept override { return desc_; }

private:
    TextureDesc desc_;
};

class NullSampler final : public ISampler {};
class NullShader final : public IShader {};

class NullPipeline final : public IPipeline {
public:
    explicit NullPipeline(PipelineDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const PipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override { return nullptr; }

private:
    PipelineDesc desc_;
};

class NullComputePipeline final : public IComputePipeline {
public:
    explicit NullComputePipeline(ComputePipelineDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const ComputePipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override { return nullptr; }

private:
    ComputePipelineDesc desc_;
};

class NullSurface final : public ISurface {
public:
    explicit NullSurface(SurfaceDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const SurfaceDesc& desc() const noexcept override { return desc_; }

private:
    SurfaceDesc desc_;
};

class NullSwapchain final : public ISwapchain {
public:
    explicit NullSwapchain(SwapchainDesc desc) : desc_(std::move(desc)) {
        if (desc_.imageCount == 0) {
            desc_.imageCount = desc_.framesInFlight;
        }
    }
    [[nodiscard]] const SwapchainDesc& desc() const noexcept override { return desc_; }

    [[nodiscard]] Status resize(Extent2D extent) override {
        if (extent.width == 0 || extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain extent must be non-zero");
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
            drawable_ = std::make_unique<NullTexture>(TextureDesc{
                .extent    = desc_.extent,
                .format    = desc_.format,
                .debugName = "swapchain_drawable",
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

    [[nodiscard]] Status schedule_present(ICommandBuffer& cmd) override;

private:
    SwapchainDesc                desc_;
    std::unique_ptr<NullTexture> drawable_;
    bool                         acquired_ = false;
    std::uint32_t                currentImageIndex_ = 0;
    std::uint32_t                nextImageIndex_ = 0;
};

class NullFence final : public IFence {
public:
    explicit NullFence(FenceDesc desc)
        : value_(desc.signaled && desc.initialValue == 0 ? 1 : desc.initialValue) {}

    [[nodiscard]] bool signaled() const noexcept override { return value_ != 0; }
    [[nodiscard]] std::uint64_t value() const noexcept override { return value_; }
    void signal() noexcept { ++value_; }
    [[nodiscard]] Status wait_for(std::uint64_t /*timeoutNanoseconds*/) noexcept override {
        if (!signaled()) {
            return Status::failure(StatusCode::timeout, "fence wait timed out");
        }
        return Status::success();
    }
    [[nodiscard]] Status wait_for_value(
        std::uint64_t targetValue,
        std::uint64_t /*timeoutNanoseconds*/) noexcept override {
        if (value_ < targetValue) {
            return Status::failure(StatusCode::timeout,
                                   "fence timeline wait timed out");
        }
        return Status::success();
    }
    [[nodiscard]] Status reset() noexcept override {
        value_ = 0;
        return Status::success();
    }
    void wait() noexcept override { /* already signaled synchronously before submit returns */ }

    // Allows pooling logic to update initial state
    void reset(FenceDesc desc) {
        value_ = desc.signaled && desc.initialValue == 0 ? 1 : desc.initialValue;
    }

    NullDevice* device_ = nullptr;
private:
    std::uint64_t value_ = 0;
};

class NullCommandBuffer final : public ICommandBuffer {
public:
    explicit NullCommandBuffer(std::shared_ptr<SharedStats> stats)
        : stats_(std::move(stats)) {}

    void reset() {
        state_ = State::initial;
        inRenderPass_ = false;
    }

    NullDevice* device_ = nullptr;

    [[nodiscard]] bool can_schedule_present() const noexcept {
        return state_ == State::recording && !inRenderPass_;
    }

    [[nodiscard]] Status begin() override {
        if (state_ != State::initial) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer can begin once");
        }
        state_ = State::recording;
        return Status::success();
    }

    [[nodiscard]] Status begin_render_pass(const RenderPassDesc& desc) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "begin_render_pass requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "render pass already active");
        }
        if (!validation::is_non_zero(desc.extent)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "render pass extent must be non-zero");
        }
        if (desc.colorAttachment.texture &&
            !validation::texture_supports_usage(
                desc.colorAttachment.texture->desc(),
                TextureUsageFlags::color_attachment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "color attachment texture lacks color attachment usage");
        }
        if (desc.depthAttachment.texture &&
            !validation::texture_supports_usage(
                desc.depthAttachment.texture->desc(),
                TextureUsageFlags::depth_stencil)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "depth attachment texture lacks depth usage");
        }
        inRenderPass_ = true;
        return Status::success();
    }

    [[nodiscard]] Status end_render_pass() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "end_render_pass requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "no active render pass");
        }
        inRenderPass_ = false;
        return Status::success();
    }

    [[nodiscard]] Status bind_pipeline(IPipeline& /*pipeline*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_pipeline requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_pipeline requires active render pass");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_compute_pipeline(IComputePipeline& /*pipeline*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_compute_pipeline requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_compute_pipeline cannot run inside render pass");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_vertex_buffer(std::uint32_t /*binding*/,
                                             IBuffer& buffer,
                                             std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_vertex_buffer requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_vertex_buffer requires active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::vertex)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks vertex usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_index_buffer(IBuffer& buffer,
                                            std::size_t /*offset*/,
                                            IndexFormat /*format*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_index_buffer requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_index_buffer requires active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::index)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks index usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_uniform_buffer(std::uint32_t /*binding*/,
                                              IBuffer& buffer,
                                              std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_uniform_buffer requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_uniform_buffer requires active render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::uniform)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks uniform usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_storage_buffer(std::uint32_t /*binding*/,
                                               IBuffer& buffer,
                                               std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_storage_buffer requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_storage_buffer cannot run inside render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::storage)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks storage usage");
        }
        return Status::success();
    }

    [[nodiscard]] Status resource_barrier(
        const BufferBarrierDesc& barrier) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer barrier requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer barrier cannot run inside render pass");
        }
        if (!validation::buffer_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status resource_barrier(
        const TextureBarrierDesc& barrier) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "texture barrier requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "texture barrier cannot run inside render pass");
        }
        if (!validation::texture_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_viewport(float /*x*/, float /*y*/,
                                       float /*width*/, float /*height*/,
                                       float /*minDepth*/,
                                       float /*maxDepth*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_viewport requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_viewport requires active render pass");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_scissor(std::uint32_t /*x*/, std::uint32_t /*y*/,
                                      std::uint32_t /*width*/,
                                      std::uint32_t /*height*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_scissor requires recording");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_scissor requires active render pass");
        }
        return Status::success();
    }

    [[nodiscard]] Status draw(std::uint32_t vertex_count) override {
        if (state_ != State::recording || !inRenderPass_ || vertex_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires active render pass");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_instanced(std::uint32_t vertex_count,
                                        std::uint32_t instance_count) override {
        if (state_ != State::recording || !inRenderPass_ || vertex_count == 0 ||
            instance_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_instanced requires active render pass");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed(std::uint32_t index_count) override {
        return draw_indexed_instanced(index_count, 1);
    }

    [[nodiscard]] Status draw_indexed_instanced(std::uint32_t index_count,
                                                std::uint32_t instance_count) override {
        if (state_ != State::recording || !inRenderPass_ || index_count == 0 ||
            instance_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_instanced requires active render pass");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_indirect(IBuffer& indirect_buffer,
                                        std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect failed validation");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect requires active render pass");
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks indirect usage");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed_indirect(IBuffer& indirect_buffer,
                                                std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect failed validation");
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect requires active render pass");
        }
        if (!validation::buffer_supports_usage(indirect_buffer.desc(),
                                               BufferUsageFlags::indirect)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer lacks indirect usage");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status dispatch_compute(std::uint32_t /*group_count_x*/,
                                           std::uint32_t /*group_count_y*/,
                                           std::uint32_t /*group_count_z*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch_compute requires recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch_compute cannot run inside render pass");
        }
        ++stats_->value.drawsRecorded; // Re-use draw counter for now
        return Status::success();
    }

    [[nodiscard]] Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not recording");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer cannot end with active render pass");
        }
        state_ = State::ready;
        return Status::success();
    }

    [[nodiscard]] bool ready_for_submit() const noexcept override {
        return state_ == State::ready;
    }

    [[nodiscard]] CommandBufferState state() const noexcept override {
        switch (state_) {
            case State::initial: return CommandBufferState::initial;
            case State::recording: return CommandBufferState::recording;
            case State::ready: return CommandBufferState::executable;
            case State::submitted: return CommandBufferState::submitted;
        }
        return CommandBufferState::initial;
    }

    void mark_submitted() noexcept {
        if (state_ == State::ready) {
            state_ = State::submitted;
        }
    }

private:
    enum class State { initial, recording, ready, submitted };
    std::shared_ptr<SharedStats> stats_;
    State state_ = State::initial;
    bool inRenderPass_ = false;
};

Status NullSwapchain::schedule_present(ICommandBuffer& cmd) {
    auto* nullCmd = dynamic_cast<NullCommandBuffer*>(&cmd);
    if (!nullCmd) {
        return Status::failure(StatusCode::invalid_argument,
                               "schedule_present requires a null command buffer");
    }
    if (!nullCmd->can_schedule_present()) {
        return Status::failure(StatusCode::invalid_state,
                               "schedule_present requires recording state outside render pass");
    }
    if (!acquired_) {
        return Status::failure(StatusCode::invalid_state,
                               "schedule_present requires an acquired drawable");
    }
    acquired_ = false;
    return Status::success();
}

class NullQueue final : public IQueue {
public:
    NullQueue(std::shared_ptr<SharedStats> stats, QueueKind kind)
        : stats_(std::move(stats)), kind_(kind) {}

    [[nodiscard]] QueueKind kind() const noexcept override {
        return kind_;
    }

    [[nodiscard]] Status submit(ICommandBuffer& command_buffer,
                                IFence* signal_fence) override {
        auto* cmd = dynamic_cast<NullCommandBuffer*>(&command_buffer);
        if (!cmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "command buffer does not belong to null backend");
        }
        if (!command_buffer.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                    "command buffer is not ready for submit");
        }
        ++stats_->value.submissions;
        cmd->mark_submitted();
        if (signal_fence) {
            auto* fence = dynamic_cast<NullFence*>(signal_fence);
            if (!fence) {
                return Status::failure(StatusCode::invalid_argument,
                                       "signal fence does not belong to null backend");
            }
            fence->signal();
        }
        return Status::success();
    }

private:
    std::shared_ptr<SharedStats> stats_;
    QueueKind kind_ = QueueKind::graphics;
};

class NullFrameUploadRing final : public IFrameUploadRing {
public:
    NullFrameUploadRing(std::uint32_t framesInFlight, std::size_t capacityPerFrame)
        : framesInFlight_(framesInFlight)
        , capacityPerFrame_(capacityPerFrame)
        , storage_(static_cast<std::size_t>(framesInFlight) * capacityPerFrame)
        , backingBuffer_(BufferDesc{
              .size      = static_cast<std::size_t>(framesInFlight) * capacityPerFrame,
              .usage     = BufferUsage::storage,
              .debugName = "upload_ring",
          }) {}

    [[nodiscard]] FrameAllocation allocate(std::size_t size,
                                           std::size_t alignment) override {
        std::size_t base = 0;
        if (!validation::align_up(frameOffset_, alignment, base)) {
            return {};
        }
        const std::size_t frameEnd =
            (static_cast<std::size_t>(currentFrame_) + 1) * capacityPerFrame_;
        if (!validation::range_fits(base, size, frameEnd)) {
            return {};
        }
        void* ptr     = storage_.data() + base;
        frameOffset_  = base + size;
        return FrameAllocation{&backingBuffer_, base, ptr, size};
    }

    void advance() override {
        currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
        frameOffset_  = static_cast<std::size_t>(currentFrame_) * capacityPerFrame_;
    }

    [[nodiscard]] Status advance_if_ready(const IFence& completedFence) override {
        if (!completedFence.signaled()) {
            return Status::failure(StatusCode::timeout,
                                   "upload ring frame is not ready for reuse");
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
    std::uint32_t          framesInFlight_;
    std::size_t            capacityPerFrame_;
    std::vector<std::byte> storage_;
    NullBuffer             backingBuffer_;
    std::uint32_t          currentFrame_ = 0;
    std::size_t            frameOffset_  = 0;
};

class NullDevice final : public IDevice {
public:
    explicit NullDevice(std::shared_ptr<SharedStats> stats)
        : stats_(std::move(stats))
        , graphicsQueue_(stats_, QueueKind::graphics)
        , computeQueue_(stats_, QueueKind::compute)
        , transferQueue_(stats_, QueueKind::transfer) {}

    [[nodiscard]] const Capabilities& capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] IQueue& queue(QueueKind kind) override {
        switch (kind) {
            case QueueKind::graphics: return graphicsQueue_;
            case QueueKind::compute: return computeQueue_;
            case QueueKind::transfer: return transferQueue_;
        }
        return graphicsQueue_;
    }

    [[nodiscard]] Result<std::unique_ptr<IBuffer>>
    create_buffer(const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size must be non-zero");
        }
        if (desc.size > capabilities_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size exceeds device limit");
        }
        if (!validation::memory_domain_supported(desc.memory, capabilities_)) {
            return Status::failure(StatusCode::unsupported,
                                   "buffer memory domain is not supported");
        }
        ++stats_->value.buffersCreated;
        return std::unique_ptr<IBuffer>(std::make_unique<NullBuffer>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ITexture>>
    create_texture(const TextureDesc& desc) override {
        if (!validation::texture_shape_valid(
                desc, capabilities_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                    "texture extent exceeds device limits");
        }
        if (!validation::memory_domain_supported(desc.memory, capabilities_)) {
            return Status::failure(StatusCode::unsupported,
                                   "texture memory domain is not supported");
        }
        if (!validation::texture_usage_supported_by_format(capabilities_, desc)) {
            return Status::failure(StatusCode::unsupported,
                                   "texture format does not support requested usage");
        }
        ++stats_->value.texturesCreated;
        return std::unique_ptr<ITexture>(std::make_unique<NullTexture>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ISampler>>
    create_sampler(const SamplerDesc&) override {
        return std::unique_ptr<ISampler>(std::make_unique<NullSampler>());
    }

    [[nodiscard]] Result<std::unique_ptr<IShader>>
    create_shader(const ShaderDesc& desc) override {
        if (!validation::shader_payload_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader descriptor payload is invalid");
        }
        if (!validation::shader_byte_format_supported(desc.byteFormat, capabilities_)) {
            return Status::failure(StatusCode::unsupported,
                                   "shader byte format is not supported");
        }
        return std::unique_ptr<IShader>(std::make_unique<NullShader>());
    }

    [[nodiscard]] Result<std::unique_ptr<IPipeline>>
    create_pipeline(const PipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, capabilities_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "pipeline layout is invalid");
        }
        if (!validation::pipeline_render_state_valid(desc, capabilities_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "pipeline render state is invalid");
        }
        return std::unique_ptr<IPipeline>(std::make_unique<NullPipeline>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<IComputePipeline>>
    create_compute_pipeline(const ComputePipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, capabilities_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline layout is invalid");
        }
        return std::unique_ptr<IComputePipeline>(std::make_unique<NullComputePipeline>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ISurface>>
    create_surface(const SurfaceDesc& desc) override {
        if (!validation::extent_within(desc.initialExtent,
                                       capabilities_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface extent exceeds device limits");
        }
        if (!validation::native_surface_handles_valid(desc.native)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "native surface handles are invalid for the surface kind");
        }
        if (!validation::native_surface_kind_supported(desc.native.kind, capabilities_)) {
            return Status::failure(StatusCode::unsupported,
                                   "native surface kind is not supported by the backend");
        }
        ++stats_->value.surfacesCreated;
        return std::unique_ptr<ISurface>(std::make_unique<NullSurface>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ISwapchain>>
    create_swapchain(ISurface&, const SwapchainDesc& desc) override {
        if (!validation::swapchain_supported(desc, capabilities_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain description is invalid");
        }
        ++stats_->value.swapchainsCreated;
        return std::unique_ptr<ISwapchain>(std::make_unique<NullSwapchain>(desc));
    }

    [[nodiscard]] CommandBufferPtr create_command_buffer() override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        NullCommandBuffer* cmd = nullptr;
        if (!cmd_pool_.empty()) {
            cmd = cmd_pool_.back();
            cmd_pool_.pop_back();
        } else {
            ++stats_->value.commandBuffersCreated;
            cmd = new NullCommandBuffer(stats_);
            cmd->device_ = this; // need to add device_ to NullCommandBuffer
        }
        return CommandBufferPtr(cmd, [](ICommandBuffer* p) {
            auto* obj = static_cast<NullCommandBuffer*>(p);
            if (obj->device_) { obj->device_->recycle_command_buffer(obj); }
            else { delete obj; }
        });
    }

    [[nodiscard]] FencePtr create_fence(const FenceDesc& desc) override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        NullFence* f = nullptr;
        if (!fence_pool_.empty()) {
            f = fence_pool_.back();
            fence_pool_.pop_back();
            f->reset(desc);
        } else {
            f = new NullFence(desc);
            f->device_ = this;
        }
        return FencePtr(f, [](IFence* p) {
            auto* obj = static_cast<NullFence*>(p);
            if (obj->device_) { obj->device_->recycle_fence(obj); }
            else { delete obj; }
        });
    }

    [[nodiscard]] core::Result<std::unique_ptr<IFrameUploadRing>>
    create_upload_ring(std::uint32_t frames_in_flight,
                       std::size_t   capacity_per_frame) override {
        if (!validation::frame_count_supported(frames_in_flight, capabilities_) ||
            capacity_per_frame == 0 || capacity_per_frame > capabilities_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "frames_in_flight and capacity must be non-zero");
        }
        return std::unique_ptr<IFrameUploadRing>(
            std::make_unique<NullFrameUploadRing>(frames_in_flight, capacity_per_frame));
    }

    void recycle_command_buffer(NullCommandBuffer* cmd) {
        cmd->reset();
        std::lock_guard<std::mutex> lock(pool_mutex_);
        cmd_pool_.push_back(cmd);
    }

    void recycle_fence(NullFence* f) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        fence_pool_.push_back(f);
    }

    ~NullDevice() {
        for (auto* c : cmd_pool_) { delete c; }
        for (auto* f : fence_pool_) { delete f; }
    }

private:
    std::shared_ptr<SharedStats> stats_;
    Capabilities capabilities_ = make_null_capabilities();
    NullQueue graphicsQueue_;
    NullQueue computeQueue_;
    NullQueue transferQueue_;
    std::mutex pool_mutex_;
    std::vector<NullCommandBuffer*> cmd_pool_;
    std::vector<NullFence*> fence_pool_;
};

class NullBackend final : public INullBackend {
public:
    [[nodiscard]] BackendKind kind() const noexcept override {
        return BackendKind::null_backend;
    }

    [[nodiscard]] std::vector<AdapterInfo> enumerate_adapters() const override {
        return {AdapterInfo{
            .id = 0,
            .name = "Truffle Null Adapter",
            .backend = BackendKind::null_backend,
            .capabilities = make_null_capabilities(),
            .type = AdapterType::cpu,
            .driverDescription = "Strict CPU-backed RHI validation adapter",
        }};
    }

    [[nodiscard]] Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& desc) override {
        if (desc.adapterId != 0) {
            return Status::failure(StatusCode::unavailable,
                                   "null backend has one adapter");
        }
        return std::unique_ptr<IDevice>(std::make_unique<NullDevice>(stats_));
    }

    [[nodiscard]] NullBackendStats stats() const noexcept override {
        return stats_->value;
    }

private:
    std::shared_ptr<SharedStats> stats_ = std::make_shared<SharedStats>();
};

} // namespace

std::unique_ptr<INullBackend> create_null_backend() {
    return std::make_unique<NullBackend>();
}

} // namespace truffle::rhi
