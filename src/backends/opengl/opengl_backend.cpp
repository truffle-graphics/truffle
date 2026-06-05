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

[[nodiscard]] Capabilities make_opengl_capabilities() {
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
            .unifiedMemory = true,
        },
        .limits = {
            .maxTextureDimension2D = 16384,
            .maxBufferSize = 256ull * 1024ull * 1024ull,
            .minUniformBufferOffsetAlignment = 256,
            .minStorageBufferOffsetAlignment = 16,
            .maxColorAttachments = 8,
            .maxVertexBuffers = 16,
            .maxVertexAttributes = 16,
            .maxVertexBufferStride = 2048,
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
            {.format = TextureFormat::depth32_float_stencil8,
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
            ShaderByteFormat::glsl_source,
        },
    };
}

class OpenGLFence final : public IFence {
public:
    explicit OpenGLFence(FenceDesc desc)
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
                                   "OpenGLFence: wait timed out");
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

class OpenGLBindGroupLayout final : public IBindGroupLayout {
public:
    explicit OpenGLBindGroupLayout(BindGroupLayoutDesc desc)
        : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }

    [[nodiscard]] const BindGroupLayoutDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupLayoutDesc desc_;
};

class OpenGLBindGroup final : public IBindGroup {
public:
    explicit OpenGLBindGroup(BindGroupDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }

    [[nodiscard]] const BindGroupDesc& desc() const noexcept override {
        return desc_;
    }

private:
    BindGroupDesc desc_;
};

class OpenGLCommandBuffer final : public ICommandBuffer {
public:
    OpenGLCommandBuffer(BackendDiagnosticsPtr diagnostics, DeviceLimits limits)
        : limits_(limits), diagnostics_(std::move(diagnostics)) {}

    enum class State {
        initial,
        recording,
        executable,
        submitted,
    };

