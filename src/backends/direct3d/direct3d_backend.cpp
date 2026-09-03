#include "truffle/rhi/direct3d_backend.hpp"

#include "foundation_backend.hpp"

#ifdef _WIN32
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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
  BufferUsage usage = BufferUsage::none;
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
  TextureViewDesc desc;
  std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
  D3D12_CPU_DESCRIPTOR_HANDLE shaderResource{};
  D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccess{};
  D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
  D3D12_CPU_DESCRIPTOR_HANDLE depthStencil{};
};

struct Direct3DSamplerResource {
  D3D12_SAMPLER_DESC desc{};
};

struct Direct3DSemaphoreResource {
  ComPtr<ID3D12Fence> fence;
};

struct Direct3DShaderResource {
  std::shared_ptr<Direct3DContext> context;
  ComPtr<ID3DBlob> ownedBytecode;
  std::vector<std::byte> bytecode;
  ShaderDesc desc;
};

struct Direct3DBindingSlot {
  std::uint32_t group = 0;
  std::uint32_t binding = 0;
  BindingType type = BindingType::uniform_buffer;
  std::uint32_t descriptorCount = 1;
  std::uint32_t rootParameter = 0;
  bool readOnly = true;
};

struct Direct3DPipelineResource {
  ComPtr<ID3D12RootSignature> rootSignature;
  ComPtr<ID3D12PipelineState> pipeline;
  PipelineDesc desc;
  std::vector<Direct3DBindingSlot> bindings;
  std::uint32_t pushConstantParameter = UINT_MAX;
};

struct Direct3DComputePipelineResource {
  ComPtr<ID3D12RootSignature> rootSignature;
  ComPtr<ID3D12PipelineState> pipeline;
  ComputePipelineDesc desc;
  std::vector<Direct3DBindingSlot> bindings;
  std::uint32_t pushConstantParameter = UINT_MAX;
};

[[nodiscard]] D3D12_SHADER_VISIBILITY direct3d_shader_visibility(ShaderStageMask stages) {
  if (stages == ShaderStageMask::vertex) {
    return D3D12_SHADER_VISIBILITY_VERTEX;
  }
  if (stages == ShaderStageMask::fragment) {
    return D3D12_SHADER_VISIBILITY_PIXEL;
  }
  return D3D12_SHADER_VISIBILITY_ALL;
}

[[nodiscard]] D3D12_DESCRIPTOR_RANGE_TYPE direct3d_range_type(BindingType type,
                                                              bool readOnly) {
  switch (type) {
  case BindingType::uniform_buffer:
    return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  case BindingType::storage_buffer:
    return readOnly ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  case BindingType::sampled_texture:
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  case BindingType::storage_texture:
    return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  case BindingType::sampler:
    return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  }
  return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

[[nodiscard]] const ShaderBindingMap *direct3d_binding_map(const ShaderDesc &shader,
                                                           std::uint32_t group,
                                                           std::uint32_t binding) {
  const auto found = std::find_if(
      shader.bindingMap.begin(), shader.bindingMap.end(), [&](const auto &mapping) {
        return mapping.group == group && mapping.binding == binding &&
               mapping.arrayElement == 0;
      });
  return found == shader.bindingMap.end() ? nullptr : &*found;
}

struct Direct3DRootLayout {
  ComPtr<ID3D12RootSignature> signature;
  std::vector<Direct3DBindingSlot> bindings;
  std::uint32_t pushConstantParameter = UINT_MAX;
};

[[nodiscard]] Result<Direct3DRootLayout> create_direct3d_root_layout(
    Direct3DContext &context,
    std::span<const std::shared_ptr<Direct3DShaderResource>> shaders,
    const detail::NativePipelineLayout &layout, bool graphics) {
  struct ReflectedSlot {
    Direct3DBindingSlot slot;
    ShaderStageMask visibility = ShaderStageMask::none;
    std::uint32_t nativeGroup = 0;
    std::uint32_t nativeBinding = 0;
  };
  std::vector<ReflectedSlot> reflected;
  for (const auto &shader : shaders) {
    if (!shader) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 root layout has an invalid shader");
    }
    for (const auto &binding : shader->desc.reflection) {
      const auto logicalType = [&] {
        if (binding.type == ResourceBindingType::sampler) {
          return BindingType::sampler;
        }
        if (binding.type == ResourceBindingType::texture) {
          return binding.readOnly ? BindingType::sampled_texture
                                  : BindingType::storage_texture;
        }
        return binding.readOnly ? BindingType::uniform_buffer
                                : BindingType::storage_buffer;
      }();
      const auto existing =
          std::find_if(reflected.begin(), reflected.end(), [&](const auto &candidate) {
            return candidate.slot.group == binding.group &&
                   candidate.slot.binding == binding.binding;
          });
      const auto *mapping =
          direct3d_binding_map(shader->desc, binding.group, binding.binding);
      const auto nativeGroup = mapping ? mapping->nativeGroup : binding.group;
      const auto nativeBinding = mapping ? mapping->nativeBinding : binding.binding;
      if (existing != reflected.end()) {
        if (existing->slot.type != logicalType ||
            existing->slot.descriptorCount != binding.arrayCount ||
            existing->nativeGroup != nativeGroup ||
            existing->nativeBinding != nativeBinding) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 shader binding maps disagree across stages");
        }
        existing->visibility = existing->visibility | shader_stage_mask(binding.stage);
        existing->slot.readOnly = existing->slot.readOnly && binding.readOnly;
        continue;
      }
      reflected.push_back({
          .slot = {.group = binding.group,
                   .binding = binding.binding,
                   .type = logicalType,
                   .descriptorCount = binding.arrayCount,
                   .readOnly = binding.readOnly},
          .visibility = shader_stage_mask(binding.stage),
          .nativeGroup = nativeGroup,
          .nativeBinding = nativeBinding,
      });
    }
  }
  std::sort(reflected.begin(), reflected.end(), [](const auto &lhs, const auto &rhs) {
    return std::pair{lhs.slot.group, lhs.slot.binding} <
           std::pair{rhs.slot.group, rhs.slot.binding};
  });
  for (auto &entry : reflected) {
    const auto group = std::find_if(
        layout.bindGroups.begin(), layout.bindGroups.end(),
        [&](const auto &candidate) { return candidate.group == entry.slot.group; });
    if (group == layout.bindGroups.end()) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 shader binding has no pipeline layout group");
    }
    const auto binding = std::find_if(
        group->entries.begin(), group->entries.end(),
        [&](const auto &candidate) { return candidate.binding == entry.slot.binding; });
    if (binding == group->entries.end()) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 shader binding has no pipeline layout entry");
    }
    entry.slot.type = binding->type;
    entry.slot.descriptorCount = binding->arrayCount;
    entry.visibility = binding->visibility;
  }

  std::uint32_t pushBytes = 0;
  ShaderStageMask pushVisibility = ShaderStageMask::none;
  for (const auto &range : layout.pushConstants) {
    pushBytes = std::max(pushBytes, range.offset + range.size);
    pushVisibility = pushVisibility | shader_stage_mask(range.stage);
  }
  if (pushBytes > 256 || pushBytes % 4 != 0) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 push constants are limited to 256 aligned bytes");
  }

  std::vector<D3D12_DESCRIPTOR_RANGE> ranges(reflected.size());
  std::vector<D3D12_ROOT_PARAMETER> parameters(reflected.size() +
                                               (pushBytes == 0 ? 0u : 1u));
  for (std::size_t index = 0; index < reflected.size(); ++index) {
    auto &entry = reflected[index];
    entry.slot.rootParameter = static_cast<std::uint32_t>(index);
    ranges[index] = {
        .RangeType = direct3d_range_type(entry.slot.type, entry.slot.readOnly),
        .NumDescriptors = entry.slot.descriptorCount,
        .BaseShaderRegister = entry.nativeBinding,
        .RegisterSpace = entry.nativeGroup,
        .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
    };
    parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[index].DescriptorTable = {.NumDescriptorRanges = 1,
                                         .pDescriptorRanges = &ranges[index]};
    parameters[index].ShaderVisibility = direct3d_shader_visibility(entry.visibility);
  }
  std::uint32_t pushParameter = UINT_MAX;
  if (pushBytes != 0) {
    pushParameter = static_cast<std::uint32_t>(reflected.size());
    parameters[pushParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[pushParameter].Constants = {
        .ShaderRegister = 255,
        .RegisterSpace = 0,
        .Num32BitValues = pushBytes / 4,
    };
    parameters[pushParameter].ShaderVisibility =
        direct3d_shader_visibility(pushVisibility);
  }
  const D3D12_ROOT_SIGNATURE_DESC desc{
      .NumParameters = static_cast<UINT>(parameters.size()),
      .pParameters = parameters.data(),
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = graphics ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                        : D3D12_ROOT_SIGNATURE_FLAG_NONE,
  };
  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  auto result = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                            &serialized, &errors);
  if (FAILED(result)) {
    const auto message =
        errors ? std::string{static_cast<const char *>(errors->GetBufferPointer()),
                             errors->GetBufferSize()}
               : std::string{"D3D12 root-signature serialization failed"};
    return direct3d_failure(StatusCode::invalid_argument, message, result);
  }
  Direct3DRootLayout output;
  result = context.device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                               serialized->GetBufferSize(),
                                               IID_PPV_ARGS(&output.signature));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 root-signature creation failed", result);
  }
  output.bindings.reserve(reflected.size());
  for (auto &entry : reflected) {
    output.bindings.push_back(std::move(entry.slot));
  }
  output.pushConstantParameter = pushParameter;
  return output;
}

