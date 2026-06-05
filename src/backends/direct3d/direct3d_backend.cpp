#include "truffle/rhi/rhi.hpp"
#include "truffle/rhi/shader_reflection.hpp"
#include "truffle/rhi/validation.hpp"

#include "../backend_diagnostics.hpp"

#include <algorithm>
#include <algorithm>
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
            .maxSamplerAnisotropy = 16,
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

class Direct3DBindGroupLayout final : public IBindGroupLayout {
public:
    explicit Direct3DBindGroupLayout(BindGroupLayoutDesc desc)
        : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }

    [[nodiscard]] const BindGroupLayoutDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupLayoutDesc desc_;
};

class Direct3DBindGroup final : public IBindGroup {
public:
    explicit Direct3DBindGroup(BindGroupDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }

    [[nodiscard]] const BindGroupDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupDesc desc_;
};

class Direct3DCommandBuffer final : public ICommandBuffer {
public:
    explicit Direct3DCommandBuffer(BackendDiagnosticsPtr diagnostics)
        : diagnostics_(std::move(diagnostics)) {}

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
        if (debugLabelDepth_ != 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: cannot end while debug label is active");
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

    [[nodiscard]] Status push_debug_label(const DebugLabelDesc& desc) override {
        if (const auto s = require_recording("push_debug_label"); !s.ok()) {
            return s;
        }
        if (!validation::debug_label_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: debug label descriptor is invalid");
        }
        ++debugLabelDepth_;
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().debugLabelsPushed;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             desc.name, "debug label pushed");
        return Status::success();
    }

    [[nodiscard]] Status pop_debug_label() override {
        if (const auto s = require_recording("pop_debug_label"); !s.ok()) {
            return s;
        }
        if (debugLabelDepth_ == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: no active debug label to pop");
        }
        --debugLabelDepth_;
        return Status::success();
    }