    [[nodiscard]] Status begin() override {
        if (state_ != State::initial) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: begin requires initial state");
        }
        state_ = State::recording;
        return Status::success();
    }

    [[nodiscard]] Status end() override {
        if (state_ != State::recording) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: end requires recording state");
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: cannot end while render pass is active");
        }
        if (debugLabelDepth_ != 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: cannot end while debug label is active");
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
                                   "OpenGLCommandBuffer: debug label descriptor is invalid");
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
                                   "OpenGLCommandBuffer: no active debug label to pop");
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
                                   "OpenGLCommandBuffer: debug marker descriptor is invalid");
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
                                   "OpenGLCommandBuffer: render pass already active");
        }
        if (!validation::render_pass_desc_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: render pass descriptor is invalid");
        }
        if (desc.colorAttachment.texture &&
            desc.colorAttachment.texture->backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: color attachment texture must be created by OpenGL backend");
        }
        if (desc.depthAttachment.texture &&
            desc.depthAttachment.texture->backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: depth attachment texture must be created by OpenGL backend");
        }
        activeColorFormat_.reset();
        activeDepthFormat_.reset();
        if (desc.colorAttachment.texture) {
            activeColorFormat_ = desc.colorAttachment.texture->desc().format;
        }
        if (desc.depthAttachment.texture) {
            activeDepthFormat_ = desc.depthAttachment.texture->desc().format;
        }
        stencilReference_ = 0;
        inRenderPass_ = true;
        return Status::success();
    }

    [[nodiscard]] Status end_render_pass() override {
        if (const auto s = require_recording("end_render_pass"); !s.ok()) {
            return s;
        }
        if (!inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: no active render pass");
        }
        inRenderPass_ = false;
        graphicsLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        activeColorFormat_.reset();
        activeDepthFormat_.reset();
        return Status::success();
    }

    [[nodiscard]] Status bind_pipeline(IPipeline& pipeline) override {
        if (const auto s = require_render_pass("bind_pipeline"); !s.ok()) {
            return s;
        }
        if (pipeline.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: pipeline must be created by OpenGL backend");
        }
        if (!validation::pipeline_render_pass_compatible(
                pipeline.desc(), activeColorFormat_, activeDepthFormat_)) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: pipeline is incompatible with the active render pass");
        }
        graphicsLayout_ = &pipeline.desc().layout;
        boundGraphicsGroups_.clear();
        computeLayout_ = nullptr;
        boundComputeGroups_.clear();
        return Status::success();
    }

    [[nodiscard]] Status bind_compute_pipeline(IComputePipeline& pipeline) override {
        if (const auto s = require_recording("bind_compute_pipeline"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: bind_compute_pipeline cannot run inside render pass");
        }
        if (pipeline.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: compute pipeline must be created by OpenGL backend");
        }
        computeLayout_ = &pipeline.desc().layout;
        boundComputeGroups_.clear();
        graphicsLayout_ = nullptr;
        boundGraphicsGroups_.clear();
        return Status::success();
    }

    [[nodiscard]] Status bind_vertex_buffer(
        std::uint32_t /*binding*/, IBuffer& buffer, std::size_t /*offset*/) override {
        if (const auto s = require_render_pass("bind_vertex_buffer"); !s.ok()) {
            return s;
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::vertex)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: buffer lacks vertex usage");
        }
        if (buffer.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: vertex buffer must be created by OpenGL backend");
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
                                   "OpenGLCommandBuffer: buffer lacks index usage");
        }
        if (buffer.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: index buffer must be created by OpenGL backend");
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
                                   "OpenGLCommandBuffer: buffer lacks uniform usage");
        }
        if (buffer.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: uniform buffer must be created by OpenGL backend");
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
                                   "OpenGLCommandBuffer: bind_storage_buffer cannot run inside render pass");
        }
        if (!validation::buffer_supports_usage(buffer.desc(), BufferUsageFlags::storage)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: buffer lacks storage usage");
        }
        if (buffer.backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: storage buffer must be created by OpenGL backend");
        }
        return Status::success();
    }

    [[nodiscard]] Status bind_group(std::uint32_t groupIndex,
                                    IBindGroup& group) override {
        static const std::vector<BindGroupDynamicOffset> noDynamicOffsets;
        return bind_group(groupIndex, group, noDynamicOffsets);
    }

    [[nodiscard]] Status bind_group(
        std::uint32_t groupIndex,
        IBindGroup& group,
        const std::vector<BindGroupDynamicOffset>& dynamicOffsets) override {
        if (const auto s = require_recording("bind_group"); !s.ok()) {
            return s;
        }
        if (!dynamic_cast<OpenGLBindGroup*>(&group)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: bind group must be created by OpenGL backend");
        }
        if (!validation::bind_group_desc_valid(group.desc())) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: bind group descriptor is invalid");
        }
        if (!validation::bind_group_dynamic_offsets_valid(
                group.desc(), dynamicOffsets, limits_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: bind group dynamic offsets are invalid");
        }
        const auto* activeLayout = inRenderPass_ ? graphicsLayout_ : computeLayout_;
        auto& boundGroups = inRenderPass_ ? boundGraphicsGroups_ : boundComputeGroups_;
        if (!activeLayout) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: bind_group requires a bound pipeline");
        }
        if (!validation::pipeline_layout_bind_group_compatible(
                *activeLayout, groupIndex, group.desc().layout->desc())) {
            return Status::failure(
                StatusCode::invalid_argument,
                "OpenGLCommandBuffer: bind group layout is incompatible with pipeline layout");
        }
        remember_bound_group(boundGroups, groupIndex);
        return Status::success();
    }

    [[nodiscard]] Status resource_barrier(
        const BufferBarrierDesc& barrier) override {
        if (const auto s = require_recording("resource_barrier"); !s.ok()) {
            return s;
        }
        if (inRenderPass_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: buffer barrier cannot run inside render pass");
        }
        if (!validation::buffer_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: buffer barrier is invalid");
        }
        if (barrier.buffer->backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: buffer barrier resource must be created by OpenGL backend");
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
                                   "OpenGLCommandBuffer: texture barrier cannot run inside render pass");
        }
        if (!validation::texture_barrier_valid(barrier)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: texture barrier is invalid");
        }
        if (barrier.texture->backend_kind() != BackendKind::opengl) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: texture barrier resource must be created by OpenGL backend");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_viewport(
        float x, float y, float width, float height,
        float minDepth, float maxDepth) override {
        if (const auto s = require_render_pass("set_viewport"); !s.ok()) {
            return s;
        }
        if (!validation::viewport_valid(x, y, width, height, minDepth, maxDepth)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: viewport descriptor is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_scissor(
        std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height) override {
        if (const auto s = require_render_pass("set_scissor"); !s.ok()) {
            return s;
        }
        if (!validation::scissor_valid(x, y, width, height)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGLCommandBuffer: scissor rectangle is invalid");
        }
        return Status::success();
    }

    [[nodiscard]] Status set_stencil_reference(
        std::uint32_t reference) override {
        if (const auto s = require_render_pass("set_stencil_reference"); !s.ok()) {
            return s;
        }
        if (!activeDepthFormat_ ||
            !texture_format_has_stencil_aspect(*activeDepthFormat_)) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: set_stencil_reference requires stencil-capable depth attachment");
        }
        stencilReference_ = reference;
        return Status::success();
    }

    [[nodiscard]] Status draw(std::uint32_t /*vertex_count*/) override {
        if (const auto s = require_render_pass("draw"); !s.ok()) {
            return s;
        }
        if (const auto s = require_graphics_bind_groups("draw"); !s.ok()) {
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
        if (const auto s = require_graphics_bind_groups("draw_instanced"); !s.ok()) {
            return s;
        }
        record_draw();
        return Status::success();
    }

    [[nodiscard]] Status draw_indexed(std::uint32_t /*index_count*/) override {
        if (const auto s = require_render_pass("draw_indexed"); !s.ok()) {
            return s;
        }
        if (const auto s = require_graphics_bind_groups("draw_indexed"); !s.ok()) {
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
        if (const auto s = require_graphics_bind_groups("draw_indexed_instanced"); !s.ok()) {
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
                                   "OpenGLCommandBuffer: buffer lacks indirect usage");
        }
        if (const auto s = require_graphics_bind_groups("draw_indirect"); !s.ok()) {
            return s;
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
                                   "OpenGLCommandBuffer: buffer lacks indirect usage");
        }
        if (const auto s = require_graphics_bind_groups("draw_indexed_indirect"); !s.ok()) {
            return s;
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
                                   "OpenGLCommandBuffer: dispatch_compute cannot run inside render pass");
        }
        if (!computeLayout_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLCommandBuffer: dispatch_compute requires a bound compute pipeline");
        }
        if (!validation::pipeline_layout_required_groups_bound(
                *computeLayout_, boundComputeGroups_)) {
            return Status::failure(
                StatusCode::invalid_state,
                "OpenGLCommandBuffer: dispatch_compute requires all pipeline bind groups");
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
                std::string{"OpenGLCommandBuffer: "} + op + " requires recording state");
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
                std::string{"OpenGLCommandBuffer: "} + op + " requires active render pass");
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
                std::string{"OpenGLCommandBuffer: "} + op +
                    " requires a bound graphics pipeline");
        }
        if (!validation::pipeline_layout_required_groups_bound(
                *graphicsLayout_, boundGraphicsGroups_)) {
            return Status::failure(
                StatusCode::invalid_state,
                std::string{"OpenGLCommandBuffer: "} + op +
                    " requires all pipeline bind groups");
        }
        return Status::success();
    }

    State state_ = State::initial;
    bool inRenderPass_ = false;
    std::optional<TextureFormat> activeColorFormat_;
    std::optional<TextureFormat> activeDepthFormat_;
    std::uint32_t stencilReference_ = 0;
    std::uint32_t debugLabelDepth_ = 0;
    const PipelineLayoutDesc* graphicsLayout_ = nullptr;
    const PipelineLayoutDesc* computeLayout_ = nullptr;
    std::vector<std::uint32_t> boundGraphicsGroups_;
    std::vector<std::uint32_t> boundComputeGroups_;
    DeviceLimits limits_;
    BackendDiagnosticsPtr diagnostics_;
};