[[nodiscard]] D3D12_COMPARISON_FUNC direct3d_compare(CompareOp operation) {
  return static_cast<D3D12_COMPARISON_FUNC>(D3D12_COMPARISON_FUNC_NEVER +
                                            static_cast<int>(operation));
}

[[nodiscard]] D3D12_STENCIL_OP direct3d_stencil(StencilOp operation) {
  switch (operation) {
  case StencilOp::keep:
    return D3D12_STENCIL_OP_KEEP;
  case StencilOp::zero:
    return D3D12_STENCIL_OP_ZERO;
  case StencilOp::replace:
    return D3D12_STENCIL_OP_REPLACE;
  case StencilOp::increment_clamp:
    return D3D12_STENCIL_OP_INCR_SAT;
  case StencilOp::decrement_clamp:
    return D3D12_STENCIL_OP_DECR_SAT;
  case StencilOp::invert:
    return D3D12_STENCIL_OP_INVERT;
  case StencilOp::increment_wrap:
    return D3D12_STENCIL_OP_INCR;
  case StencilOp::decrement_wrap:
    return D3D12_STENCIL_OP_DECR;
  }
  return D3D12_STENCIL_OP_KEEP;
}

[[nodiscard]] D3D12_BLEND direct3d_blend(BlendFactor factor) {
  switch (factor) {
  case BlendFactor::zero:
    return D3D12_BLEND_ZERO;
  case BlendFactor::one:
    return D3D12_BLEND_ONE;
  case BlendFactor::source_color:
    return D3D12_BLEND_SRC_COLOR;
  case BlendFactor::one_minus_source_color:
    return D3D12_BLEND_INV_SRC_COLOR;
  case BlendFactor::destination_color:
    return D3D12_BLEND_DEST_COLOR;
  case BlendFactor::one_minus_destination_color:
    return D3D12_BLEND_INV_DEST_COLOR;
  case BlendFactor::source_alpha:
    return D3D12_BLEND_SRC_ALPHA;
  case BlendFactor::one_minus_source_alpha:
    return D3D12_BLEND_INV_SRC_ALPHA;
  case BlendFactor::destination_alpha:
    return D3D12_BLEND_DEST_ALPHA;
  case BlendFactor::one_minus_destination_alpha:
    return D3D12_BLEND_INV_DEST_ALPHA;
  }
  return D3D12_BLEND_ONE;
}

[[nodiscard]] D3D12_BLEND_OP direct3d_blend_operation(BlendOp operation) {
  return static_cast<D3D12_BLEND_OP>(D3D12_BLEND_OP_ADD + static_cast<int>(operation));
}

[[nodiscard]] DXGI_FORMAT direct3d_vertex_format(VertexFormat format) {
  switch (format) {
  case VertexFormat::float32:
    return DXGI_FORMAT_R32_FLOAT;
  case VertexFormat::float32x2:
    return DXGI_FORMAT_R32G32_FLOAT;
  case VertexFormat::float32x3:
    return DXGI_FORMAT_R32G32B32_FLOAT;
  case VertexFormat::float32x4:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case VertexFormat::uint32:
    return DXGI_FORMAT_R32_UINT;
  case VertexFormat::uint32x2:
    return DXGI_FORMAT_R32G32_UINT;
  case VertexFormat::uint32x3:
    return DXGI_FORMAT_R32G32B32_UINT;
  case VertexFormat::uint32x4:
    return DXGI_FORMAT_R32G32B32A32_UINT;
  }
  return DXGI_FORMAT_UNKNOWN;
}

[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE
direct3d_topology_type(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::point_list:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  case PrimitiveTopology::line_list:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  case PrimitiveTopology::triangle_list:
  case PrimitiveTopology::triangle_strip:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  case PrimitiveTopology::patch_list:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
  }
  return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

[[nodiscard]] D3D_PRIMITIVE_TOPOLOGY direct3d_topology(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::point_list:
    return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
  case PrimitiveTopology::line_list:
    return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
  case PrimitiveTopology::triangle_strip:
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
  case PrimitiveTopology::triangle_list:
  case PrimitiveTopology::patch_list:
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
  return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE direct3d_address_mode(SamplerAddressMode mode) {
  switch (mode) {
  case SamplerAddressMode::clamp_to_edge:
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  case SamplerAddressMode::repeat:
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  case SamplerAddressMode::mirror_repeat:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
  }
  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_sampler(const std::shared_ptr<void> &nativeContext,
                        const SamplerDesc &desc) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  auto sampler = std::make_shared<Direct3DSamplerResource>();
  const auto anisotropic = desc.maxAnisotropy > 1.0F;
  const auto comparison = desc.compare != CompareOp::always;
  if (anisotropic) {
    sampler->desc.Filter =
        comparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
  } else {
    const auto min = desc.minFilter == Filter::linear;
    const auto mag = desc.magFilter == Filter::linear;
    const auto mip = desc.mipFilter == Filter::linear;
    const auto reduction = comparison ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
                                      : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    sampler->desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        min ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        mag ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        mip ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, reduction);
  }
  sampler->desc.AddressU = direct3d_address_mode(desc.addressU);
  sampler->desc.AddressV = direct3d_address_mode(desc.addressV);
  sampler->desc.AddressW = direct3d_address_mode(desc.addressW);
  sampler->desc.MipLODBias = 0.0F;
  sampler->desc.MaxAnisotropy =
      static_cast<UINT>(std::clamp(desc.maxAnisotropy, 1.0F, 16.0F));
  sampler->desc.ComparisonFunc = direct3d_compare(desc.compare);
  sampler->desc.MinLOD = desc.lodMin;
  sampler->desc.MaxLOD = desc.lodMax;
  return std::static_pointer_cast<void>(std::move(sampler));
}

[[nodiscard]] const char *direct3d_shader_profile(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::vertex:
    return "vs_5_1";
  case ShaderStage::fragment:
    return "ps_5_1";
  case ShaderStage::compute:
    return "cs_5_1";
  }
  return "";
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_shader(const std::shared_ptr<void> &nativeContext,
                       const ShaderDesc &desc) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  if (desc.format != ShaderByteFormat::dxil &&
      desc.format != ShaderByteFormat::native_source) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 accepts DXIL or native HLSL shader variants");
  }
  auto shader = std::make_shared<Direct3DShaderResource>();
  shader->context = context;
  shader->desc = desc;
  if (desc.format == ShaderByteFormat::native_source) {
    ComPtr<ID3DBlob> errors;
    const auto result =
        D3DCompile(desc.code.data(), desc.code.size(), desc.debugName.c_str(), nullptr,
                   nullptr, desc.entryPoint.c_str(), direct3d_shader_profile(desc.stage),
                   D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
                   &shader->ownedBytecode, &errors);
    if (FAILED(result)) {
      const auto message =
          errors ? std::string{static_cast<const char *>(errors->GetBufferPointer()),
                               errors->GetBufferSize()}
                 : std::string{"D3D12 HLSL compilation failed"};
      return direct3d_failure(StatusCode::invalid_argument, message, result);
    }
  } else {
    shader->bytecode = desc.code;
  }
  return std::static_pointer_cast<void>(std::move(shader));
}

