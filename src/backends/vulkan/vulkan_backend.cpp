#include "truffle/rhi/rhi.hpp"
#include "truffle/rhi/shader_reflection.hpp"

#include <memory>

namespace truffle::rhi {

namespace {

class VulkanDevice final : public IDevice {
public:
    VulkanDevice() = default;

    [[nodiscard]] const Capabilities& capabilities() const noexcept override {
        static Capabilities caps;
        return caps;
    }

    [[nodiscard]] core::Result<std::unique_ptr<ISwapchain>> create_swapchain(
        ISurface& /*surface*/, const SwapchainDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<ISwapchain>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<IBuffer>> create_buffer(
        const BufferDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<IBuffer>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<ITexture>> create_texture(
        const TextureDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<ITexture>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }
    
    [[nodiscard]] core::Result<std::unique_ptr<ISampler>> create_sampler(
        const SamplerDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<ISampler>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<IShader>> create_shader(
        const ShaderDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<IShader>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<IPipeline>> create_pipeline(
        const PipelineDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<IPipeline>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<IComputePipeline>> create_compute_pipeline(
        const ComputePipelineDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<IComputePipeline>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] CommandBufferPtr create_command_buffer() override {
        return CommandBufferPtr(nullptr, [](ICommandBuffer*){});
    }

    IQueue& queue(QueueKind /*kind*/) override {
        // Technically illegal if someone dereferences this, 
        // but for a strict stub we never return a valid queue.
        static struct DummyQueue : public IQueue {
            [[nodiscard]] QueueKind kind() const noexcept override { return QueueKind::graphics; }
            [[nodiscard]] core::Status submit(ICommandBuffer& /*cmd*/, IFence* /*fence*/ = nullptr) override {
                return core::Status::failure(core::StatusCode::unavailable, "stub");
            }
        } dummy;
        return dummy;
    }

    [[nodiscard]] FencePtr create_fence(const FenceDesc& /*desc*/) override {
        return FencePtr(nullptr, [](IFence*){});
    }


    [[nodiscard]] core::Result<std::unique_ptr<ISurface>> create_surface(
        const SurfaceDesc& /*desc*/) override {
        return core::Result<std::unique_ptr<ISurface>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    [[nodiscard]] core::Result<std::unique_ptr<IFrameUploadRing>> create_upload_ring(
        std::uint32_t /*frames_in_flight*/, std::size_t /*buffer_size*/) override {
        return core::Result<std::unique_ptr<IFrameUploadRing>>{core::Status::failure(
            core::StatusCode::unavailable, "Vulkan backend is a stub")};
    }

    // We can just omit stats since it's not a pure virtual anymore. Or map it if we really need it.
    // wait I see we removed stats() from IDevice in `rhi.hpp` locally? Ah, I left `BackendStats` in rhi.hpp? Let me double check rhi.hpp.
};

class VulkanBackend final : public IBackend {
public:
    VulkanBackend() = default;

    BackendKind kind() const noexcept override { return BackendKind::vulkan; }

    std::vector<AdapterInfo> enumerate_adapters() const override {
        return {}; // Vulkan stub doesn't expose real adapters yet
    }

    core::Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& /*desc*/) override {
        return std::unique_ptr<IDevice>(std::make_unique<VulkanDevice>());
    }
};

} // namespace

std::unique_ptr<IBackend> create_vulkan_backend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace truffle::rhi