class OpenGLQueue final : public IQueue {
public:
    OpenGLQueue(QueueKind kind, BackendDiagnosticsPtr diagnostics)
        : kind_(kind), diagnostics_(std::move(diagnostics)) {}

    [[nodiscard]] QueueKind kind() const noexcept override { return kind_; }

    [[nodiscard]] Status submit(ICommandBuffer& command_buffer,
                                IFence* signal_fence = nullptr) override {
        if (!command_buffer.ready_for_submit()) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGLQueue: command buffer is not ready for submit");
        }

        if (auto* cmd = dynamic_cast<OpenGLCommandBuffer*>(&command_buffer)) {
            cmd->mark_submitted();
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().submissions;
        }
        record_backend_event(diagnostics_, BackendEventKind::submitted,
                             {}, "command buffer submitted");

        if (signal_fence) {
            if (auto* fence = dynamic_cast<OpenGLFence*>(signal_fence)) {
                fence->signal();
            }
        }

        return Status::success();
    }

private:
    QueueKind kind_;
    BackendDiagnosticsPtr diagnostics_;
};

class OpenGLBuffer final : public IBuffer {
public:
    explicit OpenGLBuffer(BufferDesc desc)
        : desc_(std::move(desc)),
          storage_(desc_.size),
          mapped_(desc_.mappedAtCreation) {}
    OpenGLBuffer(BufferDesc desc, std::byte* externalData)
        : desc_(std::move(desc)),
          externalData_(externalData),
          mapped_(desc_.mappedAtCreation) {}
    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }
    [[nodiscard]] const BufferDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] Result<void*> map() override {
        if (!validation::buffer_memory_mappable(desc_)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL buffer memory is not CPU-mappable");
        }
        if (mapped_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGL buffer is already mapped");
        }
        mapped_ = true;
        return data();
    }
    [[nodiscard]] Status unmap() override {
        if (!mapped_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGL buffer is not mapped");
        }
        mapped_ = false;
        return Status::success();
    }
    [[nodiscard]] bool mapped() const noexcept override { return mapped_; }
    [[nodiscard]] void* mapped_data() noexcept override {
        return mapped_ ? data() : nullptr;
    }