[[nodiscard]] D3D12_SHADER_BYTECODE
direct3d_shader_bytecode(const Direct3DShaderResource &shader) {
  if (shader.ownedBytecode) {
    return {.pShaderBytecode = shader.ownedBytecode->GetBufferPointer(),
            .BytecodeLength = shader.ownedBytecode->GetBufferSize()};
  }
  return {.pShaderBytecode = shader.bytecode.data(),
          .BytecodeLength = shader.bytecode.size()};
}

[[nodiscard]] D3D12_DEPTH_STENCILOP_DESC
direct3d_stencil_face(const StencilFaceState &state) {
  return {
      .StencilFailOp = direct3d_stencil(state.failOp),
      .StencilDepthFailOp = direct3d_stencil(state.depthFailOp),
      .StencilPassOp = direct3d_stencil(state.passOp),
      .StencilFunc = direct3d_compare(state.compare),
  };
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_pipeline(const std::shared_ptr<void> &nativeContext,
                         const PipelineDesc &desc,
                         const detail::NativePipelineLayout &layout,
                         const std::shared_ptr<void> &vertexResource,
                         const std::shared_ptr<void> &fragmentResource) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  const auto vertex = std::static_pointer_cast<Direct3DShaderResource>(vertexResource);
  const auto fragment =
      std::static_pointer_cast<Direct3DShaderResource>(fragmentResource);
  if (!context || !context->device || !vertex || !fragment ||
      vertex->desc.stage != ShaderStage::vertex ||
      fragment->desc.stage != ShaderStage::fragment) {
    return Status::failure(StatusCode::invalid_argument,
                           "D3D12 graphics shaders are invalid");
  }
  if (!desc.specializationConstants.empty() ||
      desc.rasterization.polygonMode == PolygonMode::point ||
      desc.topology == PrimitiveTopology::patch_list ||
      has_dynamic_state(desc.dynamicState, DynamicState::depth_bias)) {
    return Status::failure(
        StatusCode::unsupported,
        "D3D12 specialization, point polygon, tessellation, or dynamic depth-bias "
        "state is unsupported");
  }
  std::array<std::shared_ptr<Direct3DShaderResource>, 2> shaders{vertex, fragment};
  auto rootResult = create_direct3d_root_layout(*context, shaders, layout, true);
  if (!rootResult.ok()) {
    return rootResult.status();
  }
  auto root = std::move(rootResult).value();

  std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
  for (std::size_t slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
    const auto &buffer = desc.vertexBuffers[slot];
    for (const auto &attribute : buffer.attributes) {
      elements.push_back({
          .SemanticName = "TEXCOORD",
          .SemanticIndex = attribute.location,
          .Format = direct3d_vertex_format(attribute.format),
          .InputSlot = static_cast<UINT>(slot),
          .AlignedByteOffset = attribute.offset,
          .InputSlotClass = buffer.stepMode == VertexStepMode::instance
                                ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
          .InstanceDataStepRate = buffer.stepMode == VertexStepMode::instance ? 1u : 0u,
      });
    }
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
  native.pRootSignature = root.signature.Get();
  native.VS = direct3d_shader_bytecode(*vertex);
  native.PS = direct3d_shader_bytecode(*fragment);
  native.BlendState.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnabled;
  native.BlendState.IndependentBlendEnable = TRUE;
  for (std::size_t index = 0; index < desc.colorTargets.size(); ++index) {
    const auto &target = desc.colorTargets[index];
    auto &output = native.BlendState.RenderTarget[index];
    output.BlendEnable = target.blend.enabled;
    output.LogicOpEnable = FALSE;
    output.SrcBlend = direct3d_blend(target.blend.color.sourceFactor);
    output.DestBlend = direct3d_blend(target.blend.color.destinationFactor);
    output.BlendOp = direct3d_blend_operation(target.blend.color.operation);
    output.SrcBlendAlpha = direct3d_blend(target.blend.alpha.sourceFactor);
    output.DestBlendAlpha = direct3d_blend(target.blend.alpha.destinationFactor);
    output.BlendOpAlpha = direct3d_blend_operation(target.blend.alpha.operation);
    output.LogicOp = D3D12_LOGIC_OP_NOOP;
    output.RenderTargetWriteMask = target.writeMask;
    native.RTVFormats[index] = direct3d_format(target.format).renderTarget;
  }
  native.SampleMask = desc.multisample.sampleMask;
  native.RasterizerState = {
      .FillMode = desc.rasterization.polygonMode == PolygonMode::line
                      ? D3D12_FILL_MODE_WIREFRAME
                      : D3D12_FILL_MODE_SOLID,
      .CullMode = desc.rasterization.cullMode == CullMode::front  ? D3D12_CULL_MODE_FRONT
                  : desc.rasterization.cullMode == CullMode::back ? D3D12_CULL_MODE_BACK
                                                                  : D3D12_CULL_MODE_NONE,
      .FrontCounterClockwise =
          desc.rasterization.frontFace == FrontFace::counter_clockwise,
      .DepthBias = static_cast<INT>(desc.rasterization.depthBias),
      .DepthBiasClamp = desc.rasterization.depthBiasClamp,
      .SlopeScaledDepthBias = desc.rasterization.depthBiasSlopeScale,
      .DepthClipEnable = !desc.rasterization.depthClampEnabled,
      .MultisampleEnable = desc.multisample.sampleCount > 1,
      .AntialiasedLineEnable = FALSE,
      .ForcedSampleCount = 0,
      .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
  };
  native.DepthStencilState = {
      .DepthEnable = desc.depthStencil.format != TextureFormat::unknown,
      .DepthWriteMask = desc.depthStencil.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL
                                                            : D3D12_DEPTH_WRITE_MASK_ZERO,
      .DepthFunc = direct3d_compare(desc.depthStencil.depthCompare),
      .StencilEnable = has_aspect(direct3d_format(desc.depthStencil.format).aspects,
                                  TextureAspect::stencil),
      .StencilReadMask = static_cast<UINT8>(desc.depthStencil.stencilReadMask),
      .StencilWriteMask = static_cast<UINT8>(desc.depthStencil.stencilWriteMask),
      .FrontFace = direct3d_stencil_face(desc.depthStencil.front),
      .BackFace = direct3d_stencil_face(desc.depthStencil.back),
  };
  native.InputLayout = {.pInputElementDescs = elements.data(),
                        .NumElements = static_cast<UINT>(elements.size())};
  native.PrimitiveTopologyType = direct3d_topology_type(desc.topology);
  native.NumRenderTargets = static_cast<UINT>(desc.colorTargets.size());
  native.DSVFormat = direct3d_format(desc.depthStencil.format).depthStencil;
  native.SampleDesc = {.Count = desc.multisample.sampleCount, .Quality = 0};
  native.NodeMask = 0;
  native.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

  auto pipeline = std::make_shared<Direct3DPipelineResource>();
  auto result = context->device->CreateGraphicsPipelineState(
      &native, IID_PPV_ARGS(&pipeline->pipeline));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 graphics pipeline creation failed", result);
  }
  pipeline->rootSignature = std::move(root.signature);
  pipeline->bindings = std::move(root.bindings);
  pipeline->pushConstantParameter = root.pushConstantParameter;
  pipeline->desc = desc;
  pipeline->desc.vertexShader = nullptr;
  pipeline->desc.fragmentShader = nullptr;
  pipeline->desc.layout = nullptr;
  pipeline->desc.cache = nullptr;
  return std::static_pointer_cast<void>(std::move(pipeline));
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_compute_pipeline(const std::shared_ptr<void> &nativeContext,
                                 const ComputePipelineDesc &desc,
                                 const detail::NativePipelineLayout &layout,
                                 const std::shared_ptr<void> &shaderResource) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  const auto shader = std::static_pointer_cast<Direct3DShaderResource>(shaderResource);
  if (!context || !context->device || !shader ||
      shader->desc.stage != ShaderStage::compute) {
    return Status::failure(StatusCode::invalid_argument,
                           "D3D12 compute shader is invalid");
  }
  if (!desc.specializationConstants.empty()) {
    return Status::failure(StatusCode::unsupported,
                           "D3D12 runtime specialization is unsupported");
  }
  std::array<std::shared_ptr<Direct3DShaderResource>, 1> shaders{shader};
  auto rootResult = create_direct3d_root_layout(*context, shaders, layout, false);
  if (!rootResult.ok()) {
    return rootResult.status();
  }
  auto root = std::move(rootResult).value();
  const D3D12_COMPUTE_PIPELINE_STATE_DESC native{
      .pRootSignature = root.signature.Get(),
      .CS = direct3d_shader_bytecode(*shader),
      .NodeMask = 0,
      .CachedPSO = {},
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
  };
  auto pipeline = std::make_shared<Direct3DComputePipelineResource>();
  const auto result = context->device->CreateComputePipelineState(
      &native, IID_PPV_ARGS(&pipeline->pipeline));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 compute pipeline creation failed", result);
  }
  pipeline->rootSignature = std::move(root.signature);
  pipeline->bindings = std::move(root.bindings);
  pipeline->pushConstantParameter = root.pushConstantParameter;
  pipeline->desc = desc;
  pipeline->desc.computeShader = nullptr;
  pipeline->desc.layout = nullptr;
  pipeline->desc.cache = nullptr;
  return std::static_pointer_cast<void>(std::move(pipeline));
}

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
                                MemoryDomain memory,
                                BufferUsage usage = BufferUsage::none) {
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
      .Flags =
          memory == MemoryDomain::device_local && has_usage(usage, BufferUsage::storage)
              ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
              : D3D12_RESOURCE_FLAG_NONE,
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
  auto native =
      create_direct3d_buffer_resource(*context, desc.size, desc.memory, desc.usage);
  if (!native.ok()) {
    return native.status();
  }
  try {
    auto resource = std::make_shared<Direct3DBufferResource>();
    resource->context = context;
    resource->resource = std::move(native).value();
    resource->size = desc.size;
    resource->usage = desc.usage;
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
  if (desc.sampleCount > 1) {
    required |= has_usage(desc.usage, TextureUsage::color_attachment)
                    ? D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET
                    : D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
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
  const auto multisampleValid =
      desc.sampleCount == 1 ||
      (desc.dimension == TextureDimension::d2 && desc.mipLevels == 1 &&
       desc.arrayLayers == 1 &&
       (has_usage(desc.usage, TextureUsage::color_attachment) ||
        has_usage(desc.usage, TextureUsage::depth_stencil_attachment)) &&
       !has_usage(desc.usage, TextureUsage::storage));
  if (format.resource == DXGI_FORMAT_UNKNOWN || format.compressed || !multisampleValid ||
      desc.memory != MemoryDomain::device_local || desc.shareable ||
      has_usage(desc.usage, TextureUsage::present) ||
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
  if (desc.sampleCount > 1) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{
        .Format = format.view,
        .SampleCount = desc.sampleCount,
        .Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
        .NumQualityLevels = 0,
    };
    if (FAILED(context->device->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) ||
        levels.NumQualityLevels == 0) {
      return Status::failure(
          StatusCode::unsupported,
          "the D3D12 adapter does not support the requested sample count");
    }
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
      .SampleDesc = {.Count = desc.sampleCount, .Quality = 0},
      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
      .Flags = direct3d_texture_flags(desc.usage),
  };
  ComPtr<ID3D12Resource> native;
  std::lock_guard contextLock{context->mutex};
  D3D12_CLEAR_VALUE clearValue{};
  const D3D12_CLEAR_VALUE *optimizedClear = nullptr;
  if (has_usage(desc.usage, TextureUsage::color_attachment)) {
    clearValue.Format = format.renderTarget;
    optimizedClear = &clearValue;
  } else if (has_usage(desc.usage, TextureUsage::depth_stencil_attachment)) {
    clearValue.Format = format.depthStencil;
    clearValue.DepthStencil = {.Depth = 1.0F, .Stencil = 0};
    optimizedClear = &clearValue;
  }
  const auto result = context->device->CreateCommittedResource(
      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
      optimizedClear, IID_PPV_ARGS(&native));
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
create_direct3d_descriptor_heap(Direct3DContext &context, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                UINT descriptorCount = 1, bool shaderVisible = false) {
  const D3D12_DESCRIPTOR_HEAP_DESC desc{
      .Type = type,
      .NumDescriptors = descriptorCount,
      .Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                             : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
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
    view->desc = desc;
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
        if (texture->desc.sampleCount > 1) {
          native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        } else if (texture->desc.arrayLayers == 1) {
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
          desc.dimension == TextureDimension::cube || texture->desc.sampleCount > 1) {
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
        if (texture->desc.sampleCount > 1) {
          native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        } else if (texture->desc.arrayLayers == 1) {
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
        if (texture->desc.sampleCount > 1) {
          native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        } else {
          native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
          native.Texture2D.MipSlice = desc.range.baseMipLevel;
        }
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
      continue;
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
    return (target & D3D12_RESOURCE_STATE_GENERIC_READ) == target
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "D3D12 upload buffers require a generic-read state");
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

void transition_direct3d_texture_range(ID3D12GraphicsCommandList &commandList,
                                       Direct3DTextureResource &resource,
                                       const TextureSubresourceRange &range,
                                       D3D12_RESOURCE_STATES target) {
  for (std::uint32_t layer = range.baseArrayLayer;
       layer < range.baseArrayLayer + range.arrayLayerCount; ++layer) {
    for (std::uint32_t mip = range.baseMipLevel;
         mip < range.baseMipLevel + range.mipLevelCount; ++mip) {
      transition_direct3d_texture(
          commandList, resource,
          {.aspect = range.aspects, .mipLevel = mip, .arrayLayer = layer}, target);
    }
  }
}

[[nodiscard]] D3D12_RESOURCE_STATES direct3d_buffer_barrier_state(Access access) {
  if (has_access(access, Access::shader_write) ||
      has_access(access, Access::memory_write)) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if (has_access(access, Access::transfer_write)) {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  }
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  if (has_access(access, Access::transfer_read)) {
    state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  if (has_access(access, Access::index_read)) {
    state |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
  }
  if (has_access(access, Access::vertex_attribute_read) ||
      has_access(access, Access::uniform_read)) {
    state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  }
  if (has_access(access, Access::indirect_read)) {
    state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  }
  if (has_access(access, Access::shader_read) ||
      has_access(access, Access::memory_read)) {
    state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  }
  return state;
}

[[nodiscard]] D3D12_RESOURCE_STATES
direct3d_texture_barrier_state(const detail::NativeTextureBarrier &barrier) {
  switch (barrier.newLayout) {
  case TextureLayout::general:
    if (has_access(barrier.destinationAccess, Access::shader_write)) {
      return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    return has_access(barrier.destinationAccess, Access::shader_read)
               ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
               : D3D12_RESOURCE_STATE_COMMON;
  case TextureLayout::color_attachment:
    return D3D12_RESOURCE_STATE_RENDER_TARGET;
  case TextureLayout::depth_stencil_attachment:
    return has_access(barrier.destinationAccess, Access::depth_stencil_write)
               ? D3D12_RESOURCE_STATE_DEPTH_WRITE
               : D3D12_RESOURCE_STATE_DEPTH_READ;
  case TextureLayout::depth_stencil_read_only:
    return D3D12_RESOURCE_STATE_DEPTH_READ;
  case TextureLayout::shader_read_only:
    return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  case TextureLayout::transfer_source:
    return D3D12_RESOURCE_STATE_COPY_SOURCE;
  case TextureLayout::transfer_destination:
    return D3D12_RESOURCE_STATE_COPY_DEST;
  case TextureLayout::present:
    return D3D12_RESOURCE_STATE_PRESENT;
  case TextureLayout::undefined:
    break;
  }
  return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] ID3D12Resource *
direct3d_barrier_resource(const std::shared_ptr<void> &buffer,
                          const std::shared_ptr<void> &texture) {
  if (buffer) {
    const auto native = std::static_pointer_cast<Direct3DBufferResource>(buffer);
    return native ? native->resource.Get() : nullptr;
  }
  const auto native = std::static_pointer_cast<Direct3DTextureResource>(texture);
  return native ? native->resource.Get() : nullptr;
}

[[nodiscard]] Status record_direct3d_barriers(ID3D12GraphicsCommandList &commandList,
                                              const detail::NativeCommand &command) {
  for (const auto &barrier : command.bufferBarriers) {
    const auto buffer = std::static_pointer_cast<Direct3DBufferResource>(barrier.buffer);
    if (!buffer) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 buffer barrier resource is invalid");
    }
    if (barrier.transferOwnership) {
      return Status::failure(
          StatusCode::unsupported,
          "D3D12 queue ownership transfer requires a native multi-queue backend");
    }
    if (auto status = transition_direct3d_buffer(
            commandList, *buffer,
            direct3d_buffer_barrier_state(barrier.destinationAccess));
        !status.ok()) {
      return status;
    }
    if (has_access(barrier.sourceAccess, Access::shader_write) &&
        has_access(barrier.destinationAccess, Access::shader_write)) {
      D3D12_RESOURCE_BARRIER native{};
      native.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      native.UAV.pResource = buffer->resource.Get();
      commandList.ResourceBarrier(1, &native);
    }
  }
  for (const auto &barrier : command.textureBarriers) {
    const auto texture =
        std::static_pointer_cast<Direct3DTextureResource>(barrier.texture);
    if (!texture) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 texture barrier resource is invalid");
    }
    if (barrier.transferOwnership) {
      return Status::failure(
          StatusCode::unsupported,
          "D3D12 queue ownership transfer requires a native multi-queue backend");
    }
    transition_direct3d_texture_range(commandList, *texture, barrier.range,
                                      direct3d_texture_barrier_state(barrier));
    if (has_access(barrier.sourceAccess, Access::shader_write) &&
        has_access(barrier.destinationAccess, Access::shader_write)) {
      D3D12_RESOURCE_BARRIER native{};
      native.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      native.UAV.pResource = texture->resource.Get();
      commandList.ResourceBarrier(1, &native);
    }
  }
  for (const auto &barrier : command.aliasingBarriers) {
    auto *before = direct3d_barrier_resource(barrier.beforeBuffer, barrier.beforeTexture);
    auto *after = direct3d_barrier_resource(barrier.afterBuffer, barrier.afterTexture);
    if (before == nullptr || after == nullptr || before == after) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 aliasing barrier resources are invalid");
    }
    D3D12_RESOURCE_BARRIER native{};
    native.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    native.Aliasing.pResourceBefore = before;
    native.Aliasing.pResourceAfter = after;
    commandList.ResourceBarrier(1, &native);
  }
  return Status::success();
}

struct Direct3DSubmissionDescriptors {
  ComPtr<ID3D12DescriptorHeap> resources;
  ComPtr<ID3D12DescriptorHeap> samplers;
  UINT resourceIncrement = 0;
  UINT samplerIncrement = 0;
  UINT nextResource = 0;
  UINT nextSampler = 0;
};

[[nodiscard]] Result<Direct3DSubmissionDescriptors>
create_direct3d_submission_descriptors(Direct3DContext &context,
                                       std::span<const detail::NativeCommand> commands) {
  UINT resourceCount = 0;
  UINT samplerCount = 0;
  for (const auto &command : commands) {
    if (command.kind != detail::NativeCommandKind::bind_group) {
      continue;
    }
    for (const auto &binding : command.bindings) {
      binding.type == BindingType::sampler ? ++samplerCount : ++resourceCount;
    }
  }
  Direct3DSubmissionDescriptors output;
  if (resourceCount != 0) {
    auto heap = create_direct3d_descriptor_heap(
        context, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resourceCount, true);
    if (!heap.ok()) {
      return heap.status();
    }
    output.resources = std::move(heap).value();
    output.resourceIncrement = context.device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
  if (samplerCount != 0) {
    auto heap = create_direct3d_descriptor_heap(
        context, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, samplerCount, true);
    if (!heap.ok()) {
      return heap.status();
    }
    output.samplers = std::move(heap).value();
    output.samplerIncrement = context.device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
  return output;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE
direct3d_cpu_handle(ID3D12DescriptorHeap &heap, UINT index, UINT increment) {
  auto handle = heap.GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) * increment;
  return handle;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE
direct3d_gpu_handle(ID3D12DescriptorHeap &heap, UINT index, UINT increment) {
  auto handle = heap.GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(index) * increment;
  return handle;
}

[[nodiscard]] Status encode_direct3d_bind_group(
    Direct3DContext &context, ID3D12GraphicsCommandList &commandList,
    const detail::NativeCommand &command, std::span<const Direct3DBindingSlot> slots,
    Direct3DSubmissionDescriptors &descriptors, bool graphics) {
  for (const auto &slot : slots) {
    if (slot.group != command.arguments[0]) {
      continue;
    }
    const auto first = std::find_if(
        command.bindings.begin(), command.bindings.end(), [&](const auto &binding) {
          return binding.group == slot.group && binding.binding == slot.binding;
        });
    if (first == command.bindings.end()) {
      continue;
    }
    const auto sampler = slot.type == BindingType::sampler;
    auto *heap = sampler ? descriptors.samplers.Get() : descriptors.resources.Get();
    auto &next = sampler ? descriptors.nextSampler : descriptors.nextResource;
    const auto increment =
        sampler ? descriptors.samplerIncrement : descriptors.resourceIncrement;
    if (heap == nullptr) {
      return Status::failure(StatusCode::invalid_state,
                             "D3D12 descriptor heap is unavailable");
    }
    const auto tableStart = next;
    for (std::uint32_t element = 0; element < slot.descriptorCount; ++element) {
      const auto found = std::find_if(
          command.bindings.begin(), command.bindings.end(), [&](const auto &binding) {
            return binding.group == slot.group && binding.binding == slot.binding &&
                   binding.arrayElement == element && binding.type == slot.type;
          });
      if (found == command.bindings.end()) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 bind group has an incomplete descriptor array");
      }
      const auto cpu = direct3d_cpu_handle(*heap, next++, increment);
      switch (slot.type) {
      case BindingType::uniform_buffer: {
        const auto resource =
            std::static_pointer_cast<Direct3DBufferResource>(found->resource);
        if (!resource || found->offset % 256 != 0) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 constant-buffer binding is invalid");
        }
        const auto size = static_cast<UINT>((found->size + 255u) & ~255u);
        const D3D12_CONSTANT_BUFFER_VIEW_DESC view{
            .BufferLocation = resource->resource->GetGPUVirtualAddress() + found->offset,
            .SizeInBytes = size,
        };
        context.device->CreateConstantBufferView(&view, cpu);
        if (auto status = transition_direct3d_buffer(
                commandList, *resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            !status.ok()) {
          return status;
        }
        break;
      }
      case BindingType::storage_buffer: {
        const auto resource =
            std::static_pointer_cast<Direct3DBufferResource>(found->resource);
        if (!resource || found->offset % 4 != 0 || found->size % 4 != 0) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 storage-buffer binding is invalid");
        }
        if (slot.readOnly) {
          D3D12_SHADER_RESOURCE_VIEW_DESC view{};
          view.Format = DXGI_FORMAT_R32_TYPELESS;
          view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
          view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
          view.Buffer.FirstElement = found->offset / 4;
          view.Buffer.NumElements = static_cast<UINT>(found->size / 4);
          view.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
          context.device->CreateShaderResourceView(resource->resource.Get(), &view, cpu);
          if (auto status = transition_direct3d_buffer(
                  commandList, *resource,
                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
              !status.ok()) {
            return status;
          }
        } else {
          D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
          view.Format = DXGI_FORMAT_R32_TYPELESS;
          view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
          view.Buffer.FirstElement = found->offset / 4;
          view.Buffer.NumElements = static_cast<UINT>(found->size / 4);
          view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
          context.device->CreateUnorderedAccessView(resource->resource.Get(), nullptr,
                                                    &view, cpu);
          if (auto status = transition_direct3d_buffer(
                  commandList, *resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
              !status.ok()) {
            return status;
          }
        }
        break;
      }
      case BindingType::sampled_texture:
      case BindingType::storage_texture: {
        const auto view =
            std::static_pointer_cast<Direct3DTextureViewResource>(found->resource);
        if (!view) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 texture binding is invalid");
        }
        const auto source = slot.type == BindingType::sampled_texture
                                ? view->shaderResource
                                : view->unorderedAccess;
        if (source.ptr == 0) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 texture descriptor is unavailable");
        }
        context.device->CopyDescriptorsSimple(1, cpu, source,
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        transition_direct3d_texture_range(
            commandList, *view->texture, view->desc.range,
            slot.type == BindingType::sampled_texture
                ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        break;
      }
      case BindingType::sampler: {
        const auto resource =
            std::static_pointer_cast<Direct3DSamplerResource>(found->resource);
        if (!resource) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 sampler binding is invalid");
        }
        context.device->CreateSampler(&resource->desc, cpu);
        break;
      }
      }
    }
    const auto gpu = direct3d_gpu_handle(*heap, tableStart, increment);
    if (graphics) {
      commandList.SetGraphicsRootDescriptorTable(slot.rootParameter, gpu);
    } else {
      commandList.SetComputeRootDescriptorTable(slot.rootParameter, gpu);
    }
  }
  return Status::success();
}

[[nodiscard]] Status
record_direct3d_commands(Direct3DContext &context, ID3D12GraphicsCommandList &commandList,
                         std::span<const detail::NativeCommand> commands,
                         std::vector<ComPtr<ID3D12Resource>> &transients,
                         std::vector<ComPtr<ID3D12DescriptorHeap>> &descriptorHeaps,
                         std::vector<ComPtr<ID3D12CommandSignature>> &signatures) {
  auto descriptorResult = create_direct3d_submission_descriptors(context, commands);
  if (!descriptorResult.ok()) {
    return descriptorResult.status();
  }
  auto descriptors = std::move(descriptorResult).value();
  std::array<ID3D12DescriptorHeap *, 2> shaderHeaps{};
  UINT shaderHeapCount = 0;
  if (descriptors.resources) {
    shaderHeaps[shaderHeapCount++] = descriptors.resources.Get();
    descriptorHeaps.push_back(descriptors.resources);
  }
  if (descriptors.samplers) {
    shaderHeaps[shaderHeapCount++] = descriptors.samplers.Get();
    descriptorHeaps.push_back(descriptors.samplers);
  }
  if (shaderHeapCount != 0) {
    commandList.SetDescriptorHeaps(shaderHeapCount, shaderHeaps.data());
  }
  std::shared_ptr<Direct3DPipelineResource> graphicsPipeline;
  std::shared_ptr<Direct3DComputePipelineResource> computePipeline;
  std::shared_ptr<Direct3DBufferResource> indexBuffer;
  std::size_t indexBufferOffset = 0;
  DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
  Extent2D renderExtent{};
  std::vector<detail::NativeRenderAttachment> renderAttachments;
  detail::NativeDepthStencilAttachment depthAttachment;
  for (const auto &command : commands) {
    if (command.kind == detail::NativeCommandKind::barrier) {
      if (auto status = record_direct3d_barriers(commandList, command); !status.ok()) {
        return status;
      }
      continue;
    }
    if (command.kind != detail::NativeCommandKind::transfer) {
      switch (command.kind) {
      case detail::NativeCommandKind::begin_render: {
        renderExtent = command.extent;
        renderAttachments = command.colorAttachments;
        depthAttachment = command.depthStencilAttachment;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets;
        if (!command.colorAttachments.empty()) {
          auto heap = create_direct3d_descriptor_heap(
              context, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
              static_cast<UINT>(command.colorAttachments.size()));
          if (!heap.ok()) {
            return heap.status();
          }
          const auto increment = context.device->GetDescriptorHandleIncrementSize(
              D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
          for (std::size_t index = 0; index < command.colorAttachments.size(); ++index) {
            const auto texture = std::static_pointer_cast<Direct3DTextureResource>(
                command.colorAttachments[index].texture);
            if (!texture || texture->format.renderTarget == DXGI_FORMAT_UNKNOWN) {
              return Status::failure(StatusCode::invalid_argument,
                                     "D3D12 render attachment is invalid");
            }
            transition_direct3d_texture(commandList, *texture, {},
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
            const auto handle = direct3d_cpu_handle(*heap.value().Get(),
                                                    static_cast<UINT>(index), increment);
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format = texture->format.renderTarget;
            view.ViewDimension = texture->desc.sampleCount > 1
                                     ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                                     : D3D12_RTV_DIMENSION_TEXTURE2D;
            context.device->CreateRenderTargetView(texture->resource.Get(), &view,
                                                   handle);
            renderTargets.push_back(handle);
            const auto &attachment = command.colorAttachments[index];
            if (attachment.loadOp == LoadOp::clear) {
              const float clear[4]{attachment.clear.r, attachment.clear.g,
                                   attachment.clear.b, attachment.clear.a};
              commandList.ClearRenderTargetView(handle, clear, 0, nullptr);
            } else if (attachment.loadOp == LoadOp::dont_care) {
              commandList.DiscardResource(texture->resource.Get(), nullptr);
            }
          }
          descriptorHeaps.push_back(std::move(heap).value());
        }
        D3D12_CPU_DESCRIPTOR_HANDLE depthHandle{};
        if (command.depthStencilAttachment.texture) {
          const auto texture = std::static_pointer_cast<Direct3DTextureResource>(
              command.depthStencilAttachment.texture);
          if (!texture || texture->format.depthStencil == DXGI_FORMAT_UNKNOWN) {
            return Status::failure(StatusCode::invalid_argument,
                                   "D3D12 depth attachment is invalid");
          }
          transition_direct3d_texture(commandList, *texture, {},
                                      D3D12_RESOURCE_STATE_DEPTH_WRITE);
          auto heap =
              create_direct3d_descriptor_heap(context, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
          if (!heap.ok()) {
            return heap.status();
          }
          depthHandle = heap.value()->GetCPUDescriptorHandleForHeapStart();
          D3D12_DEPTH_STENCIL_VIEW_DESC view{};
          view.Format = texture->format.depthStencil;
          view.ViewDimension = texture->desc.sampleCount > 1
                                   ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                   : D3D12_DSV_DIMENSION_TEXTURE2D;
          context.device->CreateDepthStencilView(texture->resource.Get(), &view,
                                                 depthHandle);
          D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
          if (command.depthStencilAttachment.depthLoadOp == LoadOp::clear) {
            flags |= D3D12_CLEAR_FLAG_DEPTH;
          }
          if (has_aspect(texture->format.aspects, TextureAspect::stencil) &&
              command.depthStencilAttachment.stencilLoadOp == LoadOp::clear) {
            flags |= D3D12_CLEAR_FLAG_STENCIL;
          }
          if (flags != 0) {
            commandList.ClearDepthStencilView(
                depthHandle, flags, command.depthStencilAttachment.clearDepth,
                static_cast<UINT8>(command.depthStencilAttachment.clearStencil), 0,
                nullptr);
          }
          descriptorHeaps.push_back(std::move(heap).value());
        }
        commandList.OMSetRenderTargets(static_cast<UINT>(renderTargets.size()),
                                       renderTargets.data(), FALSE,
                                       depthHandle.ptr == 0 ? nullptr : &depthHandle);
        graphicsPipeline.reset();
        indexBuffer.reset();
        break;
      }
      case detail::NativeCommandKind::end_render:
        for (const auto &attachment : renderAttachments) {
          const auto source =
              std::static_pointer_cast<Direct3DTextureResource>(attachment.texture);
          if (attachment.resolveTexture) {
            const auto destination = std::static_pointer_cast<Direct3DTextureResource>(
                attachment.resolveTexture);
            transition_direct3d_texture(commandList, *source, {},
                                        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            transition_direct3d_texture(commandList, *destination, {},
                                        D3D12_RESOURCE_STATE_RESOLVE_DEST);
            commandList.ResolveSubresource(destination->resource.Get(), 0,
                                           source->resource.Get(), 0,
                                           source->format.view);
          } else if (attachment.storeOp == StoreOp::dont_care) {
            commandList.DiscardResource(source->resource.Get(), nullptr);
          }
        }
        graphicsPipeline.reset();
        renderAttachments.clear();
        break;
      case detail::NativeCommandKind::begin_compute:
        computePipeline.reset();
        break;
      case detail::NativeCommandKind::end_compute:
        computePipeline.reset();
        break;
      case detail::NativeCommandKind::bind_graphics_pipeline: {
        graphicsPipeline =
            std::static_pointer_cast<Direct3DPipelineResource>(command.object);
        if (!graphicsPipeline || !graphicsPipeline->pipeline ||
            !graphicsPipeline->rootSignature) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 graphics pipeline binding is invalid");
        }
        commandList.SetGraphicsRootSignature(graphicsPipeline->rootSignature.Get());
        commandList.SetPipelineState(graphicsPipeline->pipeline.Get());
        commandList.IASetPrimitiveTopology(
            direct3d_topology(graphicsPipeline->desc.topology));
        if (graphicsPipeline->desc.viewports.empty() &&
            !has_dynamic_state(graphicsPipeline->desc.dynamicState,
                               DynamicState::viewport)) {
          const D3D12_VIEWPORT viewport{0.0F,
                                        0.0F,
                                        static_cast<float>(renderExtent.width),
                                        static_cast<float>(renderExtent.height),
                                        0.0F,
                                        1.0F};
          commandList.RSSetViewports(1, &viewport);
        }
        if (graphicsPipeline->desc.scissors.empty() &&
            !has_dynamic_state(graphicsPipeline->desc.dynamicState,
                               DynamicState::scissor)) {
          const D3D12_RECT scissor{0, 0, static_cast<LONG>(renderExtent.width),
                                   static_cast<LONG>(renderExtent.height)};
          commandList.RSSetScissorRects(1, &scissor);
        }
        if (!graphicsPipeline->desc.viewports.empty()) {
          std::vector<D3D12_VIEWPORT> viewports;
          for (const auto &viewport : graphicsPipeline->desc.viewports) {
            viewports.push_back({viewport.x, viewport.y, viewport.width, viewport.height,
                                 viewport.minimumDepth, viewport.maximumDepth});
          }
          commandList.RSSetViewports(static_cast<UINT>(viewports.size()),
                                     viewports.data());
        }
        if (!graphicsPipeline->desc.scissors.empty()) {
          std::vector<D3D12_RECT> scissors;
          for (const auto &scissor : graphicsPipeline->desc.scissors) {
            scissors.push_back({scissor.x, scissor.y,
                                scissor.x + static_cast<LONG>(scissor.width),
                                scissor.y + static_cast<LONG>(scissor.height)});
          }
          commandList.RSSetScissorRects(static_cast<UINT>(scissors.size()),
                                        scissors.data());
        }
        commandList.OMSetBlendFactor(graphicsPipeline->desc.blendConstant.data());
        commandList.OMSetStencilRef(graphicsPipeline->desc.stencilReference);
        break;
      }
      case detail::NativeCommandKind::bind_compute_pipeline:
        computePipeline =
            std::static_pointer_cast<Direct3DComputePipelineResource>(command.object);
        if (!computePipeline || !computePipeline->pipeline ||
            !computePipeline->rootSignature) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 compute pipeline binding is invalid");
        }
        commandList.SetComputeRootSignature(computePipeline->rootSignature.Get());
        commandList.SetPipelineState(computePipeline->pipeline.Get());
        break;
      case detail::NativeCommandKind::bind_vertex_buffer: {
        const auto buffer =
            std::static_pointer_cast<Direct3DBufferResource>(command.object);
        if (!buffer || !graphicsPipeline ||
            command.arguments[0] >= graphicsPipeline->desc.vertexBuffers.size()) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 vertex-buffer binding is invalid");
        }
        if (auto status = transition_direct3d_buffer(
                commandList, *buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            !status.ok()) {
          return status;
        }
        const auto offset = static_cast<std::size_t>(command.arguments[1]);
        const D3D12_VERTEX_BUFFER_VIEW view{
            .BufferLocation = buffer->resource->GetGPUVirtualAddress() + offset,
            .SizeInBytes = static_cast<UINT>(buffer->size - offset),
            .StrideInBytes =
                graphicsPipeline->desc.vertexBuffers[command.arguments[0]].stride,
        };
        commandList.IASetVertexBuffers(static_cast<UINT>(command.arguments[0]), 1, &view);
        break;
      }
      case detail::NativeCommandKind::bind_index_buffer: {
        indexBuffer = std::static_pointer_cast<Direct3DBufferResource>(command.object);
        if (!indexBuffer) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 index-buffer binding is invalid");
        }
        if (auto status = transition_direct3d_buffer(commandList, *indexBuffer,
                                                     D3D12_RESOURCE_STATE_INDEX_BUFFER);
            !status.ok()) {
          return status;
        }
        indexBufferOffset = command.arguments[0];
        indexFormat =
            command.arguments[1] == static_cast<std::uint64_t>(IndexFormat::uint16)
                ? DXGI_FORMAT_R16_UINT
                : DXGI_FORMAT_R32_UINT;
        const D3D12_INDEX_BUFFER_VIEW view{
            .BufferLocation =
                indexBuffer->resource->GetGPUVirtualAddress() + indexBufferOffset,
            .SizeInBytes = static_cast<UINT>(indexBuffer->size - indexBufferOffset),
            .Format = indexFormat,
        };
        commandList.IASetIndexBuffer(&view);
        break;
      }
      case detail::NativeCommandKind::bind_uniform_buffer:
      case detail::NativeCommandKind::bind_storage_buffer:
        return Status::failure(
            StatusCode::unsupported,
            "D3D12 direct buffer slots are unsupported; use reflected bind groups");
      case detail::NativeCommandKind::bind_group:
        if (graphicsPipeline) {
          if (auto status = encode_direct3d_bind_group(context, commandList, command,
                                                       graphicsPipeline->bindings,
                                                       descriptors, true);
              !status.ok()) {
            return status;
          }
        } else if (computePipeline) {
          if (auto status = encode_direct3d_bind_group(context, commandList, command,
                                                       computePipeline->bindings,
                                                       descriptors, false);
              !status.ok()) {
            return status;
          }
        } else {
          return Status::failure(StatusCode::invalid_state,
                                 "D3D12 bind group requires a pipeline");
        }
        break;
      case detail::NativeCommandKind::push_constants: {
        const auto values = static_cast<UINT>(command.bytes.size() / 4);
        const auto offset = static_cast<UINT>(command.arguments[1] / 4);
        if (graphicsPipeline && graphicsPipeline->pushConstantParameter != UINT_MAX) {
          commandList.SetGraphicsRoot32BitConstants(
              graphicsPipeline->pushConstantParameter, values, command.bytes.data(),
              offset);
        } else if (computePipeline &&
                   computePipeline->pushConstantParameter != UINT_MAX) {
          commandList.SetComputeRoot32BitConstants(computePipeline->pushConstantParameter,
                                                   values, command.bytes.data(), offset);
        } else {
          return Status::failure(StatusCode::invalid_state,
                                 "D3D12 pipeline has no push-constant range");
        }
        break;
      }
      case detail::NativeCommandKind::set_viewports: {
        std::vector<D3D12_VIEWPORT> viewports;
        for (const auto &viewport : command.viewports) {
          viewports.push_back({viewport.x, viewport.y, viewport.width, viewport.height,
                               viewport.minimumDepth, viewport.maximumDepth});
        }
        commandList.RSSetViewports(static_cast<UINT>(viewports.size()), viewports.data());
        break;
      }
      case detail::NativeCommandKind::set_scissors: {
        std::vector<D3D12_RECT> scissors;
        for (const auto &scissor : command.scissors) {
          scissors.push_back({scissor.x, scissor.y,
                              scissor.x + static_cast<LONG>(scissor.width),
                              scissor.y + static_cast<LONG>(scissor.height)});
        }
        commandList.RSSetScissorRects(static_cast<UINT>(scissors.size()),
                                      scissors.data());
        break;
      }
      case detail::NativeCommandKind::set_blend_constant:
        commandList.OMSetBlendFactor(
            reinterpret_cast<const float *>(command.bytes.data()));
        break;
      case detail::NativeCommandKind::set_stencil_reference:
        commandList.OMSetStencilRef(static_cast<UINT>(command.arguments[0]));
        break;
      case detail::NativeCommandKind::set_depth_bias:
        return Status::failure(StatusCode::unsupported,
                               "D3D12 dynamic depth bias is unsupported");
      case detail::NativeCommandKind::draw:
        commandList.DrawInstanced(static_cast<UINT>(command.arguments[0]),
                                  static_cast<UINT>(command.arguments[1]),
                                  static_cast<UINT>(command.arguments[2]),
                                  static_cast<UINT>(command.arguments[3]));
        break;
      case detail::NativeCommandKind::draw_indexed:
        commandList.DrawIndexedInstanced(static_cast<UINT>(command.arguments[0]),
                                         static_cast<UINT>(command.arguments[1]),
                                         static_cast<UINT>(command.arguments[2]),
                                         static_cast<INT>(command.arguments[3]),
                                         static_cast<UINT>(command.arguments[4]));
        break;
      case detail::NativeCommandKind::draw_indirect:
      case detail::NativeCommandKind::dispatch_indirect: {
        const auto buffer =
            std::static_pointer_cast<Direct3DBufferResource>(command.object);
        if (!buffer) {
          return Status::failure(StatusCode::invalid_argument,
                                 "D3D12 indirect buffer is invalid");
        }
        if (auto status = transition_direct3d_buffer(
                commandList, *buffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            !status.ok()) {
          return status;
        }
        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        UINT count = 1;
        UINT stride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        if (command.kind == detail::NativeCommandKind::dispatch_indirect) {
          argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        } else if (command.arguments[1] != 0) {
          argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
          count = static_cast<UINT>(command.arguments[2]);
          stride = static_cast<UINT>(command.arguments[3]);
        } else {
          argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
          count = static_cast<UINT>(command.arguments[2]);
          stride = static_cast<UINT>(command.arguments[3]);
        }
        const D3D12_COMMAND_SIGNATURE_DESC signatureDesc{
            .ByteStride = stride,
            .NumArgumentDescs = 1,
            .pArgumentDescs = &argument,
            .NodeMask = 0,
        };
        ComPtr<ID3D12CommandSignature> signature;
        const auto result = context.device->CreateCommandSignature(
            &signatureDesc, nullptr, IID_PPV_ARGS(&signature));
        if (FAILED(result)) {
          return direct3d_failure(StatusCode::backend_error,
                                  "D3D12 command signature creation failed", result);
        }
        const auto offset = command.arguments[0];
        commandList.ExecuteIndirect(signature.Get(), count, buffer->resource.Get(),
                                    offset, nullptr, 0);
        signatures.push_back(std::move(signature));
        break;
      }
      case detail::NativeCommandKind::draw_indirect_count:
        return Status::failure(StatusCode::unsupported,
                               "D3D12 indirect-count drawing is capability-gated");
      case detail::NativeCommandKind::dispatch:
        commandList.Dispatch(static_cast<UINT>(command.arguments[0]),
                             static_cast<UINT>(command.arguments[1]),
                             static_cast<UINT>(command.arguments[2]));
        break;
      case detail::NativeCommandKind::transfer:
      case detail::NativeCommandKind::barrier:
        break;
      }
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
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device || !context->queue || !context->fence) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  std::lock_guard lock{context->mutex};
  if (auto status = validate_direct3d_commands(commands); !status.ok()) {
    return status;
  }
  for (const auto &wait : waits) {
    const auto semaphore =
        std::static_pointer_cast<Direct3DSemaphoreResource>(wait.semaphore);
    if (!semaphore || !semaphore->fence) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 wait semaphore is invalid");
    }
    const auto result = context->queue->Wait(semaphore->fence.Get(), wait.value);
    if (FAILED(result)) {
      return direct3d_failure(StatusCode::backend_error,
                              "D3D12 queue semaphore wait failed", result);
    }
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
  std::vector<ComPtr<ID3D12DescriptorHeap>> descriptorHeaps;
  std::vector<ComPtr<ID3D12CommandSignature>> signatures;
  if (auto status = record_direct3d_commands(*context, *commandList.Get(), commands,
                                             transients, descriptorHeaps, signatures);
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
  for (const auto &signal : signals) {
    const auto semaphore =
        std::static_pointer_cast<Direct3DSemaphoreResource>(signal.semaphore);
    if (!semaphore || !semaphore->fence) {
      return Status::failure(StatusCode::invalid_argument,
                             "D3D12 signal semaphore is invalid");
    }
    result = context->queue->Signal(semaphore->fence.Get(), signal.value);
    if (FAILED(result)) {
      return direct3d_failure(StatusCode::backend_error,
                              "D3D12 queue semaphore signal failed", result);
    }
  }
  return submit_empty(*context);
}

[[nodiscard]] Result<std::shared_ptr<void>>
create_direct3d_semaphore(const std::shared_ptr<void> &nativeContext,
                          const SemaphoreDesc &desc) {
  const auto context = std::static_pointer_cast<Direct3DContext>(nativeContext);
  if (!context || !context->device) {
    return Status::failure(StatusCode::device_lost,
                           "the D3D12 native context is unavailable");
  }
  auto semaphore = std::make_shared<Direct3DSemaphoreResource>();
  const auto result = context->device->CreateFence(
      desc.initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&semaphore->fence));
  if (FAILED(result)) {
    return direct3d_failure(StatusCode::backend_error,
                            "D3D12 timeline semaphore creation failed", result);
  }
  return std::static_pointer_cast<void>(std::move(semaphore));
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
  config.supportedFeatures = {Feature::compute,         Feature::transfer,
                              Feature::memory_budget,   Feature::descriptor_arrays,
                              Feature::dynamic_offsets, Feature::push_constants};
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
  config.bindingCapabilities = {
      .ordinaryBindGroups = true,
      .descriptorArrays = true,
      .dynamicOffsets = true,
      .immutableSamplers = true,
      .pushConstants = true,
      .bindlessTables = false,
      .updateAfterBind = false,
      .maxBindGroups = 2,
      .maxBindingsPerGroup = 8,
      .maxDescriptorsPerGroup = 8,
      .maxPushConstantBytes = 128,
      .minUniformBufferOffsetAlignment = 256,
      .minStorageBufferOffsetAlignment = 16,
  };
  config.pipelineCapabilities = {
      .graphics = true,
      .compute = true,
      .multipleRenderTargets = true,
      .depthStencil = true,
      .multisample = true,
      .tessellation = false,
      .indirect = true,
      .indirectCount = false,
      .pipelineCache = false,
      .maxColorAttachments = 8,
      .maxVertexBuffers = 16,
      .maxViewports = 16,
      .maxComputeWorkgroupSize = {1024, 1024, 64},
      .maxComputeInvocations = 1024,
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
  config.createSampler = &create_direct3d_sampler;
  config.createShader = &create_direct3d_shader;
  config.createPipeline = &create_direct3d_pipeline;
  config.createComputePipeline = &create_direct3d_compute_pipeline;
  config.createSemaphore = &create_direct3d_semaphore;
  config.nativeSubmit = &submit_direct3d_commands;
  return detail::create_foundation_instance(desc, std::move(config));
#else
  (void)desc;
  return detail::unavailable_backend(BackendKind::direct3d12, "Direct3D 12");
#endif
}

} // namespace truffle::rhi
