#include "truffle/rhi/direct3d_backend.hpp"

#include "foundation_backend.hpp"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#endif

namespace truffle::rhi {

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] Status direct3d_failure(StatusCode code, std::string message,
                                      HRESULT result) {
  BackendDiagnostic detail{
      .domain = "d3d12",
      .nativeCode = static_cast<std::int64_t>(result),
      .objectLabel = {},
      .message = message,
  };
  return Status::failure(code, std::move(message), std::move(detail));
}

[[nodiscard]] std::string utf8_name(const wchar_t *value) {
  if (value == nullptr || value[0] == L'\0') {
    return "Direct3D 12 WARP adapter";
  }
  const auto length = static_cast<int>(std::wcslen(value));
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return "Direct3D 12 WARP adapter";
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), required, nullptr,
                      nullptr);
  return result;
}

struct Direct3DContext {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  std::uint64_t fenceValue = 0;
  std::mutex mutex;
};

struct Direct3DBufferResource {
  std::shared_ptr<Direct3DContext> context;
  ComPtr<ID3D12Resource> resource;
  std::size_t size = 0;
  MemoryDomain memory = MemoryDomain::device_local;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  void *mapped = nullptr;
  std::mutex mutex;
};

struct Direct3DFormat {
  DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT shaderResource = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT unorderedAccess = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT renderTarget = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT depthStencil = DXGI_FORMAT_UNKNOWN;
  TextureAspect aspects = TextureAspect::none;
  std::size_t bytesPerPixel = 0;
  bool compressed = false;
};

[[nodiscard]] Direct3DFormat direct3d_format(TextureFormat format) {
  switch (format) {
  case TextureFormat::r8_unorm:
    return {DXGI_FORMAT_R8_TYPELESS,
            DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            1,
            false};
  case TextureFormat::rg8_unorm:
    return {DXGI_FORMAT_R8G8_TYPELESS,
            DXGI_FORMAT_R8G8_UNORM,
            DXGI_FORMAT_R8G8_UNORM,
            DXGI_FORMAT_R8G8_UNORM,
            DXGI_FORMAT_R8G8_UNORM,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            2,
            false};
  case TextureFormat::rgba8_unorm:
    return {DXGI_FORMAT_R8G8B8A8_TYPELESS,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            4,
            false};
  case TextureFormat::rgba8_srgb:
    return {DXGI_FORMAT_R8G8B8A8_TYPELESS,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            4,
            false};
  case TextureFormat::bgra8_unorm:
    return {DXGI_FORMAT_B8G8R8A8_TYPELESS,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            4,
            false};
  case TextureFormat::bgra8_srgb:
    return {DXGI_FORMAT_B8G8R8A8_TYPELESS,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            4,
            false};
  case TextureFormat::rgba16_float:
    return {DXGI_FORMAT_R16G16B16A16_TYPELESS,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            8,
            false};
  case TextureFormat::rgba32_float:
    return {DXGI_FORMAT_R32G32B32A32_TYPELESS,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_UNKNOWN,
            TextureAspect::color,
            16,
            false};
  case TextureFormat::depth16_unorm:
    return {DXGI_FORMAT_R16_TYPELESS,
            DXGI_FORMAT_D16_UNORM,
            DXGI_FORMAT_R16_UNORM,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_D16_UNORM,
            TextureAspect::depth,
            2,
            false};
  case TextureFormat::depth24_unorm_stencil8:
    return {DXGI_FORMAT_R24G8_TYPELESS,
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            TextureAspect::depth | TextureAspect::stencil,
            4,
            false};
  case TextureFormat::depth32_float:
    return {DXGI_FORMAT_R32_TYPELESS,
            DXGI_FORMAT_D32_FLOAT,
            DXGI_FORMAT_R32_FLOAT,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_D32_FLOAT,
            TextureAspect::depth,
            4,
            false};
  case TextureFormat::depth32_float_stencil8:
    return {DXGI_FORMAT_R32G8X24_TYPELESS,
            DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
            DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
            TextureAspect::depth | TextureAspect::stencil,
            8,
            false};
  case TextureFormat::bc1_rgba_unorm:
  case TextureFormat::bc1_rgba_srgb:
  case TextureFormat::bc3_rgba_unorm:
  case TextureFormat::bc3_rgba_srgb:
    return {.compressed = true};
  case TextureFormat::unknown:
    break;
  }
  return {};
}

struct Direct3DTextureResource {
  std::shared_ptr<Direct3DContext> context;
  ComPtr<ID3D12Resource> resource;
  TextureDesc desc;
  Direct3DFormat format;
  std::vector<D3D12_RESOURCE_STATES> states;
  std::mutex mutex;
};

struct Direct3DTextureViewResource {
  std::shared_ptr<Direct3DTextureResource> texture;
  std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
  D3D12_CPU_DESCRIPTOR_HANDLE shaderResource{};
  D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccess{};
  D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
  D3D12_CPU_DESCRIPTOR_HANDLE depthStencil{};
};

[[nodiscard]] D3D12_HEAP_TYPE direct3d_heap_type(MemoryDomain memory) {
  switch (memory) {
  case MemoryDomain::upload:
    return D3D12_HEAP_TYPE_UPLOAD;
  case MemoryDomain::readback:
    return D3D12_HEAP_TYPE_READBACK;
  case MemoryDomain::device_local:
    return D3D12_HEAP_TYPE_DEFAULT;
  case MemoryDomain::external:
    break;
  }
  return D3D12_HEAP_TYPE_CUSTOM;
}

[[nodiscard]] D3D12_RESOURCE_STATES direct3d_initial_state(MemoryDomain memory) {
  switch (memory) {
  case MemoryDomain::upload:
    return D3D12_RESOURCE_STATE_GENERIC_READ;
  case MemoryDomain::readback:
    return D3D12_RESOURCE_STATE_COPY_DEST;
  case MemoryDomain::device_local:
  case MemoryDomain::external:
    return D3D12_RESOURCE_STATE_COMMON;
  }
  return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] Result<ComPtr<ID3D12Resource>>