    [[nodiscard]] Status insert_debug_marker(const DebugLabelDesc& desc) override {
        if (const auto s = require_recording("insert_debug_marker"); !s.ok()) {
            return s;
        }
        if (!validation::debug_label_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: debug marker descriptor is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().debugMarkersInserted;
        }
        record_backend_event(diagnostics_, BackendEventKind::debug_marker,
                             desc.name, "debug marker inserted");
        return Status::success();
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
            desc.colorAttachment.texture->backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: color attachment texture must be created by Direct3D backend");
        }
        if (desc.colorAttachment.texture &&
            !validation::texture_supports_usage(
                desc.colorAttachment.texture->desc(),
                TextureUsageFlags::color_attachment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: color attachment texture lacks color attachment usage");
        }
        if (desc.depthAttachment.texture &&
            desc.depthAttachment.texture->backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: depth attachment texture must be created by Direct3D backend");
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

    [[nodiscard]] Status bind_pipeline(IPipeline& pipeline) override {
        if (const auto s = require_render_pass("bind_pipeline"); !s.ok()) {
            return s;
        }
        if (pipeline.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: pipeline must be created by Direct3D backend");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_compute_pipeline(IComputePipeline& pipeline) override {
        if (const auto s = require_recording("bind_compute_pipeline"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "Direct3DCommandBuffer: bind_compute_pipeline cannot run inside render pass");
        }
        if (pipeline.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: compute pipeline must be created by Direct3D backend");
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
        if (buffer.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: vertex buffer must be created by Direct3D backend");
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
        if (buffer.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: index buffer must be created by Direct3D backend");
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
        if (buffer.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: uniform buffer must be created by Direct3D backend");
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
        if (buffer.backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: storage buffer must be created by Direct3D backend");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_group(std::uint32_t /*groupIndex*/,
                                    IBindGroup& group) override {
        if (const auto s = require_recording("bind_group"); !s.ok()) {
            return s;
        }
        if (!dynamic_cast<Direct3DBindGroup*>(&group)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: bind group must be created by Direct3D backend");
        }
        if (!validation::bind_group_desc_valid(group.desc())) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: bind group descriptor is invalid");
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
        if (barrier.buffer->backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: buffer barrier resource must be created by Direct3D backend");
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
        if (barrier.texture->backend_kind() != BackendKind::direct3d) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3DCommandBuffer: texture barrier resource must be created by Direct3D backend");
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
        if (const auto s = require_render_pass("draw"); !s.ok()) {
            return s;
        }
        record_draw();
        return Status::success();
    }

    [[nodiscard]] Status draw_instanced(
        std::uint32_t /*vertex_count*/, std::uint32_t /*instance_count*/) override {
        if (const auto s = require_render_pass("draw_instanced"); !s.ok()) {
            return s;
        }
        record_draw();
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed(std::uint32_t /*index_count*/) override {
        if (const auto s = require_render_pass("draw_indexed"); !s.ok()) {
            return s;
        }
        record_draw();
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed_instanced(
        std::uint32_t /*index_count*/, std::uint32_t /*instance_count*/) override {
        if (const auto s = require_render_pass("draw_indexed_instanced"); !s.ok()) {
            return s;
        }
        record_draw();
        return Status::success();
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
        record_draw();
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
        record_draw();
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().dispatchesRecorded;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             {}, "dispatch recorded");
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

    void record_draw() {
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().drawsRecorded;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_recorded,
                             {}, "draw recorded");
    }

    State state_ = State::initial;
    bool inRenderPass_ = false;
    std::uint32_t debugLabelDepth_ = 0;
    BackendDiagnosticsPtr diagnostics_;
};

class Direct3DQueue final : public IQueue {
public:
    Direct3DQueue(QueueKind kind, BackendDiagnosticsPtr diagnostics)
        : kind_(kind), diagnostics_(std::move(diagnostics)) {}

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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().submissions;
        }
        record_backend_event(diagnostics_, BackendEventKind::submitted,
                             {}, "command buffer submitted");

        if (signal_fence) {
            if (auto* fence = dynamic_cast<Direct3DFence*>(signal_fence)) {
                fence->signal();
            }
        }

        return Status::success();
    }

private:
    QueueKind kind_;
    BackendDiagnosticsPtr diagnostics_;
};

class Direct3DBuffer final : public IBuffer {
public:
    explicit Direct3DBuffer(BufferDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }
    [[nodiscard]] const BufferDesc& desc() const noexcept override { return desc_; }

private:
    BufferDesc desc_;
};

class Direct3DTexture final : public ITexture {
public:
    explicit Direct3DTexture(TextureDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }
    [[nodiscard]] const TextureDesc& desc() const noexcept override { return desc_; }

private:
    TextureDesc desc_;
};

class Direct3DSampler final : public ISampler {
public:
    explicit Direct3DSampler(SamplerDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }
    [[nodiscard]] const SamplerDesc& desc() const noexcept override { return desc_; }

private:
    SamplerDesc desc_;
};

class Direct3DShader final : public IShader {
public:
    explicit Direct3DShader(ShaderDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }

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

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }

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

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::direct3d;
    }

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
    explicit Direct3DDevice(BackendDiagnosticsPtr diagnostics)
        : caps_(make_direct3d_capabilities())
        , graphicsQueue_(QueueKind::graphics, diagnostics)
        , computeQueue_(QueueKind::compute, diagnostics)
        , transferQueue_(QueueKind::transfer, diagnostics)
        , diagnostics_(std::move(diagnostics)) {}

    [[nodiscard]] const Capabilities& capabilities() const noexcept override {
        return caps_;
    }

    [[nodiscard]] core::Result<std::unique_ptr<ISwapchain>> create_swapchain(
        ISurface& /*surface*/, const SwapchainDesc& desc) override {
        if (!validation::swapchain_supported(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                    "Direct3D backend: swapchain description is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().swapchainsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::swapchain_created,
                             {}, "swapchain created");
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().buffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "buffer created");
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().texturesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "texture created");
        return std::unique_ptr<ITexture>(std::make_unique<Direct3DTexture>(desc));
    }
    
    [[nodiscard]] core::Result<std::unique_ptr<ISampler>> create_sampler(
        const SamplerDesc& desc) override {
        if (!validation::sampler_desc_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "sampler descriptor is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().samplersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "sampler created");
        return std::unique_ptr<ISampler>(std::make_unique<Direct3DSampler>(desc));
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().shadersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.entryPoint, "shader created");
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
        if ((desc.vertexShader == nullptr) != (desc.fragmentShader == nullptr)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline requires both vertex and fragment shaders or neither");
        }
        auto* vertexShader =
            desc.vertexShader ? dynamic_cast<Direct3DShader*>(desc.vertexShader) : nullptr;
        auto* fragmentShader =
            desc.fragmentShader ? dynamic_cast<Direct3DShader*>(desc.fragmentShader) : nullptr;
        if ((desc.vertexShader && !vertexShader) ||
            (desc.fragmentShader && !fragmentShader)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline shaders must be created by Direct3D backend");
        }
        if ((vertexShader && vertexShader->desc().stage != ShaderStage::vertex) ||
            (fragmentShader && fragmentShader->desc().stage != ShaderStage::fragment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: pipeline shader stages are invalid");
        }

        auto reflection = std::make_unique<Direct3DPipelineReflection>();
        if (vertexShader) {
            reflection->add_binding(ResourceBinding{
                .name = "vertex_buffer_0",
                .stage = ShaderStage::vertex,
                .type = ResourceBindingType::Buffer,
                .bindingIndex = 0,
                .dataSize = vertexShader->desc().bytecode.size(),
            });
        }
        if (fragmentShader) {
            reflection->add_binding(ResourceBinding{
                .name = "fragment_buffer_1",
                .stage = ShaderStage::fragment,
                .type = ResourceBindingType::Buffer,
                .bindingIndex = 1,
                .dataSize = fragmentShader->desc().bytecode.size(),
            });
        }

        if (diagnostics_) {
            ++diagnostics_->mutable_stats().graphicsPipelinesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::pipeline_created,
                             desc.debugName, "graphics pipeline created");
        return std::unique_ptr<IPipeline>(
            std::make_unique<Direct3DPipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBindGroupLayout>>
    create_bind_group_layout(const BindGroupLayoutDesc& desc) override {
        if (!validation::bind_group_layout_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: bind group layout is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupLayoutsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group layout created");
        return std::unique_ptr<IBindGroupLayout>(
            std::make_unique<Direct3DBindGroupLayout>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBindGroup>>
    create_bind_group(const BindGroupDesc& desc) override {
        if (!validation::bind_group_desc_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: bind group descriptor is invalid");
        }
        if (!dynamic_cast<Direct3DBindGroupLayout*>(desc.layout)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: bind group layout must be created by Direct3D backend");
        }
        for (const auto& entry : desc.entries) {
            switch (entry.type) {
                case BindingResourceType::uniform_buffer:
                case BindingResourceType::storage_buffer:
                    if ((entry.buffer.buffer &&
                         !dynamic_cast<Direct3DBuffer*>(entry.buffer.buffer)) ||
                        std::any_of(entry.buffers.begin(), entry.buffers.end(),
                                    [](const BufferBindingDesc& binding) {
                                        return !dynamic_cast<Direct3DBuffer*>(binding.buffer);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "Direct3D backend: bind group buffer must be created by Direct3D backend");
                    }
                    break;
                case BindingResourceType::sampled_texture:
                case BindingResourceType::storage_texture:
                    if ((entry.texture &&
                         !dynamic_cast<Direct3DTexture*>(entry.texture)) ||
                        std::any_of(entry.textures.begin(), entry.textures.end(),
                                    [](const ITexture* texture) {
                                        return !dynamic_cast<const Direct3DTexture*>(texture);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "Direct3D backend: bind group texture must be created by Direct3D backend");
                    }
                    break;
                case BindingResourceType::sampler:
                    if ((entry.sampler &&
                         !dynamic_cast<Direct3DSampler*>(entry.sampler)) ||
                        std::any_of(entry.samplers.begin(), entry.samplers.end(),
                                    [](const ISampler* sampler) {
                                        return !dynamic_cast<const Direct3DSampler*>(sampler);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "Direct3D backend: bind group sampler must be created by Direct3D backend");
                    }
                    break;
            }
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group created");
        return std::unique_ptr<IBindGroup>(std::make_unique<Direct3DBindGroup>(desc));
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
        if (computeShader->desc().stage != ShaderStage::compute) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Direct3D backend: compute pipeline requires compute shader stage");
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

        if (diagnostics_) {
            ++diagnostics_->mutable_stats().computePipelinesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::pipeline_created,
                             desc.debugName, "compute pipeline created");
        return std::unique_ptr<IComputePipeline>(
            std::make_unique<Direct3DComputePipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] CommandBufferPtr create_command_buffer() override {
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().commandBuffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_buffer_created,
                             {}, "command buffer created");
        return CommandBufferPtr(new Direct3DCommandBuffer(diagnostics_), [](ICommandBuffer* cmd) {
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().fencesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::fence_created,
                             {}, "fence created");
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
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().surfacesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::surface_created,
                             {}, "surface created");
        return std::unique_ptr<ISurface>(std::make_unique<Direct3DSurface>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IFrameUploadRing>> create_upload_ring(
        std::uint32_t frames_in_flight, std::size_t buffer_size) override {
        if (!validation::frame_count_supported(frames_in_flight, caps_) ||
            buffer_size == 0 || buffer_size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                    "Direct3D backend: upload ring frames/capacity must be non-zero");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().uploadRingsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::upload_ring_created,
                             {}, "upload ring created");
        return std::unique_ptr<IFrameUploadRing>(
            std::make_unique<Direct3DFrameUploadRing>(frames_in_flight, buffer_size));
    }

private:
    Capabilities caps_{};
    Direct3DQueue graphicsQueue_;
    Direct3DQueue computeQueue_;
    Direct3DQueue transferQueue_;
    BackendDiagnosticsPtr diagnostics_;
};

class Direct3DBackend final : public IBackend {
public:
    Direct3DBackend() = default;

    BackendKind kind() const noexcept override { return BackendKind::direct3d; }

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
        ++diagnostics_->mutable_stats().devicesCreated;
        record_backend_event(diagnostics_, BackendEventKind::device_created,
                             {}, "device created");
        return std::unique_ptr<IDevice>(std::make_unique<Direct3DDevice>(diagnostics_));
    }

private:
    BackendDiagnosticsPtr diagnostics_ =
        make_backend_diagnostics(BackendKind::direct3d);
};

} // namespace

std::unique_ptr<IBackend> create_direct3d_backend() {
    return std::make_unique<Direct3DBackend>();
}

} // namespace truffle::rhi
