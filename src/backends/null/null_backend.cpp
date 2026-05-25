#include "truffle/rhi/null_backend.hpp"

#include <memory>
#include <utility>

namespace truffle::rhi {
namespace {

using core::Result;
using core::Status;
using core::StatusCode;

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

private:
    PipelineDesc desc_;
};

class NullComputePipeline final : public IComputePipeline {
public:
    explicit NullComputePipeline(ComputePipelineDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const ComputePipelineDesc& desc() const noexcept override { return desc_; }

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
    explicit NullSwapchain(SwapchainDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] const SwapchainDesc& desc() const noexcept override { return desc_; }

    [[nodiscard]] Status resize(Extent2D extent) override {
        if (extent.width == 0 || extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "swapchain extent must be non-zero");
        }
        desc_.extent = extent;
        return Status::success();
    }

    [[nodiscard]] ITexture* acquire_next_texture() override {
        if (!drawable_) {
            drawable_ = std::make_unique<NullTexture>(TextureDesc{
                .extent    = desc_.extent,
                .format    = desc_.format,
                .debugName = "swapchain_drawable",
            });
        }
        return drawable_.get();
    }

    [[nodiscard]] Status schedule_present(ICommandBuffer& /*cmd*/) override {
        return Status::success();
    }

private:
    SwapchainDesc                desc_;
    std::unique_ptr<NullTexture> drawable_;
};

class NullFence final : public IFence {
public:
    explicit NullFence(FenceDesc desc) : signaled_(desc.signaled) {}

    [[nodiscard]] bool signaled() const noexcept override { return signaled_; }
    void signal() noexcept { signaled_ = true; }
    void wait() noexcept override { /* already signaled synchronously before submit returns */ }

    // Allows pooling logic to update initial state
    void reset(bool is_signaled) { signaled_ = is_signaled; }

    NullDevice* device_ = nullptr;
private:
    bool signaled_ = false;
};

class NullCommandBuffer final : public ICommandBuffer {
public:
    explicit NullCommandBuffer(std::shared_ptr<SharedStats> stats)
        : stats_(std::move(stats)) {}

    void reset() { state_ = State::initial; } // For pooling

    NullDevice* device_ = nullptr;

    [[nodiscard]] Status begin() override {
        if (state_ != State::initial) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer can begin once");
        }
        state_ = State::recording;
        return Status::success();
    }

    [[nodiscard]] Status begin_render_pass(
        const RenderPassDesc& /*desc*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "begin_render_pass requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status end_render_pass() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "end_render_pass requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_pipeline(IPipeline& /*pipeline*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_pipeline requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_compute_pipeline(IComputePipeline& /*pipeline*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_compute_pipeline requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_vertex_buffer(std::uint32_t /*binding*/,
                                             IBuffer& /*buffer*/,
                                             std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_vertex_buffer requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_index_buffer(IBuffer& /*buffer*/,
                                            std::size_t /*offset*/,
                                            IndexFormat /*format*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_index_buffer requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_uniform_buffer(std::uint32_t /*binding*/,
                                              IBuffer& /*buffer*/,
                                              std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_uniform_buffer requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_storage_buffer(std::uint32_t /*binding*/,
                                              IBuffer& /*buffer*/,
                                              std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind_storage_buffer requires recording");
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
        return Status::success();
    }

    [[nodiscard]] Status set_scissor(std::uint32_t /*x*/, std::uint32_t /*y*/,
                                      std::uint32_t /*width*/,
                                      std::uint32_t /*height*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "set_scissor requires recording");
        }
        return Status::success();
    }

    [[nodiscard]] Status draw(std::uint32_t vertex_count) override {
        if (state_ != State::recording || vertex_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires a recording command buffer");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_instanced(std::uint32_t vertex_count,
                                        std::uint32_t instance_count) override {
        if (state_ != State::recording || vertex_count == 0 || instance_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_instanced requires a recording command buffer");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed(std::uint32_t index_count) override {
        return draw_indexed_instanced(index_count, 1);
    }

    [[nodiscard]] Status draw_indexed_instanced(std::uint32_t index_count,
                                                std::uint32_t instance_count) override {
        if (state_ != State::recording || index_count == 0 || instance_count == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_instanced requires a recording command buffer");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }
    [[nodiscard]] Status draw_indirect(IBuffer& /*indirect_buffer*/,
                                        std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indirect failed validation");
        }
        ++stats_->value.drawsRecorded;
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed_indirect(IBuffer& /*indirect_buffer*/,
                                                std::size_t /*offset*/) override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw_indexed_indirect failed validation");
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
        ++stats_->value.drawsRecorded; // Re-use draw counter for now
        return Status::success();
    }
    [[nodiscard]] Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not recording");
        }
        state_ = State::ready;
        return Status::success();
    }

    [[nodiscard]] bool ready_for_submit() const noexcept override {
        return state_ == State::ready;
    }