create_direct3d_buffer_resource(Direct3DContext &context, std::size_t size,
                                MemoryDomain memory) {
  const D3D12_HEAP_PROPERTIES heapProperties{
      .Type = direct3d_heap_type(memory),
      .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
      .CreationNodeMask = 1,
      .VisibleNodeMask = 1,
  };
  const D3D12_RESOURCE_DESC resourceDesc{
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Alignment = 0,
      .Width = size,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_UNKNOWN,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = D3D12_RESOURCE_FLAG_NONE,
  };
  ComPtr<ID3D12Resource> resource;
  const auto result = context.device->CreateCommittedResource(
      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
      direct3d_initial_state(memory), nullptr, IID_PPV_ARGS(&resource));
  if (FAILED(result)) {
    return direct3d_failure(result == E_OUTOFMEMORY ? StatusCode::out_of_memory
                                                    : StatusCode::backend_error,
                            "D3D12 buffer allocation failed", result);
  }
  return resource;
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_buffer(const std::shared_ptr<void> &nativeContext,
                       const BufferDesc &desc) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  if (desc.memory == MemoryDomain::external || desc.shareable) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 external buffer memory is not implemented");
  }
  std::lock_guard contextLock{context->mutex};
  auto native = create_direct3d_buffer_resource(*context, desc.size, desc.memory);
  if (!native.ok()) {
    return native.status();
  }
  try {
    auto resource = std::make_shared<Direct3DBufferResource>();
    resource->context = context;
    resource->resource = std::move(native).value();
    resource->size = desc.size;
    resource->memory = desc.memory;
    resource->state = direct3d_initial_state(desc.memory);
    if (desc.mappedAtCreation) {
      const D3D12_RANGE readRange{0, 0};
      const auto result = resource->resource->Map(0, &readRange, &resource->mapped);
      if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error, "D3D12 buffer mapping failed",
                                result);
      }
    }
    return std::static_pointer_cast<void>(std::move(resource));
  } catch (const std::bad_alloc &) {
    return Status::failure(StatusCode::out_of_memory,
                           "D3D12 buffer resource allocation failed");
  }
}

[[nodiscard]] D3D12_RESOURCE_DIMENSION
direct3d_texture_dimension(TextureDimension dimension) {
  switch (dimension) {
  case TextureDimension::d1:
    return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  case TextureDimension::d2:
  case TextureDimension::cube:
    return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  case TextureDimension::d3:
    return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  }
  return D3D12_RESOURCE_DIMENSION_UNKNOWN;
}