private:
    [[nodiscard]] std::byte* data() noexcept {
        return externalData_ ? externalData_ : storage_.data();
    }

    BufferDesc desc_;
    std::vector<std::byte> storage_;
    std::byte* externalData_ = nullptr;
    bool mapped_ = false;
};

class OpenGLTexture final : public ITexture {
public:
    explicit OpenGLTexture(TextureDesc desc) : desc_(std::move(desc)) {}
    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }
    [[nodiscard]] const TextureDesc& desc() const noexcept override { return desc_; }

private:
    TextureDesc desc_;
};

class OpenGLSampler final : public ISampler {
public:
    explicit OpenGLSampler(SamplerDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }
    [[nodiscard]] const SamplerDesc& desc() const noexcept override { return desc_; }

private:
    SamplerDesc desc_;
};

class OpenGLShader final : public IShader {
public:
    explicit OpenGLShader(ShaderDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }

    [[nodiscard]] const ShaderDesc& desc() const noexcept { return desc_; }

private:
    ShaderDesc desc_;
};

class OpenGLPipelineReflection final : public IPipelineReflection {
public:
    OpenGLPipelineReflection() = default;

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

class OpenGLPipeline final : public IPipeline {
public:
    OpenGLPipeline(PipelineDesc desc,
                   std::unique_ptr<OpenGLPipelineReflection> reflection)
        : desc_(std::move(desc)), reflection_(std::move(reflection)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }

    [[nodiscard]] const PipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override {
        return reflection_.get();
    }

private:
    PipelineDesc desc_;
    std::unique_ptr<OpenGLPipelineReflection> reflection_;
};

class OpenGLComputePipeline final : public IComputePipeline {
public:
    OpenGLComputePipeline(ComputePipelineDesc desc,
                          std::unique_ptr<OpenGLPipelineReflection> reflection)
        : desc_(std::move(desc)), reflection_(std::move(reflection)) {}

    [[nodiscard]] std::optional<BackendKind> backend_kind() const noexcept override {
        return BackendKind::opengl;
    }

    [[nodiscard]] const ComputePipelineDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] const IPipelineReflection* reflection() const noexcept override {
        return reflection_.get();
    }

private:
    ComputePipelineDesc desc_;
    std::unique_ptr<OpenGLPipelineReflection> reflection_;
};

