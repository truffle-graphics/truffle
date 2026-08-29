#pragma once

#include "truffle/rhi/rhi.hpp"

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

struct FoundationBackendConfig {
    BackendKind kind = BackendKind::null_validation;
    std::string adapterName;
    std::vector<QueueKind> queueKinds;
    std::vector<Feature> supportedFeatures;
    AdapterInfo::ResourceCapabilities resourceCapabilities;
    std::size_t uploadBudgetBytes = 256u * 1024u * 1024u;
    std::size_t readbackBudgetBytes = 256u * 1024u * 1024u;
    std::size_t deviceLocalBudgetBytes = 1024u * 1024u * 1024u;
    bool native = false;
    bool validationOnly = false;
    bool presentation = false;
    bool logicalResources = false;
    Result<std::shared_ptr<void>> (*createBuffer)(const BufferDesc&) = nullptr;
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
    Result<std::shared_ptr<void>> (*createTexture)(const TextureDesc&) = nullptr;
    Result<std::shared_ptr<void>> (*createTextureView)(
        const std::shared_ptr<void>&, const TextureViewDesc&) = nullptr;
    Status (*writeTexture)(const std::shared_ptr<void>&, const TextureRegion&,
                           std::span<const std::byte>,
                           const TextureDataLayout&) = nullptr;
    Status (*readTexture)(const std::shared_ptr<void>&, const TextureRegion&,
                          std::span<std::byte>,
                          const TextureDataLayout&) = nullptr;
    Status (*nativeSubmit)(std::span<const NativeTransfer>) = nullptr;
};

[[nodiscard]] Result<Instance> create_foundation_instance(
    const InstanceDesc& desc, FoundationBackendConfig config);

[[nodiscard]] Result<Instance> unavailable_backend(
    BackendKind kind, std::string backendName);

} // namespace truffle::rhi::detail