[[nodiscard]] D3D12_RESOURCE_FLAGS direct3d_texture_flags(TextureUsage usage) {
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (has_usage(usage, TextureUsage::storage)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  if (has_usage(usage, TextureUsage::color_attachment)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  if (has_usage(usage, TextureUsage::depth_stencil_attachment)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  }
  return flags;
}

[[nodiscard]] bool direct3d_format_supports(Direct3DContext &context,
                                            const TextureDesc &desc,
                                            const Direct3DFormat &format) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
  support.Format = format.view;
  if (FAILED(context.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support,
                                                 sizeof(support)))) {
    return false;
  }
  D3D12_FORMAT_SUPPORT1 dimensionSupport = D3D12_FORMAT_SUPPORT1_NONE;
  switch (desc.dimension) {
  case TextureDimension::d1:
    dimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE1D;
    break;
  case TextureDimension::d2:
  case TextureDimension::cube:
    dimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
    break;
  case TextureDimension::d3:
    dimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE3D;
    break;
  }
  D3D12_FORMAT_SUPPORT1 required = dimensionSupport;
  if (has_usage(desc.usage, TextureUsage::sampled)) {
    required |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
  }
  if (has_usage(desc.usage, TextureUsage::color_attachment)) {
    required |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  }
  if (has_usage(desc.usage, TextureUsage::depth_stencil_attachment)) {
    required |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  }
  if ((support.Support1 & required) != required) {
    return false;
  }
  if (has_usage(desc.usage, TextureUsage::storage) &&
      (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
    return false;
  }
  return true;
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_texture(const std::shared_ptr<void> &nativeContext,
                        const TextureDesc &desc) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  const auto format = direct3d_format(desc.format);
  const auto depthStencil = has_aspect(format.aspects, TextureAspect::depth) ||
                            has_aspect(format.aspects, TextureAspect::stencil);
  if (format.resource == DXGI_FORMAT_UNKNOWN || format.compressed ||
      desc.sampleCount != 1 || desc.memory != MemoryDomain::device_local ||
      desc.shareable || has_usage(desc.usage, TextureUsage::present) ||
      desc.mipLevels > std::numeric_limits<UINT16>::max() ||
      desc.arrayLayers > std::numeric_limits<UINT16>::max() ||
      (depthStencil && (has_usage(desc.usage, TextureUsage::storage) ||
                        has_usage(desc.usage, TextureUsage::color_attachment))) ||
      (!depthStencil && has_usage(desc.usage, TextureUsage::depth_stencil_attachment)) ||
      (desc.dimension == TextureDimension::d3 && depthStencil)) {
    return Status::failure(
        StatusCode::unsupported,
        "this D3D12 texture shape, format, sample count, usage, or memory "
        "mode is unsupported");
  }
  if (!direct3d_format_supports(*context, desc, format)) {
    return Status::failure(
        StatusCode::unsupported,
        "the D3D12 adapter does not support the requested texture format "
        "usage");
  }

  const auto depthOrArray =
      desc.dimension == TextureDimension::d3 ? desc.extent.depth : desc.arrayLayers;
  if (depthOrArray > std::numeric_limits<UINT16>::max()) {
    return Status::failure(StatusCode::unsupported,
                           "the D3D12 texture depth or array is too large");
  }
  const D3D12_HEAP_PROPERTIES heapProperties{
      .Type = D3D12_HEAP_TYPE_DEFAULT,
      .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
      .CreationNodeMask = 1,
      .VisibleNodeMask = 1,
  };
  const D3D12_RESOURCE_DESC resourceDesc{
      .Dimension = direct3d_texture_dimension(desc.dimension),
      .Alignment = 0,
      .Width = desc.extent.width,
      .Height = desc.extent.height,
      .DepthOrArraySize = static_cast<UINT16>(depthOrArray),
      .MipLevels = static_cast<UINT16>(desc.mipLevels),
      .Format = format.resource,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
      .Flags = direct3d_texture_flags(desc.usage),
  };
  ComPtr<ID3D12Resource> native;
  std::lock_guard contextLock{context->mutex};
  const auto result = context->device->CreateCommittedResource(
      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
      nullptr, IID_PPV_ARGS(&native));
  if (FAILED(result)) {
    return direct3d_failure(result == E_OUTOFMEMORY ? StatusCode::out_of_memory
                                                    : StatusCode::backend_error,
                            "D3D12 texture allocation failed", result);
  }
  try {
    auto resource = std::make_shared<Direct3DTextureResource>();
    resource->context = context;
    resource->resource = std::move(native);
    resource->desc = desc;
    resource->format = format;
    const auto subresourceCount =
        static_cast<std::size_t>(desc.mipLevels) *
        (desc.dimension == TextureDimension::d3 ? 1u : desc.arrayLayers);
    resource->states.assign(subresourceCount, D3D12_RESOURCE_STATE_COMMON);
    return std::static_pointer_cast<void>(std::move(resource));
  } catch (const std::bad_alloc &) {
    return Status::failure(StatusCode::out_of_memory,
                           "D3D12 texture resource allocation failed");
  }
}

[[nodiscard]] Result<ComPtr<ID3D12DescriptorHeap>>
create_direct3d_descriptor_heap(Direct3DContext &context,
                                D3D12_DESCRIPTOR_HEAP_TYPE type) {
  const D3D12_DESCRIPTOR_HEAP_DESC desc{
      .Type = type,
      .NumDescriptors = 1,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
      .NodeMask = 0,
  };
  ComPtr<ID3D12DescriptorHeap> heap;
  const auto result = context.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
  if (FAILED(result)) {
    return direct3d_failure(result == E_OUTOFMEMORY ? StatusCode::out_of_memory
                                                    : StatusCode::backend_error,
                            "D3D12 texture-view descriptor allocation failed", result);
  }
  return heap;
}

[[nodiscard]] std::uint32_t direct3d_mip_depth(const TextureDesc &desc,
                                               std::uint32_t mip) {
  return std::max(1u, desc.extent.depth >> mip);
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_texture_view(const std::shared_ptr<void> &nativeResource,
                             const TextureViewDesc &desc) {
  const auto texture = std::static_pointer_cast<Direct3DTextureResource>(nativeResource);
  const auto format = direct3d_format(desc.format);
  if (!texture || !texture->resource || format.resource == DXGI_FORMAT_UNKNOWN ||
      format.resource != texture->format.resource || format.compressed ||
      (desc.dimension == TextureDimension::cube &&
       (desc.range.baseArrayLayer % 6u != 0 || desc.range.arrayLayerCount % 6u != 0))) {
    return Status::failure(StatusCode::unsupported,
                           "this D3D12 texture view is unsupported");
  }
  const auto writableView =
      has_usage(texture->desc.usage, TextureUsage::storage) ||
      has_usage(texture->desc.usage, TextureUsage::color_attachment) ||
      has_usage(texture->desc.usage, TextureUsage::depth_stencil_attachment);
  if (writableView && desc.range.mipLevelCount != 1) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 writable texture views select exactly one mip level");
  }

  try {
    auto view = std::make_shared<Direct3DTextureViewResource>();
    view->texture = texture;
    const auto retainHeap =
        [&](D3D12_DESCRIPTOR_HEAP_TYPE type) -> Result<D3D12_CPU_DESCRIPTOR_HANDLE> {
      auto heapResult = create_direct3d_descriptor_heap(*texture->context, type);
      if (!heapResult.ok()) {
        return heapResult.status();
      }
      auto heap = std::move(heapResult).value();
      const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
      view->heaps.push_back(std::move(heap));
      return handle;
    };

    std::lock_guard contextLock{texture->context->mutex};
    if (has_usage(texture->desc.usage, TextureUsage::sampled)) {
      if (format.shaderResource == DXGI_FORMAT_UNKNOWN) {
        return Status::failure(StatusCode::unsupported,
                               "the D3D12 texture format has no shader-resource view");
      }
      D3D12_SHADER_RESOURCE_VIEW_DESC native{};
      native.Format = format.shaderResource;
      native.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      switch (desc.dimension) {
      case TextureDimension::d1:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
          native.Texture1D.MostDetailedMip = desc.range.baseMipLevel;
          native.Texture1D.MipLevels = desc.range.mipLevelCount;
        } else {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
          native.Texture1DArray.MostDetailedMip = desc.range.baseMipLevel;
          native.Texture1DArray.MipLevels = desc.range.mipLevelCount;
          native.Texture1DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture1DArray.ArraySize = desc.range.arrayLayerCount;
        }
        break;
      case TextureDimension::d2:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
          native.Texture2D.MostDetailedMip = desc.range.baseMipLevel;
          native.Texture2D.MipLevels = desc.range.mipLevelCount;
          native.Texture2D.PlaneSlice = 0;
        } else {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
          native.Texture2DArray.MostDetailedMip = desc.range.baseMipLevel;
          native.Texture2DArray.MipLevels = desc.range.mipLevelCount;
          native.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
          native.Texture2DArray.PlaneSlice = 0;
        }
        break;
      case TextureDimension::d3:
        native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        native.Texture3D.MostDetailedMip = desc.range.baseMipLevel;
        native.Texture3D.MipLevels = desc.range.mipLevelCount;
        break;
      case TextureDimension::cube:
        if (desc.range.arrayLayerCount == 6) {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
          native.TextureCube.MostDetailedMip = desc.range.baseMipLevel;
          native.TextureCube.MipLevels = desc.range.mipLevelCount;
        } else {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
          native.TextureCubeArray.MostDetailedMip = desc.range.baseMipLevel;
          native.TextureCubeArray.MipLevels = desc.range.mipLevelCount;
          native.TextureCubeArray.First2DArrayFace = desc.range.baseArrayLayer;
          native.TextureCubeArray.NumCubes = desc.range.arrayLayerCount / 6u;
        }
        break;
      }
      auto handle = retainHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      if (!handle.ok()) {
        return handle.status();
      }
      view->shaderResource = handle.value();
      texture->context->device->CreateShaderResourceView(texture->resource.Get(), &native,
                                                         view->shaderResource);
    }

    if (has_usage(texture->desc.usage, TextureUsage::storage)) {
      if (format.unorderedAccess == DXGI_FORMAT_UNKNOWN ||
          desc.dimension == TextureDimension::cube) {
        return Status::failure(
            StatusCode::unsupported,
            "the D3D12 texture format or dimension has no unordered-access view");
      }
      D3D12_UNORDERED_ACCESS_VIEW_DESC native{};
      native.Format = format.unorderedAccess;
      switch (desc.dimension) {
      case TextureDimension::d1:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
          native.Texture1D.MipSlice = desc.range.baseMipLevel;
        } else {
          native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
          native.Texture1DArray.MipSlice = desc.range.baseMipLevel;
          native.Texture1DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture1DArray.ArraySize = desc.range.arrayLayerCount;
        }
        break;
      case TextureDimension::d2:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
          native.Texture2D.MipSlice = desc.range.baseMipLevel;
          native.Texture2D.PlaneSlice = 0;
        } else {
          native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
          native.Texture2DArray.MipSlice = desc.range.baseMipLevel;
          native.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
          native.Texture2DArray.PlaneSlice = 0;
        }
        break;
      case TextureDimension::d3:
        native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        native.Texture3D.MipSlice = desc.range.baseMipLevel;
        native.Texture3D.FirstWSlice = 0;
        native.Texture3D.WSize =
            direct3d_mip_depth(texture->desc, desc.range.baseMipLevel);
        break;
      case TextureDimension::cube:
        return Status::failure(StatusCode::unsupported,
                               "D3D12 cube unordered-access views are unsupported");
      }
      auto handle = retainHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      if (!handle.ok()) {
        return handle.status();
      }
      view->unorderedAccess = handle.value();
      texture->context->device->CreateUnorderedAccessView(
          texture->resource.Get(), nullptr, &native, view->unorderedAccess);
    }

    if (has_usage(texture->desc.usage, TextureUsage::color_attachment)) {
      if (format.renderTarget == DXGI_FORMAT_UNKNOWN ||
          desc.dimension == TextureDimension::cube) {
        return Status::failure(
            StatusCode::unsupported,
            "the D3D12 texture format or dimension has no render-target view");
      }
      D3D12_RENDER_TARGET_VIEW_DESC native{};
      native.Format = format.renderTarget;
      switch (desc.dimension) {
      case TextureDimension::d1:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
          native.Texture1D.MipSlice = desc.range.baseMipLevel;
        } else {
          native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
          native.Texture1DArray.MipSlice = desc.range.baseMipLevel;
          native.Texture1DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture1DArray.ArraySize = desc.range.arrayLayerCount;
        }
        break;
      case TextureDimension::d2:
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
          native.Texture2D.MipSlice = desc.range.baseMipLevel;
          native.Texture2D.PlaneSlice = 0;
        } else {
          native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
          native.Texture2DArray.MipSlice = desc.range.baseMipLevel;
          native.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
          native.Texture2DArray.PlaneSlice = 0;
        }
        break;
      case TextureDimension::d3:
        native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        native.Texture3D.MipSlice = desc.range.baseMipLevel;
        native.Texture3D.FirstWSlice = 0;
        native.Texture3D.WSize =
            direct3d_mip_depth(texture->desc, desc.range.baseMipLevel);
        break;
      case TextureDimension::cube:
        return Status::failure(StatusCode::unsupported,
                               "D3D12 cube render-target views are unsupported");
      }
      auto handle = retainHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      if (!handle.ok()) {
        return handle.status();
      }
      view->renderTarget = handle.value();
      texture->context->device->CreateRenderTargetView(texture->resource.Get(), &native,
                                                       view->renderTarget);
    }

    if (has_usage(texture->desc.usage, TextureUsage::depth_stencil_attachment)) {
      if (format.depthStencil == DXGI_FORMAT_UNKNOWN ||
          desc.dimension == TextureDimension::d3) {
        return Status::failure(
            StatusCode::unsupported,
            "the D3D12 texture format or dimension has no depth-stencil view");
      }
      D3D12_DEPTH_STENCIL_VIEW_DESC native{};
      native.Format = format.depthStencil;
      native.Flags = D3D12_DSV_FLAG_NONE;
      if (desc.dimension == TextureDimension::d1) {
        if (texture->desc.arrayLayers == 1) {
          native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
          native.Texture1D.MipSlice = desc.range.baseMipLevel;
        } else {
          native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
          native.Texture1DArray.MipSlice = desc.range.baseMipLevel;
          native.Texture1DArray.FirstArraySlice = desc.range.baseArrayLayer;
          native.Texture1DArray.ArraySize = desc.range.arrayLayerCount;
        }
      } else if (texture->desc.arrayLayers == 1 &&
                 desc.dimension != TextureDimension::cube) {
        native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        native.Texture2D.MipSlice = desc.range.baseMipLevel;
      } else {
        native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        native.Texture2DArray.MipSlice = desc.range.baseMipLevel;
        native.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
        native.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
      }
      auto handle = retainHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
      if (!handle.ok()) {
        return handle.status();
      }
      view->depthStencil = handle.value();
      texture->context->device->CreateDepthStencilView(texture->resource.Get(), &native,
                                                       view->depthStencil);
    }

    return std::static_pointer_cast<void>(std::move(view));
  } catch (const std::bad_alloc &) {
    return Status::failure(StatusCode::out_of_memory,
                           "D3D12 texture-view allocation failed");
  }
}