class OpenGLSurface final : public ISurface {
public:
    explicit OpenGLSurface(SurfaceDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] const SurfaceDesc& desc() const noexcept override { return desc_; }

private:
    SurfaceDesc desc_;
};

class OpenGLSwapchain final : public ISwapchain {
public:
    explicit OpenGLSwapchain(SwapchainDesc desc) : desc_(std::move(desc)) {
        if (desc_.imageCount == 0) {
            desc_.imageCount = desc_.framesInFlight;
        }
    }

    [[nodiscard]] const SwapchainDesc& desc() const noexcept override { return desc_; }

    [[nodiscard]] Status resize(Extent2D extent) override {
        if (extent.width == 0 || extent.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: swapchain extent must be non-zero");
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
            drawable_ = std::make_unique<OpenGLTexture>(TextureDesc{
                .extent = desc_.extent,
                .format = desc_.format,
                .debugName = "opengl_swapchain_drawable",
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
        auto* openGLCmd = dynamic_cast<OpenGLCommandBuffer*>(&cmd);
        if (!openGLCmd) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: schedule_present requires OpenGL command buffer");
        }
        if (!openGLCmd->can_schedule_present()) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGL backend: schedule_present requires recording state outside render pass");
        }
        if (!acquired_) {
            return Status::failure(StatusCode::invalid_state,
                                   "OpenGL backend: schedule_present requires an acquired drawable");
        }
        acquired_ = false;
        return Status::success();
    }

private:
    SwapchainDesc                    desc_;
    std::unique_ptr<OpenGLTexture> drawable_;
    bool                             acquired_ = false;
    std::uint32_t                    currentImageIndex_ = 0;
    std::uint32_t                    nextImageIndex_ = 0;
};

class OpenGLFrameUploadRing final : public IFrameUploadRing {
public:
    OpenGLFrameUploadRing(std::uint32_t framesInFlight, std::size_t capacityPerFrame)
        : framesInFlight_(framesInFlight)
        , capacityPerFrame_(capacityPerFrame)
        , storage_(static_cast<std::size_t>(framesInFlight) * capacityPerFrame)
           , backingBuffer_(BufferDesc{
               .size = static_cast<std::size_t>(framesInFlight) * capacityPerFrame,
               .usage = BufferUsage::storage,
               .debugName = "opengl_upload_ring",
           }, storage_.data()) {}

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
                                   "OpenGLFrameUploadRing: frame is not ready for reuse");
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
    OpenGLBuffer backingBuffer_;
    std::uint32_t currentFrame_ = 0;
    std::size_t frameOffset_ = 0;
};

