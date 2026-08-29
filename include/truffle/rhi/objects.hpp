#pragma once

#include "truffle/rhi/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace truffle::rhi {

namespace detail {
struct ObjectState;
struct SwapchainState;
struct UploadRingState;
struct Factory;
} // namespace detail

class Adapter;
class Device;
class Queue;
class CommandPool;
class CommandList;
class RenderEncoder;
class ComputeEncoder;
class CopyEncoder;
class BufferView;
class TextureView;
class Fence;
class Semaphore;
class QueryPool;
class Surface;
class Swapchain;
class UploadRing;
class ShaderPackage;
enum class ShaderTarget : std::uint8_t;

class Instance {
public:
    Instance() noexcept;
    ~Instance();
    Instance(Instance&&) noexcept;
    Instance& operator=(Instance&&) noexcept;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] BackendKind backend() const noexcept;
    [[nodiscard]] std::size_t adapter_count() const noexcept;
    [[nodiscard]] Result<Adapter> adapter(std::size_t index) const;
    [[nodiscard]] BackendStats stats() const noexcept;

private:
    explicit Instance(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class Adapter {
public:
    Adapter() noexcept;
    ~Adapter();
    Adapter(Adapter&&) noexcept;
    Adapter& operator=(Adapter&&) noexcept;
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] const AdapterInfo& info() const;
    [[nodiscard]] Result<Device> request_device(const DeviceDesc& desc = {}) const;

private:
    explicit Adapter(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class Buffer {
public:
    Buffer() noexcept;
    ~Buffer();
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] BufferDesc desc() const;
    [[nodiscard]] Result<std::span<std::byte>> map();
    [[nodiscard]] Status unmap();
    [[nodiscard]] bool mapped() const noexcept;
    [[nodiscard]] Status flush(std::size_t offset = 0,
                               std::size_t size = whole_size);
    [[nodiscard]] Status invalidate(std::size_t offset = 0,
                                    std::size_t size = whole_size);
    [[nodiscard]] Status write(std::size_t offset,
                               std::span<const std::byte> data);
    [[nodiscard]] Status read(std::size_t offset, std::span<std::byte> data) const;
    [[nodiscard]] MemoryRequirements memory_requirements() const noexcept;
    [[nodiscard]] Result<ExternalMemoryHandle> export_memory() const;

private:
    explicit Buffer(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class CommandList;
    friend class RenderEncoder;
    friend class ComputeEncoder;
    friend class CopyEncoder;
    friend class Device;
};

class BufferView {
public:
    BufferView() noexcept;
    ~BufferView();
    BufferView(BufferView&&) noexcept;
    BufferView& operator=(BufferView&&) noexcept;
    BufferView(const BufferView&) = delete;
    BufferView& operator=(const BufferView&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] BufferViewDesc desc() const;
    [[nodiscard]] ObjectId buffer_id() const noexcept;

private:
    explicit BufferView(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class Texture {
public:
    Texture() noexcept;
    ~Texture();
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] TextureDesc desc() const;
    [[nodiscard]] Status write(const TextureRegion& region,
                               std::span<const std::byte> data,
                               const TextureDataLayout& layout = {});
    [[nodiscard]] Status read(const TextureRegion& region,
                              std::span<std::byte> data,
                              const TextureDataLayout& layout = {}) const;
    [[nodiscard]] MemoryRequirements memory_requirements() const noexcept;
    [[nodiscard]] Result<ExternalMemoryHandle> export_memory() const;

private:
    explicit Texture(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class CommandList;
    friend class CopyEncoder;
    friend class Device;
};

class TextureView {
public:
    TextureView() noexcept;
    ~TextureView();
    TextureView(TextureView&&) noexcept;
    TextureView& operator=(TextureView&&) noexcept;
    TextureView(const TextureView&) = delete;
    TextureView& operator=(const TextureView&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] TextureViewDesc desc() const;
    [[nodiscard]] ObjectId texture_id() const noexcept;

private:
    explicit TextureView(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class Sampler {
public:
    Sampler() noexcept;
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] SamplerDesc desc() const;

private:
    explicit Sampler(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class BindGroupLayout {
public:
    BindGroupLayout() noexcept;
    ~BindGroupLayout();
    BindGroupLayout(BindGroupLayout&&) noexcept;
    BindGroupLayout& operator=(BindGroupLayout&&) noexcept;
    BindGroupLayout(const BindGroupLayout&) = delete;
    BindGroupLayout& operator=(const BindGroupLayout&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;

private:
    explicit BindGroupLayout(
        std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class DescriptorArena {
public:
    DescriptorArena() noexcept;
    ~DescriptorArena();
    DescriptorArena(DescriptorArena&&) noexcept;
    DescriptorArena& operator=(DescriptorArena&&) noexcept;
    DescriptorArena(const DescriptorArena&) = delete;
    DescriptorArena& operator=(const DescriptorArena&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] DescriptorArenaDesc desc() const;
    [[nodiscard]] std::uint64_t epoch() const noexcept;
    [[nodiscard]] Status reset();

private:
    explicit DescriptorArena(
        std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class BindGroup {
public:
    BindGroup() noexcept;
    ~BindGroup();
    BindGroup(BindGroup&&) noexcept;
    BindGroup& operator=(BindGroup&&) noexcept;
    BindGroup(const BindGroup&) = delete;
    BindGroup& operator=(const BindGroup&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] ObjectId layout_id() const noexcept;

private:
    explicit BindGroup(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class RenderEncoder;
    friend class ComputeEncoder;
};

class BindlessTable {
public:
    BindlessTable() noexcept;
    ~BindlessTable();
    BindlessTable(BindlessTable&&) noexcept;
    BindlessTable& operator=(BindlessTable&&) noexcept;
    BindlessTable(const BindlessTable&) = delete;
    BindlessTable& operator=(const BindlessTable&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;

private:
    explicit BindlessTable(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class PipelineLayout {
public:
    PipelineLayout() noexcept;
    ~PipelineLayout();
    PipelineLayout(PipelineLayout&&) noexcept;
    PipelineLayout& operator=(PipelineLayout&&) noexcept;
    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;

private:
    explicit PipelineLayout(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class PipelineCache {
public:
    PipelineCache() noexcept;
    ~PipelineCache();
    PipelineCache(PipelineCache&&) noexcept;
    PipelineCache& operator=(PipelineCache&&) noexcept;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] Result<std::vector<std::byte>> data() const;

private:
    explicit PipelineCache(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class Shader {
public:
    Shader() noexcept;
    ~Shader();
    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] ShaderStage stage() const;
    [[nodiscard]] const PipelineReflection& reflection() const;

private:
    explicit Shader(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

class Pipeline {
public:
    Pipeline() noexcept;
    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] const PipelineReflection& reflection() const;

private:
    explicit Pipeline(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class RenderEncoder;
};

class ComputePipeline {
public:
    ComputePipeline() noexcept;
    ~ComputePipeline();
    ComputePipeline(ComputePipeline&&) noexcept;
    ComputePipeline& operator=(ComputePipeline&&) noexcept;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] const PipelineReflection& reflection() const;
    [[nodiscard]] Extent3D preferred_workgroup_size() const;

private:
    explicit ComputePipeline(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class ComputeEncoder;
};

class Device {
public:
    Device() noexcept;
    ~Device();
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] bool lost() const noexcept;
    [[nodiscard]] const AdapterInfo& adapter_info() const;
    [[nodiscard]] Result<Queue> queue(QueueKind kind) const;
    [[nodiscard]] Result<CommandPool> create_command_pool(QueueKind kind) const;
    [[nodiscard]] Result<Buffer> create_buffer(const BufferDesc& desc) const;
    [[nodiscard]] Result<Texture> create_texture(const TextureDesc& desc) const;
    [[nodiscard]] Result<BufferView> create_buffer_view(
        Buffer& buffer, const BufferViewDesc& desc = {}) const;
    [[nodiscard]] Result<TextureView> create_texture_view(
        Texture& texture, const TextureViewDesc& desc = {}) const;
    [[nodiscard]] Result<Buffer> import_buffer(
        const BufferDesc& desc, ExternalMemoryHandle handle) const;
    [[nodiscard]] Result<Texture> import_texture(
        const TextureDesc& desc, ExternalMemoryHandle handle) const;
    [[nodiscard]] Result<MemoryBudget> memory_budget(MemoryDomain domain) const;
    [[nodiscard]] Result<Sampler> create_sampler(
        const SamplerDesc& desc = {}) const;
    [[nodiscard]] Result<BindGroupLayout> create_bind_group_layout(
        const BindGroupLayoutDesc& desc) const;
    [[nodiscard]] Result<DescriptorArena> create_descriptor_arena(
        const DescriptorArenaDesc& desc = {}) const;
    [[nodiscard]] Result<BindGroup> create_bind_group(
        const BindGroupDesc& desc) const;
    [[nodiscard]] Result<BindlessTable> create_bindless_table(
        const BindlessTableDesc& desc) const;
    [[nodiscard]] Result<PipelineLayout> create_pipeline_layout(
        const PipelineLayoutDesc& desc) const;
    [[nodiscard]] Result<PipelineCache> create_pipeline_cache(
        const PipelineCacheDesc& desc = {}) const;
    [[nodiscard]] Result<Shader> create_shader(const ShaderDesc& desc) const;
    [[nodiscard]] Result<Shader> create_shader(
        const ShaderPackage& package, ShaderTarget target,
        std::string_view entryPoint, ShaderStage stage,
        std::string_view permutation = {}) const;
    [[nodiscard]] Result<Pipeline> create_pipeline(const PipelineDesc& desc) const;
    [[nodiscard]] Result<ComputePipeline> create_compute_pipeline(
        const ComputePipelineDesc& desc) const;
    [[nodiscard]] Result<Fence> create_fence(const FenceDesc& desc = {}) const;
    [[nodiscard]] Result<Semaphore> create_semaphore(
        const SemaphoreDesc& desc = {}) const;
    [[nodiscard]] Result<QueryPool> create_query_pool(
        const QueryPoolDesc& desc) const;
    [[nodiscard]] Result<Surface> create_surface(const SurfaceDesc& desc) const;
    [[nodiscard]] Result<Swapchain> create_swapchain(
        Surface& surface, const SwapchainDesc& desc) const;
    [[nodiscard]] Result<UploadRing> create_upload_ring(
        std::uint32_t frameCount, std::size_t bytesPerFrame) const;

private:
    explicit Device(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class CommandPool {
public:
    CommandPool() noexcept;
    ~CommandPool();
    CommandPool(CommandPool&&) noexcept;
    CommandPool& operator=(CommandPool&&) noexcept;
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] Result<CommandList> allocate() const;
    [[nodiscard]] Status reset();

private:
    explicit CommandPool(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class CommandList {
public:
    CommandList() noexcept;
    ~CommandList();
    CommandList(CommandList&&) noexcept;
    CommandList& operator=(CommandList&&) noexcept;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] CommandListState state() const noexcept;
    [[nodiscard]] Status begin();
    [[nodiscard]] Status end();
    [[nodiscard]] Status reset();
    [[nodiscard]] Result<RenderEncoder> begin_rendering(const RenderPassDesc& desc);
    [[nodiscard]] Result<ComputeEncoder> begin_compute();
    [[nodiscard]] Result<CopyEncoder> begin_copy();
    [[nodiscard]] Status barrier(const BarrierBatch& batch);

private:
    explicit CommandList(std::unique_ptr<detail::ObjectState> state) noexcept;
    [[nodiscard]] Status encoder_command(std::uint32_t opcode, ObjectId object,
                                         std::uint64_t arg0 = 0,
                                         std::uint64_t arg1 = 0);
    [[nodiscard]] Status end_encoder(std::uint32_t encoderKind);
    void abandon_encoder(std::uint32_t encoderKind) noexcept;

    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class RenderEncoder;
    friend class ComputeEncoder;
    friend class CopyEncoder;
    friend class Queue;
};

class RenderEncoder {
public:
    RenderEncoder() noexcept;
    ~RenderEncoder();
    RenderEncoder(RenderEncoder&&) noexcept;
    RenderEncoder& operator=(RenderEncoder&&) noexcept;
    RenderEncoder(const RenderEncoder&) = delete;
    RenderEncoder& operator=(const RenderEncoder&) = delete;

    [[nodiscard]] Status bind_pipeline(Pipeline& pipeline);
    [[nodiscard]] Status bind_vertex_buffer(std::uint32_t slot, Buffer& buffer,
                                            std::size_t offset = 0);
    [[nodiscard]] Status bind_uniform_buffer(std::uint32_t slot, Buffer& buffer,
                                             std::size_t offset = 0);
    [[nodiscard]] Status bind_index_buffer(Buffer& buffer,
                                           std::size_t offset = 0,
                                           IndexFormat format = IndexFormat::uint32);
    [[nodiscard]] Status bind_group(
        std::uint32_t group, BindGroup& bindGroup,
        std::span<const std::uint32_t> dynamicOffsets = {});
    [[nodiscard]] Status push_constants(ShaderStageMask stages,
                                        std::uint32_t offset,
                                        std::span<const std::byte> data);
    [[nodiscard]] Status set_viewports(std::uint32_t first,
                                       std::span<const Viewport> viewports);
    [[nodiscard]] Status set_scissors(std::uint32_t first,
                                      std::span<const ScissorRect> scissors);
    [[nodiscard]] Status set_blend_constant(
        const std::array<float, 4>& color);
    [[nodiscard]] Status set_stencil_reference(std::uint32_t reference);
    [[nodiscard]] Status set_depth_bias(float constantFactor,
                                        float slopeScale,
                                        float clamp);
    [[nodiscard]] Status draw(std::uint32_t vertexCount,
                              std::uint32_t instanceCount = 1,
                              std::uint32_t firstVertex = 0,
                              std::uint32_t firstInstance = 0);
    [[nodiscard]] Status draw_indexed(std::uint32_t indexCount,
                                      std::uint32_t instanceCount = 1,
                                      std::uint32_t firstIndex = 0,
                                      std::int32_t vertexOffset = 0,
                                      std::uint32_t firstInstance = 0);
    [[nodiscard]] Status draw_indirect(Buffer& buffer, std::size_t offset,
                                       bool indexed, std::uint32_t drawCount = 1,
                                       std::uint32_t stride = 0);
    [[nodiscard]] Status draw_indirect_count(
        Buffer& buffer, std::size_t offset, Buffer& countBuffer,
        std::size_t countOffset, std::uint32_t maximumDrawCount,
        std::uint32_t stride, bool indexed);
    [[nodiscard]] Status end();

private:
    explicit RenderEncoder(CommandList& list) noexcept;
    CommandList* list_ = nullptr;
    bool active_ = false;
    friend class CommandList;
};

class ComputeEncoder {
public:
    ComputeEncoder() noexcept;
    ~ComputeEncoder();
    ComputeEncoder(ComputeEncoder&&) noexcept;
    ComputeEncoder& operator=(ComputeEncoder&&) noexcept;
    ComputeEncoder(const ComputeEncoder&) = delete;
    ComputeEncoder& operator=(const ComputeEncoder&) = delete;

    [[nodiscard]] Status bind_pipeline(ComputePipeline& pipeline);
    [[nodiscard]] Status bind_storage_buffer(std::uint32_t slot, Buffer& buffer,
                                             std::size_t offset = 0);
    [[nodiscard]] Status bind_group(
        std::uint32_t group, BindGroup& bindGroup,
        std::span<const std::uint32_t> dynamicOffsets = {});
    [[nodiscard]] Status push_constants(std::uint32_t offset,
                                        std::span<const std::byte> data);
    [[nodiscard]] Status dispatch(std::uint32_t x, std::uint32_t y,
                                  std::uint32_t z);
    [[nodiscard]] Status dispatch_indirect(Buffer& buffer,
                                           std::size_t offset);
    [[nodiscard]] Status end();

private:
    explicit ComputeEncoder(CommandList& list) noexcept;
    CommandList* list_ = nullptr;
    bool active_ = false;
    friend class CommandList;
};

class CopyEncoder {
public:
    CopyEncoder() noexcept;
    ~CopyEncoder();
    CopyEncoder(CopyEncoder&&) noexcept;
    CopyEncoder& operator=(CopyEncoder&&) noexcept;
    CopyEncoder(const CopyEncoder&) = delete;
    CopyEncoder& operator=(const CopyEncoder&) = delete;

    [[nodiscard]] Status copy_buffer(Buffer& source, std::size_t sourceOffset,
                                     Buffer& destination,
                                     std::size_t destinationOffset,
                                     std::size_t size);
    [[nodiscard]] Status copy_buffer(Buffer& source, Buffer& destination,
                                     const BufferCopyRegion& region);
    [[nodiscard]] Status fill_buffer(Buffer& destination, std::size_t offset,
                                     std::size_t size, std::byte value);
    [[nodiscard]] Status copy_buffer_to_texture(
        Buffer& source, Texture& destination,
        const BufferTextureCopyRegion& region);
    [[nodiscard]] Status copy_texture_to_buffer(
        Texture& source, Buffer& destination,
        const BufferTextureCopyRegion& region);
    [[nodiscard]] Status copy_texture(Texture& source, Texture& destination,
                                      const TextureCopyRegion& region);
    [[nodiscard]] Status clear_texture(Texture& texture,
                                       const TextureRegion& region,
                                       const ClearValue& value = {});
    [[nodiscard]] Status resolve_texture(Texture& source, Texture& destination,
                                         const TextureCopyRegion& region);
    [[nodiscard]] Status blit_texture(Texture& source, Texture& destination,
                                      const TextureBlitRegion& region);
    [[nodiscard]] Status end();

private:
    explicit CopyEncoder(CommandList& list) noexcept;
    CommandList* list_ = nullptr;
    bool active_ = false;
    friend class CommandList;
};

class Queue {
public:
    Queue() noexcept;
    ~Queue();
    Queue(Queue&&) noexcept;
    Queue& operator=(Queue&&) noexcept;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] QueueKind kind() const;
    [[nodiscard]] Status submit(const QueueSubmitDesc& desc);
    [[nodiscard]] Status submit(std::span<CommandList* const> commandLists,
                                Fence* signalFence = nullptr,
                                std::uint64_t signalValue = 0);
    [[nodiscard]] Status present(const QueuePresentDesc& desc);
    [[nodiscard]] Status present(Swapchain& swapchain,
                                 std::uint32_t imageIndex);

private:
    explicit Queue(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class Fence {
public:
    Fence() noexcept;
    ~Fence();
    Fence(Fence&&) noexcept;
    Fence& operator=(Fence&&) noexcept;
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] std::uint64_t completed_value() const noexcept;
    [[nodiscard]] Status wait(std::uint64_t value,
                              std::chrono::nanoseconds timeout);

private:
    explicit Fence(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Queue;
};

class Semaphore {
public:
    Semaphore() noexcept;
    ~Semaphore();
    Semaphore(Semaphore&&) noexcept;
    Semaphore& operator=(Semaphore&&) noexcept;
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] std::uint64_t value() const noexcept;

private:
    explicit Semaphore(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Queue;
};

class QueryPool {
public:
    QueryPool() noexcept;
    ~QueryPool();
    QueryPool(QueryPool&&) noexcept;
    QueryPool& operator=(QueryPool&&) noexcept;
    QueryPool(const QueryPool&) = delete;
    QueryPool& operator=(const QueryPool&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] QueryPoolDesc desc() const;

private:
    explicit QueryPool(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
};

class Surface {
public:
    Surface() noexcept;
    ~Surface();
    Surface(Surface&&) noexcept;
    Surface& operator=(Surface&&) noexcept;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] SurfaceDesc desc() const;

private:
    explicit Surface(std::unique_ptr<detail::ObjectState> state) noexcept;
    std::unique_ptr<detail::ObjectState> state_;
    friend struct detail::Factory;
    friend class Device;
};

struct AcquireResult {
    Texture* image = nullptr;
    std::uint32_t imageIndex = 0;
    Status status = Status::success();
    Semaphore* available = nullptr;
    std::uint64_t availableValue = 0;

    [[nodiscard]] bool ok() const noexcept {
        return (status.ok() || status.code == StatusCode::suboptimal) &&
               image != nullptr;
    }
};

class Swapchain {
public:
    Swapchain() noexcept;
    ~Swapchain();
    Swapchain(Swapchain&&) noexcept;
    Swapchain& operator=(Swapchain&&) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] SwapchainDesc desc() const;
    [[nodiscard]] AcquireResult acquire_next_image();
    [[nodiscard]] Status resize(Extent2D extent);

private:
    explicit Swapchain(std::unique_ptr<detail::SwapchainState> state) noexcept;
    std::unique_ptr<detail::SwapchainState> state_;
    friend struct detail::Factory;
    friend class Queue;
};

struct FrameAllocation {
    Buffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0;
    void* mapped = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return buffer != nullptr && mapped != nullptr && size != 0;
    }
};

class UploadRing {
public:
    UploadRing() noexcept;
    ~UploadRing();
    UploadRing(UploadRing&&) noexcept;
    UploadRing& operator=(UploadRing&&) noexcept;
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] FrameAllocation allocate(std::size_t size,
                                           std::size_t alignment = 16);
    [[nodiscard]] Status advance();

private:
    explicit UploadRing(std::unique_ptr<detail::UploadRingState> state) noexcept;
    std::unique_ptr<detail::UploadRingState> state_;
    friend struct detail::Factory;
};

} // namespace truffle::rhi