[[nodiscard]] Status ensure_direct3d_buffer_mapped(Direct3DBufferResource &resource,
                                                   bool forRead) {
  if (resource.memory == MemoryDomain::device_local) {
    return Status::failure(StatusCode::unsupported,
                           "default-heap D3D12 buffers are not host mappable");
  }
  if (resource.mapped != nullptr) {
    return Status::success();
  }
  const D3D12_RANGE readRange =
      forRead ? D3D12_RANGE{0, resource.size} : D3D12_RANGE{0, 0};
  const auto result = resource.resource->Map(0, &readRange, &resource.mapped);
  return SUCCEEDED(result) ? Status::success()
                           : direct3d_failure(StatusCode::backend_error,
                                              "D3D12 buffer mapping failed", result);
}

[[nodiscard]] Result<std::span<std::byte>>
map_direct3d_buffer(const std::shared_ptr<void> &nativeResource) {
  const auto resource = std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
  if (!resource) {
    return Status::failure(StatusCode::invalid_argument,
                           "D3D12 buffer resource is invalid");
  }
  std::lock_guard lock{resource->mutex};
  if (auto status = ensure_direct3d_buffer_mapped(*resource, resource->memory ==
                                                                 MemoryDomain::readback);
      !status.ok()) {
    return status;
  }
  return std::span<std::byte>{static_cast<std::byte *>(resource->mapped), resource->size};
}

