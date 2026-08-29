#pragma once

#include "truffle/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <span>
#include <string>

namespace truffle::rhi::detail {

enum class NativeTransferKind {
    copy_buffer,
    fill_buffer,
    copy_buffer_to_texture,
    copy_texture_to_buffer,
    copy_texture,
    clear_texture,
    resolve_texture,
    blit_texture,
};

struct NativeTransfer {
    NativeTransferKind kind = NativeTransferKind::copy_buffer;
    std::shared_ptr<void> source;
    std::shared_ptr<void> destination;
    BufferCopyRegion buffer;
    BufferTextureCopyRegion bufferTexture;
    TextureCopyRegion texture;
    TextureBlitRegion blit;
    ClearValue clear;
    std::byte fillValue{};
};

enum class NativeCommandKind {
    transfer,
    barrier,
    begin_render,
    end_render,
    begin_compute,
    end_compute,
    bind_graphics_pipeline,
    bind_compute_pipeline,
    bind_vertex_buffer,
    bind_uniform_buffer,
    bind_storage_buffer,
    bind_index_buffer,
    bind_group,
    push_constants,
    set_viewports,
    set_scissors,
    set_blend_constant,
    set_stencil_reference,
    set_depth_bias,
    draw,
    draw_indexed,
    draw_indirect,
    draw_indirect_count,
    dispatch,
    dispatch_indirect,
};

struct NativeRenderAttachment {
    std::shared_ptr<void> texture;
    std::shared_ptr<void> resolveTexture;
    LoadOp loadOp = LoadOp::clear;
    StoreOp storeOp = StoreOp::store;
    ClearColor clear;
};

struct NativeDepthStencilAttachment {
    std::shared_ptr<void> texture;
    LoadOp depthLoadOp = LoadOp::clear;
    StoreOp depthStoreOp = StoreOp::store;
    float clearDepth = 1.0F;
    LoadOp stencilLoadOp = LoadOp::clear;
    StoreOp stencilStoreOp = StoreOp::store;
    std::uint32_t clearStencil = 0;
};

struct NativeBindingResource {
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::uint32_t arrayElement = 0;
    BindingType type = BindingType::uniform_buffer;
    ShaderStageMask visibility = ShaderStageMask::none;
    std::shared_ptr<void> resource;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct NativeCommand {
    NativeCommandKind kind = NativeCommandKind::draw;
    NativeTransfer transfer;
    Extent2D extent;
    std::vector<NativeRenderAttachment> colorAttachments;
    NativeDepthStencilAttachment depthStencilAttachment;
    std::shared_ptr<void> object;
    std::shared_ptr<void> secondaryObject;
    std::vector<NativeBindingResource> bindings;
    std::vector<std::byte> bytes;
    std::vector<Viewport> viewports;
    std::vector<ScissorRect> scissors;
    std::array<std::uint64_t, 8> arguments{};
};

struct NativeSemaphorePoint {
    std::shared_ptr<void> semaphore;
    std::uint64_t value = 0;
    PipelineStage stages = PipelineStage::all_commands;
};

struct NativeSwapchainImage {
    std::shared_ptr<void> texture;
    std::uint32_t imageIndex = 0;
    Extent2D extent;
    Status status = Status::success();
};

struct FoundationBackendConfig {
    BackendKind kind = BackendKind::null_validation;
    PlatformKind platform = PlatformKind::all;
    BackendMaturity maturity = BackendMaturity::source_only;
    std::string adapterName;
    std::vector<QueueKind> queueKinds;
    std::vector<Feature> supportedFeatures;
    AdapterInfo::ResourceCapabilities resourceCapabilities;
    AdapterInfo::BindingCapabilities bindingCapabilities;
    AdapterInfo::PipelineCapabilities pipelineCapabilities;
    std::size_t uploadBudgetBytes = 256u * 1024u * 1024u;
    std::size_t readbackBudgetBytes = 256u * 1024u * 1024u;
    std::size_t deviceLocalBudgetBytes = 1024u * 1024u * 1024u;
    bool native = false;
    bool validationOnly = false;
    bool presentation = false;
    bool logicalResources = false;
    std::shared_ptr<void> nativeContext;
    Result<std::shared_ptr<void>> (*createBuffer)(
        const std::shared_ptr<void>&, const BufferDesc&) = nullptr;
    Result<std::span<std::byte>> (*mapBuffer)(
        const std::shared_ptr<void>&) = nullptr;
    Status (*unmapBuffer)(const std::shared_ptr<void>&) = nullptr;
    Status (*flushBuffer)(const std::shared_ptr<void>&, std::size_t,
                          std::size_t) = nullptr;
    Status (*invalidateBuffer)(const std::shared_ptr<void>&, std::size_t,
                               std::size_t) = nullptr;
    Status (*writeBuffer)(const std::shared_ptr<void>&, std::size_t,
                          std::span<const std::byte>) = nullptr;
    Status (*readBuffer)(const std::shared_ptr<void>&, std::size_t,
                         std::span<std::byte>) = nullptr;
    Result<std::shared_ptr<void>> (*createTexture)(
        const std::shared_ptr<void>&, const TextureDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createTextureView)(
        const std::shared_ptr<void>&, const TextureViewDesc&) = nullptr;
    Status (*writeTexture)(const std::shared_ptr<void>&, const TextureRegion&,
                           std::span<const std::byte>,
                           const TextureDataLayout&) = nullptr;
    Status (*readTexture)(const std::shared_ptr<void>&, const TextureRegion&,
                          std::span<std::byte>,
                          const TextureDataLayout&) = nullptr;
    Result<std::shared_ptr<void>> (*createSampler)(const SamplerDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createShader)(const ShaderDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createPipeline)(
        const PipelineDesc&, const std::shared_ptr<void>&,
        const std::shared_ptr<void>&) = nullptr;
    Result<std::shared_ptr<void>> (*createComputePipeline)(
        const ComputePipelineDesc&, const std::shared_ptr<void>&) = nullptr;
    Result<std::shared_ptr<void>> (*createSemaphore)(
        const SemaphoreDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createSurface)(const SurfaceDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createSwapchain)(
        const std::shared_ptr<void>&, const SwapchainDesc&) = nullptr;
    Result<NativeSwapchainImage> (*acquireSwapchain)(
        const std::shared_ptr<void>&) = nullptr;
    Status (*resizeSwapchain)(const std::shared_ptr<void>&, Extent2D) = nullptr;
    Status (*presentSwapchain)(const std::shared_ptr<void>&, std::uint32_t,
                               std::span<const NativeSemaphorePoint>) = nullptr;
    Status (*nativeSubmit)(const std::shared_ptr<void>&,
                           std::span<const NativeCommand>,
                           std::span<const NativeSemaphorePoint>,
                           std::span<const NativeSemaphorePoint>) = nullptr;
};

[[nodiscard]] Result<Instance> create_foundation_instance(
    const InstanceDesc& desc, FoundationBackendConfig config);

[[nodiscard]] Result<Instance> unavailable_backend(
    BackendKind kind, std::string backendName);

} // namespace truffle::rhi::detail