private:
    enum class State { initial, recording, ready };
    std::shared_ptr<SharedStats> stats_;
    State state_ = State::initial;
};

class NullQueue final : public IQueue {
public:
    explicit NullQueue(std::shared_ptr<SharedStats> stats) : stats_(std::move(stats)) {}

    [[nodiscard]] QueueKind kind() const noexcept override {
        return QueueKind::graphics;
    }

    [[nodiscard]] Status submit(ICommandBuffer& command_buffer,
                                IFence* signal_fence) override {
        if (!command_buffer.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                   "command buffer is not ready for submit");
        }
        ++stats_->value.submissions;
        if (auto* fence = dynamic_cast<NullFence*>(signal_fence)) {
            fence->signal();
        }
        return Status::success();
    }

private:
    std::shared_ptr<SharedStats> stats_;
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
        std::size_t base = frameOffset_;
        if (alignment > 1) {
            base = (base + alignment - 1) & ~(alignment - 1);
        }
        const std::size_t frameEnd =
            (static_cast<std::size_t>(currentFrame_) + 1) * capacityPerFrame_;
        if (base + size > frameEnd || size == 0) {
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

    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept override {
        return framesInFlight_;
    }

    [[nodiscard]] std::size_t capacity_per_frame() const noexcept override {
        return capacityPerFrame_;
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
        : stats_(std::move(stats)), queue_(stats_) {}

    [[nodiscard]] const Capabilities& capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] IQueue& queue(QueueKind) override { return queue_; }

    [[nodiscard]] Result<std::unique_ptr<IBuffer>>
    create_buffer(const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer size must be non-zero");
        }
        ++stats_->value.buffersCreated;
        return std::unique_ptr<IBuffer>(std::make_unique<NullBuffer>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ITexture>>
    create_texture(const TextureDesc& desc) override {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture extent must be non-zero");
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
        if (desc.bytecode.empty()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader bytecode must be present");
        }
        return std::unique_ptr<IShader>(std::make_unique<NullShader>());
    }

    [[nodiscard]] Result<std::unique_ptr<IPipeline>>
    create_pipeline(const PipelineDesc& desc) override {
        return std::unique_ptr<IPipeline>(std::make_unique<NullPipeline>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<IComputePipeline>>
    create_compute_pipeline(const ComputePipelineDesc& desc) override {
        return std::unique_ptr<IComputePipeline>(std::make_unique<NullComputePipeline>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ISurface>>
    create_surface(const SurfaceDesc& desc) override {
        if (desc.initialExtent.width == 0 || desc.initialExtent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "surface extent must be non-zero");
        }
        ++stats_->value.surfacesCreated;
        return std::unique_ptr<ISurface>(std::make_unique<NullSurface>(desc));
    }

    [[nodiscard]] Result<std::unique_ptr<ISwapchain>>
    create_swapchain(ISurface&, const SwapchainDesc& desc) override {
        if (desc.extent.width == 0 || desc.extent.height == 0 ||
            desc.framesInFlight == 0) {
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
            f->reset(desc.signaled);
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
        if (frames_in_flight == 0 || capacity_per_frame == 0) {
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
    Capabilities capabilities_{true, true, 2};
    NullQueue queue_;
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
        return {{0, "Truffle Null Adapter", BackendKind::null_backend,
                 Capabilities{true, true, 2}}};
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