[[nodiscard]] Status unmap_direct3d_buffer(const std::shared_ptr<void> &nativeResource) {
  const auto resource = std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
  if (!resource) {
    return Status::failure(StatusCode::invalid_argument,
                           "D3D12 buffer resource is invalid");
  }
  std::lock_guard lock{resource->mutex};
  if (resource->mapped == nullptr) {
    return Status::failure(StatusCode::invalid_state, "D3D12 buffer is not mapped");
  }
  resource->resource->Unmap(0, nullptr);
  resource->mapped = nullptr;
  return Status::success();
}

[[nodiscard]] Status direct3d_buffer_range(const std::shared_ptr<void> &nativeResource,
                                           std::size_t offset, std::size_t size) {
  const auto resource = std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
  return resource && offset <= resource->size && size <= resource->size - offset
             ? Status::success()
             : Status::failure(StatusCode::invalid_argument,
                               "D3D12 buffer range is invalid");
}

[[nodiscard]] Status write_direct3d_buffer(const std::shared_ptr<void> &nativeResource,
                                           std::size_t offset,
                                           std::span<const std::byte> data) {
  const auto resource = std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
  if (auto status = direct3d_buffer_range(nativeResource, offset, data.size());
      !status.ok()) {
    return status;
  }
  std::lock_guard lock{resource->mutex};
  const bool temporary = resource->mapped == nullptr;
  if (auto status = ensure_direct3d_buffer_mapped(*resource, false); !status.ok()) {
    return status;
  }
  std::memcpy(static_cast<std::byte *>(resource->mapped) + offset, data.data(),
              data.size());
  if (temporary) {
    const D3D12_RANGE writtenRange{offset, offset + data.size()};
    resource->resource->Unmap(0, &writtenRange);
    resource->mapped = nullptr;
  }
  return Status::success();
}

[[nodiscard]] Status read_direct3d_buffer(const std::shared_ptr<void> &nativeResource,
                                          std::size_t offset, std::span<std::byte> data) {
  const auto resource = std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
  if (auto status = direct3d_buffer_range(nativeResource, offset, data.size());
      !status.ok()) {
    return status;
  }
  std::lock_guard lock{resource->mutex};
  const bool temporary = resource->mapped == nullptr;
  if (auto status = ensure_direct3d_buffer_mapped(*resource, true); !status.ok()) {
    return status;
  }
  std::memcpy(data.data(), static_cast<const std::byte *>(resource->mapped) + offset,
              data.size());
  if (temporary) {
    const D3D12_RANGE writtenRange{0, 0};
    resource->resource->Unmap(0, &writtenRange);
    resource->mapped = nullptr;
  }
  return Status::success();
}

[[nodiscard]] Status wait_for_fence(Direct3DContext &context, std::uint64_t value) {
  if (context.fence->GetCompletedValue() >= value) {
    return Status::success();
  }
  const auto eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (eventHandle == nullptr) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 fence event creation failed",
                            HRESULT_FROM_WIN32(GetLastError()));
  }
  const auto result = context.fence->SetEventOnCompletion(value, eventHandle);
  if (FAILED(result)) {
    CloseHandle(eventHandle);
    return direct3d_failure(StatusCode::backend_error, "D3D12 fence wait setup failed",
                            result);
  }
  const auto waitResult = WaitForSingleObject(eventHandle, 30000);
  CloseHandle(eventHandle);
  if (waitResult != WAIT_OBJECT_0) {
    return Status::failure(StatusCode::timeout, "D3D12 WARP fence wait timed out");
  }
  return Status::success();
}

[[nodiscard]] Status submit_empty(Direct3DContext &context) {
  ++context.fenceValue;
  const auto result = context.queue->Signal(context.fence.Get(), context.fenceValue);
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error, "D3D12 WARP queue signal failed",
                            result);
  }
  return wait_for_fence(context, context.fenceValue);
}

struct Direct3DProbe {
  std::shared_ptr<Direct3DContext> context;
  std::string adapterName;
  std::size_t deviceLocalBudget = 1024u * 1024u * 1024u;
};

[[nodiscard]] bool direct3d_transfer_range_valid(const Direct3DBufferResource &resource,
                                                 std::size_t offset, std::size_t size) {
  return size != 0 && offset <= resource.size && size <= resource.size - offset;
}

[[nodiscard]] std::uint32_t direct3d_subresource(const Direct3DTextureResource &texture,
                                                 const TextureSubresource &subresource) {
  const auto layer =
      texture.desc.dimension == TextureDimension::d3 ? 0u : subresource.arrayLayer;
  return subresource.mipLevel + layer * texture.desc.mipLevels;
}