class OpenGLDevice final : public IDevice {
public:
    explicit OpenGLDevice(BackendDiagnosticsPtr diagnostics)
        : caps_(make_opengl_capabilities())
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
                                    "OpenGL backend: swapchain description is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().swapchainsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::swapchain_created,
                             {}, "swapchain created");
        return std::unique_ptr<ISwapchain>(std::make_unique<OpenGLSwapchain>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBuffer>> create_buffer(
        const BufferDesc& desc) override {
        if (desc.size == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: buffer size must be non-zero");
        }
        if (desc.size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: buffer size exceeds device limit");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL backend: buffer memory domain is not supported");
        }
        if (desc.mappedAtCreation &&
            !validation::buffer_memory_mappable(desc)) {
            return Status::failure(
                StatusCode::invalid_argument,
                "OpenGL backend: mapped buffer memory must be CPU-mappable");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().buffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "buffer created");
        return std::unique_ptr<IBuffer>(std::make_unique<OpenGLBuffer>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<ITexture>> create_texture(
        const TextureDesc& desc) override {
        if (!validation::texture_shape_valid(
                desc, caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                    "OpenGL backend: texture extent exceeds device limits");
        }
        if (!validation::memory_domain_supported(desc.memory, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL backend: texture memory domain is not supported");
        }
        if (!validation::texture_usage_supported_by_format(caps_, desc)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL backend: texture format does not support requested usage");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().texturesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.debugName, "texture created");
        return std::unique_ptr<ITexture>(std::make_unique<OpenGLTexture>(desc));
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
        return std::unique_ptr<ISampler>(std::make_unique<OpenGLSampler>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IShader>> create_shader(
        const ShaderDesc& desc) override {
        if (!validation::shader_payload_valid(desc)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: shader descriptor payload is invalid");
        }
        if (!validation::shader_byte_format_supported(desc.byteFormat, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL backend: shader byte format is not supported");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().shadersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::resource_created,
                             desc.entryPoint, "shader created");
        return std::unique_ptr<IShader>(std::make_unique<OpenGLShader>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IPipeline>> create_pipeline(
        const PipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: pipeline layout is invalid");
        }
        if (!validation::pipeline_render_state_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: pipeline render state is invalid");
        }
        if ((desc.vertexShader == nullptr) != (desc.fragmentShader == nullptr)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: pipeline requires both vertex and fragment shaders or neither");
        }
        auto* vertexShader =
            desc.vertexShader ? dynamic_cast<OpenGLShader*>(desc.vertexShader) : nullptr;
        auto* fragmentShader =
            desc.fragmentShader ? dynamic_cast<OpenGLShader*>(desc.fragmentShader) : nullptr;
        if ((desc.vertexShader && !vertexShader) ||
            (desc.fragmentShader && !fragmentShader)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: pipeline shaders must be created by OpenGL backend");
        }
        if ((vertexShader && vertexShader->desc().stage != ShaderStage::vertex) ||
            (fragmentShader && fragmentShader->desc().stage != ShaderStage::fragment)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: pipeline shader stages are invalid");
        }

        auto reflection = std::make_unique<OpenGLPipelineReflection>();
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
            std::make_unique<OpenGLPipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBindGroupLayout>>
    create_bind_group_layout(const BindGroupLayoutDesc& desc) override {
        if (!validation::bind_group_layout_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: bind group layout is invalid");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupLayoutsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group layout created");
        return std::unique_ptr<IBindGroupLayout>(
            std::make_unique<OpenGLBindGroupLayout>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBindGroup>>
    create_bind_group(const BindGroupDesc& desc) override {
        if (!validation::bind_group_desc_valid(desc, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: bind group descriptor is invalid");
        }
        if (!dynamic_cast<OpenGLBindGroupLayout*>(desc.layout)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: bind group layout must be created by OpenGL backend");
        }
        for (const auto& entry : desc.entries) {
            switch (entry.type) {
                case BindingResourceType::uniform_buffer:
                case BindingResourceType::storage_buffer:
                    if ((entry.buffer.buffer &&
                         !dynamic_cast<OpenGLBuffer*>(entry.buffer.buffer)) ||
                        std::any_of(entry.buffers.begin(), entry.buffers.end(),
                                    [](const BufferBindingDesc& binding) {
                                        return !dynamic_cast<OpenGLBuffer*>(binding.buffer);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "OpenGL backend: bind group buffer must be created by OpenGL backend");
                    }
                    break;
                case BindingResourceType::sampled_texture:
                case BindingResourceType::storage_texture:
                    if ((entry.texture &&
                         !dynamic_cast<OpenGLTexture*>(entry.texture)) ||
                        std::any_of(entry.textures.begin(), entry.textures.end(),
                                    [](const ITexture* texture) {
                                        return !dynamic_cast<const OpenGLTexture*>(texture);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "OpenGL backend: bind group texture must be created by OpenGL backend");
                    }
                    break;
                case BindingResourceType::sampler:
                    if ((entry.sampler &&
                         !dynamic_cast<OpenGLSampler*>(entry.sampler)) ||
                        std::any_of(entry.samplers.begin(), entry.samplers.end(),
                                    [](const ISampler* sampler) {
                                        return !dynamic_cast<const OpenGLSampler*>(sampler);
                                    })) {
                        return Status::failure(StatusCode::invalid_argument,
                                                "OpenGL backend: bind group sampler must be created by OpenGL backend");
                    }
                    break;
            }
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().bindGroupsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::bind_group_created,
                             desc.debugName, "bind group created");
        return std::unique_ptr<IBindGroup>(std::make_unique<OpenGLBindGroup>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IComputePipeline>> create_compute_pipeline(
        const ComputePipelineDesc& desc) override {
        if (!validation::pipeline_layout_valid(desc.layout, caps_)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: compute pipeline layout is invalid");
        }
        if (!desc.computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: compute pipeline requires compute shader");
        }

        auto* computeShader = dynamic_cast<OpenGLShader*>(desc.computeShader);
        if (!computeShader) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: compute shader must be created by OpenGL backend");
        }
        if (computeShader->desc().stage != ShaderStage::compute) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: compute pipeline requires compute shader stage");
        }

        auto reflection = std::make_unique<OpenGLPipelineReflection>();
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
            std::make_unique<OpenGLComputePipeline>(desc, std::move(reflection)));
    }

    [[nodiscard]] CommandBufferPtr create_command_buffer() override {
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().commandBuffersCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::command_buffer_created,
                             {}, "command buffer created");
        return CommandBufferPtr(
            new OpenGLCommandBuffer(diagnostics_, make_opengl_capabilities().limits),
            [](ICommandBuffer* cmd) {
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
        return FencePtr(new OpenGLFence(desc), [](IFence* fence) {
            delete fence;
        });
    }


    [[nodiscard]] core::Result<std::unique_ptr<ISurface>> create_surface(
        const SurfaceDesc& desc) override {
        if (!validation::extent_within(desc.initialExtent,
                                       caps_.limits.maxTextureDimension2D)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: surface extent exceeds device limits");
        }
        if (!validation::native_surface_handles_valid(desc.native)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "OpenGL backend: native surface handles are invalid for the surface kind");
        }
        if (!validation::native_surface_kind_supported(desc.native.kind, caps_)) {
            return Status::failure(StatusCode::unsupported,
                                   "OpenGL backend: native surface kind is not supported");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().surfacesCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::surface_created,
                             {}, "surface created");
        return std::unique_ptr<ISurface>(std::make_unique<OpenGLSurface>(desc));
    }

    [[nodiscard]] core::Result<std::unique_ptr<IFrameUploadRing>> create_upload_ring(
        std::uint32_t frames_in_flight, std::size_t buffer_size) override {
        if (!validation::frame_count_supported(frames_in_flight, caps_) ||
            buffer_size == 0 || buffer_size > caps_.limits.maxBufferSize) {
            return Status::failure(StatusCode::invalid_argument,
                                    "OpenGL backend: upload ring frames/capacity must be non-zero");
        }
        if (diagnostics_) {
            ++diagnostics_->mutable_stats().uploadRingsCreated;
        }
        record_backend_event(diagnostics_, BackendEventKind::upload_ring_created,
                             {}, "upload ring created");
        return std::unique_ptr<IFrameUploadRing>(
            std::make_unique<OpenGLFrameUploadRing>(frames_in_flight, buffer_size));
    }

private:
    Capabilities caps_{};
    OpenGLQueue graphicsQueue_;
    OpenGLQueue computeQueue_;
    OpenGLQueue transferQueue_;
    BackendDiagnosticsPtr diagnostics_;
};

class OpenGLBackend final : public IBackend {
public:
    OpenGLBackend() = default;

    BackendKind kind() const noexcept override { return BackendKind::opengl; }

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
            .name = "OpenGL Stub Adapter",
            .backend = BackendKind::opengl,
            .capabilities = make_opengl_capabilities(),
            .type = AdapterType::virtual_gpu,
            .driverDescription = "OpenGL contract adapter",
        }};
    }

    core::Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& desc) override {
        if (desc.adapterId != 0) {
            return Status::failure(StatusCode::unavailable,
                                   "OpenGL backend currently exposes one adapter (id=0)");
        }
        ++diagnostics_->mutable_stats().devicesCreated;
        record_backend_event(diagnostics_, BackendEventKind::device_created,
                             {}, "device created");
        return std::unique_ptr<IDevice>(std::make_unique<OpenGLDevice>(diagnostics_));
    }

private:
    BackendDiagnosticsPtr diagnostics_ =
        make_backend_diagnostics(BackendKind::opengl);
};

} // namespace

std::unique_ptr<IBackend> create_opengl_backend() {
    return std::make_unique<OpenGLBackend>();
}

} // namespace truffle::rhi