[[nodiscard]] bool
direct3d_buffer_texture_region_valid(const Direct3DBufferResource &buffer,
                                     const Direct3DTextureResource &texture,
                                     const BufferTextureCopyRegion &region) {
  if (texture.format.aspects != TextureAspect::color ||
      region.texture.subresource.aspect != TextureAspect::color ||
      texture.format.bytesPerPixel == 0) {
    return false;
  }
  const auto tightRow = static_cast<std::size_t>(region.texture.extent.width) *
                        texture.format.bytesPerPixel;
  const auto rowPitch =
      region.layout.bytesPerRow == 0 ? tightRow : region.layout.bytesPerRow;
  const auto rowsPerImage = region.layout.rowsPerImage == 0 ? region.texture.extent.height
                                                            : region.layout.rowsPerImage;
  if (rowPitch < tightRow || rowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0 ||
      rowPitch > std::numeric_limits<UINT>::max() ||
      rowsPerImage < region.texture.extent.height ||
      rowsPerImage > std::numeric_limits<std::size_t>::max() / rowPitch) {
    return false;
  }
  const auto offset = region.bufferOffset + region.layout.offset;
  if (offset < region.bufferOffset ||
      offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0 || offset > buffer.size) {
    return false;
  }
  const auto depth = static_cast<std::size_t>(region.texture.extent.depth);
  const auto precedingImages = depth - 1u;
  const auto imagePitch = rowPitch * rowsPerImage;
  if (precedingImages > (buffer.size - offset) / imagePitch) {
    return false;
  }
  const auto imageOffset = precedingImages * imagePitch;
  const auto precedingRows = static_cast<std::size_t>(region.texture.extent.height) - 1u;
  return precedingRows <= (buffer.size - offset - imageOffset) / rowPitch &&
         tightRow <= buffer.size - offset - imageOffset - precedingRows * rowPitch;
}

[[nodiscard]] bool direct3d_texture_copy_valid(const Direct3DTextureResource &source,
                                               const Direct3DTextureResource &destination,
                                               const TextureCopyRegion &region) {
  return &source != &destination && source.format.view == destination.format.view &&
         source.format.aspects == TextureAspect::color &&
         destination.format.aspects == TextureAspect::color &&
         region.source.subresource.aspect == TextureAspect::color &&
         region.destination.subresource.aspect == TextureAspect::color;
}

[[nodiscard]] D3D12_TEXTURE_COPY_LOCATION
direct3d_buffer_copy_location(ID3D12Resource *buffer,
                              const Direct3DTextureResource &texture,
                              const BufferTextureCopyRegion &region) {
  const auto rowPitch = region.layout.bytesPerRow == 0
                            ? static_cast<std::size_t>(region.texture.extent.width) *
                                  texture.format.bytesPerPixel
                            : region.layout.bytesPerRow;
  D3D12_TEXTURE_COPY_LOCATION location{};
  location.pResource = buffer;
  location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  location.PlacedFootprint.Offset = region.bufferOffset + region.layout.offset;
  location.PlacedFootprint.Footprint.Format = texture.format.view;
  location.PlacedFootprint.Footprint.Width = region.texture.extent.width;
  location.PlacedFootprint.Footprint.Height = region.texture.extent.height;
  location.PlacedFootprint.Footprint.Depth = region.texture.extent.depth;
  location.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
  return location;
}

[[nodiscard]] D3D12_TEXTURE_COPY_LOCATION
direct3d_texture_copy_location(Direct3DTextureResource &texture,
                               const TextureSubresource &subresource) {
  D3D12_TEXTURE_COPY_LOCATION location{};
  location.pResource = texture.resource.Get();
  location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  location.SubresourceIndex = direct3d_subresource(texture, subresource);
  return location;
}

[[nodiscard]] Status
validate_direct3d_commands(std::span<const detail::NativeCommand> commands) {
  for (const auto &command : commands) {
    if (command.kind == detail::NativeCommandKind::barrier) {
      continue;
    }
    if (command.kind != detail::NativeCommandKind::transfer) {
      return Status::failure(
          StatusCode::unsupported,
          "the D3D12 resource slice supports transfer command lists only");
    }
    const auto &transfer = command.transfer;
    switch (transfer.kind) {
    case detail::NativeTransferKind::copy_buffer: {
      const auto source =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      if (!source || !destination ||
          !direct3d_transfer_range_valid(*source, transfer.buffer.sourceOffset,
                                         transfer.buffer.size) ||
          !direct3d_transfer_range_valid(*destination, transfer.buffer.destinationOffset,
                                         transfer.buffer.size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 buffer copy range is invalid");
      }
      break;
    }
    case detail::NativeTransferKind::fill_buffer: {
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      if (!destination ||
          !direct3d_transfer_range_valid(*destination, transfer.buffer.destinationOffset,
                                         transfer.buffer.size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 buffer fill range is invalid");
      }
      break;
    }
    case detail::NativeTransferKind::copy_buffer_to_texture: {
      const auto source =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.destination);
      if (!source || !destination ||
          !direct3d_buffer_texture_region_valid(*source, *destination,
                                                transfer.bufferTexture)) {
        return Status::failure(
            StatusCode::invalid_argument,
            "D3D12 buffer-to-texture resources or footprint are invalid");
      }
      break;
    }
    case detail::NativeTransferKind::copy_texture_to_buffer: {
      const auto source =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      if (!source || !destination ||
          !direct3d_buffer_texture_region_valid(*destination, *source,
                                                transfer.bufferTexture)) {
        return Status::failure(
            StatusCode::invalid_argument,
            "D3D12 texture-to-buffer resources or footprint are invalid");
      }
      break;
    }
    case detail::NativeTransferKind::copy_texture: {
      const auto source =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.destination);
      if (!source || !destination ||
          !direct3d_texture_copy_valid(*source, *destination, transfer.texture)) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 texture-copy resources or formats are invalid");
      }
      break;
    }
    case detail::NativeTransferKind::clear_texture:
    case detail::NativeTransferKind::resolve_texture:
    case detail::NativeTransferKind::blit_texture:
      return Status::failure(StatusCode::unsupported,
                             "this D3D12 texture transfer is not implemented");
    }
  }
  return Status::success();
}

[[nodiscard]] Status transition_direct3d_buffer(ID3D12GraphicsCommandList &commandList,
                                                Direct3DBufferResource &resource,
                                                D3D12_RESOURCE_STATES target) {
  std::lock_guard lock{resource.mutex};
  if (resource.memory == MemoryDomain::upload) {
    return target == D3D12_RESOURCE_STATE_COPY_SOURCE
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "D3D12 upload buffers are copy sources only");
  }
  if (resource.memory == MemoryDomain::readback) {
    return target == D3D12_RESOURCE_STATE_COPY_DEST
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "D3D12 readback buffers are copy destinations only");
  }
  if (resource.state == target) {
    return Status::success();
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource.resource.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = resource.state;
  barrier.Transition.StateAfter = target;
  commandList.ResourceBarrier(1, &barrier);
  resource.state = target;
  return Status::success();
}

void transition_direct3d_texture(ID3D12GraphicsCommandList &commandList,
                                 Direct3DTextureResource &resource,
                                 const TextureSubresource &subresource,
                                 D3D12_RESOURCE_STATES target) {
  std::lock_guard lock{resource.mutex};
  const auto index = direct3d_subresource(resource, subresource);
  auto &state = resource.states[index];
  if (state == target) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource.resource.Get();
  barrier.Transition.Subresource = index;
  barrier.Transition.StateBefore = state;
  barrier.Transition.StateAfter = target;
  commandList.ResourceBarrier(1, &barrier);
  state = target;
}

[[nodiscard]] Status
record_direct3d_commands(Direct3DContext &context, ID3D12GraphicsCommandList &commandList,
                         std::span<const detail::NativeCommand> commands,
                         std::vector<ComPtr<ID3D12Resource>> &transients) {
  for (const auto &command : commands) {
    if (command.kind == detail::NativeCommandKind::barrier) {
      continue;
    }
    const auto &transfer = command.transfer;
    switch (transfer.kind) {
    case detail::NativeTransferKind::copy_buffer: {
      const auto source =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      if (auto status = transition_direct3d_buffer(commandList, *source,
                                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
          !status.ok()) {
        return status;
      }
      if (auto status = transition_direct3d_buffer(commandList, *destination,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);
          !status.ok()) {
        return status;
      }
      commandList.CopyBufferRegion(
          destination->resource.Get(), transfer.buffer.destinationOffset,
          source->resource.Get(), transfer.buffer.sourceOffset, transfer.buffer.size);
      break;
    }
    case detail::NativeTransferKind::fill_buffer: {
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      auto upload = create_direct3d_buffer_resource(context, transfer.buffer.size,
                                                    MemoryDomain::upload);
      if (!upload.ok()) {
        return upload.status();
      }
      auto staging = std::move(upload).value();
      void *mapped = nullptr;
      const D3D12_RANGE readRange{0, 0};
      auto result = staging->Map(0, &readRange, &mapped);
      if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 fill staging map failed", result);
      }
      std::memset(mapped, std::to_integer<unsigned char>(transfer.fillValue),
                  transfer.buffer.size);
      const D3D12_RANGE writtenRange{0, transfer.buffer.size};
      staging->Unmap(0, &writtenRange);
      if (auto status = transition_direct3d_buffer(commandList, *destination,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);
          !status.ok()) {
        return status;
      }
      commandList.CopyBufferRegion(destination->resource.Get(),
                                   transfer.buffer.destinationOffset, staging.Get(), 0,
                                   transfer.buffer.size);
      transients.push_back(std::move(staging));
      break;
    }
    case detail::NativeTransferKind::copy_buffer_to_texture: {
      const auto source =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.destination);
      if (auto status = transition_direct3d_buffer(commandList, *source,
                                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
          !status.ok()) {
        return status;
      }
      transition_direct3d_texture(commandList, *destination,
                                  transfer.bufferTexture.texture.subresource,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
      auto sourceLocation = direct3d_buffer_copy_location(
          source->resource.Get(), *destination, transfer.bufferTexture);
      auto destinationLocation = direct3d_texture_copy_location(
          *destination, transfer.bufferTexture.texture.subresource);
      commandList.CopyTextureRegion(
          &destinationLocation, transfer.bufferTexture.texture.origin.x,
          transfer.bufferTexture.texture.origin.y,
          transfer.bufferTexture.texture.origin.z, &sourceLocation, nullptr);
      break;
    }
    case detail::NativeTransferKind::copy_texture_to_buffer: {
      const auto source =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DBufferResource>(transfer.destination);
      transition_direct3d_texture(commandList, *source,
                                  transfer.bufferTexture.texture.subresource,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
      if (auto status = transition_direct3d_buffer(commandList, *destination,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);
          !status.ok()) {
        return status;
      }
      auto sourceLocation = direct3d_texture_copy_location(
          *source, transfer.bufferTexture.texture.subresource);
      auto destinationLocation = direct3d_buffer_copy_location(
          destination->resource.Get(), *source, transfer.bufferTexture);
      const auto &region = transfer.bufferTexture.texture;
      const D3D12_BOX sourceBox{
          .left = region.origin.x,
          .top = region.origin.y,
          .front = region.origin.z,
          .right = region.origin.x + region.extent.width,
          .bottom = region.origin.y + region.extent.height,
          .back = region.origin.z + region.extent.depth,
      };
      commandList.CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation,
                                    &sourceBox);
      break;
    }
    case detail::NativeTransferKind::copy_texture: {
      const auto source =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.source);
      const auto destination =
          std::static_pointer_cast<Direct3DTextureResource>(transfer.destination);
      transition_direct3d_texture(commandList, *source,
                                  transfer.texture.source.subresource,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
      transition_direct3d_texture(commandList, *destination,
                                  transfer.texture.destination.subresource,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
      auto sourceLocation =
          direct3d_texture_copy_location(*source, transfer.texture.source.subresource);
      auto destinationLocation = direct3d_texture_copy_location(
          *destination, transfer.texture.destination.subresource);
      const auto &sourceRegion = transfer.texture.source;
      const auto &destinationRegion = transfer.texture.destination;
      const D3D12_BOX sourceBox{
          .left = sourceRegion.origin.x,
          .top = sourceRegion.origin.y,
          .front = sourceRegion.origin.z,
          .right = sourceRegion.origin.x + sourceRegion.extent.width,
          .bottom = sourceRegion.origin.y + sourceRegion.extent.height,
          .back = sourceRegion.origin.z + sourceRegion.extent.depth,
      };
      commandList.CopyTextureRegion(
          &destinationLocation, destinationRegion.origin.x, destinationRegion.origin.y,
          destinationRegion.origin.z, &sourceLocation, &sourceBox);
      break;
    }
    case detail::NativeTransferKind::clear_texture:
    case detail::NativeTransferKind::resolve_texture:
    case detail::NativeTransferKind::blit_texture:
      return Status::failure(StatusCode::unsupported,
                             "this D3D12 texture transfer is not implemented");
    }
  }
  return Status::success();
}

[[nodiscard]] Result<Direct3DProbe> initialize_direct3d(const InstanceDesc &desc) {
  UINT factoryFlags = 0;
  if (desc.enableValidation) {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }

  ComPtr<IDXGIFactory6> factory;
  auto result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
  if (FAILED(result) && factoryFlags != 0) {
    factoryFlags = 0;
    result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
  }
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::unavailable, "DXGI factory creation failed",
                            result);
  }
  ComPtr<IDXGIAdapter1> adapter;
  result = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::unavailable,
                            "DXGI did not expose the WARP adapter", result);
  }

  auto context = std::make_shared<Direct3DContext>();
  result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                             IID_PPV_ARGS(&context->device));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::unavailable, "D3D12 WARP device creation failed",
                            result);
  }
  const D3D12_COMMAND_QUEUE_DESC queueDesc{
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0,
  };
  result = context->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&context->queue));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 WARP command-queue creation failed", result);
  }
  result = context->device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&context->fence));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error, "D3D12 WARP fence creation failed",
                            result);
  }
  ComPtr<ID3D12CommandAllocator> allocator;
  result = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&allocator));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 WARP command-allocator creation failed", result);
  }
  ComPtr<ID3D12GraphicsCommandList> commandList;
  result = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&commandList));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 WARP command-list creation failed", result);
  }
  result = commandList->Close();
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 WARP command-list close failed", result);
  }
  ID3D12CommandList *commandLists[] = {commandList.Get()};
  context->queue->ExecuteCommandLists(1, commandLists);
  if (auto status = submit_empty(*context); !status.ok()) {
    return status;
  }

  DXGI_ADAPTER_DESC1 adapterDesc{};
  adapter->GetDesc1(&adapterDesc);
  return Direct3DProbe{
      .context = std::move(context),
      .adapterName = utf8_name(adapterDesc.Description),
      .deviceLocalBudget = adapterDesc.DedicatedVideoMemory != 0
                               ? adapterDesc.DedicatedVideoMemory
                               : 1024u * 1024u * 1024u,
  };
}

[[nodiscard]] Status
submit_direct3d_commands(const std::shared_ptr<void> &nativeContext,
                         std::span<const detail::NativeCommand> commands,
                         std::span<const detail::NativeSemaphorePoint> waits,
                         std::span<const detail::NativeSemaphorePoint> signals) {
  if (!waits.empty() || !signals.empty()) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 timeline semaphore submission is not implemented");
  }
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device || !context->queue || !context->fence) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  std::lock_guard lock{context->mutex};
  if (auto status = validate_direct3d_commands(commands); !status.ok()) {
    return status;
  }
  ComPtr<ID3D12CommandAllocator> allocator;
  auto result = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&allocator));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 command-allocator creation failed", result);
  }
  ComPtr<ID3D12GraphicsCommandList> commandList;
  result = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&commandList));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 command-list creation failed", result);
  }
  std::vector<ComPtr<ID3D12Resource>> transients;
  if (auto status =
          record_direct3d_commands(*context, *commandList.Get(), commands, transients);
      !status.ok()) {
    return status;
  }
  result = commandList->Close();
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error, "D3D12 command-list close failed",
                            result);
  }
  ID3D12CommandList *lists[] = {commandList.Get()};
  context->queue->ExecuteCommandLists(1, lists);
  return submit_empty(*context);
}

} // namespace
#endif

Result<Instance> create_direct3d12_instance(const InstanceDesc &desc) {
#ifdef _WIN32
  auto probe = initialize_direct3d(desc);
  if (!probe.ok()) {
    return probe.status();
  }
  auto native = std::move(probe).value();
  detail::FoundationBackendConfig config;
  config.kind = BackendKind::direct3d12;
  config.platform = PlatformKind::windows;
  config.maturity = BackendMaturity::native_smoke;
  config.adapterName = std::move(native.adapterName);
  config.queueKinds = {QueueKind::graphics};
  config.supportedFeatures = {Feature::transfer, Feature::memory_budget};
  config.resourceCapabilities = {
      .bufferViews = true,
      .textureViews = true,
      .hostCoherent = true,
      .bufferCopy = true,
      .bufferFill = true,
      .bufferTextureCopy = true,
      .textureCopy = true,
      .textureClear = false,
      .textureResolve = false,
      .textureBlitNearest = false,
      .textureBlitLinear = false,
      .externalImport = false,
      .externalExport = false,
  };
  config.deviceLocalBudgetBytes = native.deviceLocalBudget;
  config.native = true;
  config.nativeContext = std::move(native.context);
  config.createBuffer = &create_direct3d_buffer;
  config.mapBuffer = &map_direct3d_buffer;
  config.unmapBuffer = &unmap_direct3d_buffer;
  config.flushBuffer = &direct3d_buffer_range;
  config.invalidateBuffer = &direct3d_buffer_range;
  config.writeBuffer = &write_direct3d_buffer;
  config.readBuffer = &read_direct3d_buffer;
  config.createTexture = &create_direct3d_texture;
  config.createTextureView = &create_direct3d_texture_view;
  config.nativeSubmit = &submit_direct3d_commands;
  return detail::create_foundation_instance(desc, std::move(config));
#else
  (void)desc;
  return detail::unavailable_backend(BackendKind::direct3d12, "Direct3D 12");
#endif
}

} // namespace truffle::rhi
