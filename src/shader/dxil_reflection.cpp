#include "dxil_reflection.hpp"

#include <dxc/DxilContainer/DxilContainer.h>
#include <dxc/DxilContainer/DxilPipelineStateValidation.h>
#include <dxc/Support/D3DReflection.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>

namespace truffle::shader::detail {
namespace {
using namespace dxc;

rhi::Status unsupported(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::unsupported, "DXIL reflection: " + std::move(message));
}

rhi::Status interfaces(const DxilPipelineStateValidation& psv, rhi::ShaderStage stage,
                       bool input, rhi::ShaderReflection& reflection) {
    const auto count = input ? psv.GetSigInputElements() : psv.GetSigOutputElements();
    for (std::uint32_t i = 0; i < count; ++i) {
        auto* raw = input ? psv.GetInputElement0(i) : psv.GetOutputElement0(i);
        if (!raw) return invalid("Missing DXIL signature metadata");
        const auto element = psv.GetSignatureElement(raw);
        if (element.GetSemanticKind() != PSVSemanticKind::Arbitrary &&
            !(stage == rhi::ShaderStage::fragment && !input &&
              element.GetSemanticKind() == PSVSemanticKind::Target)) continue;
        const auto* rawName = element.GetSemanticName();
        const auto* indexes = element.GetSemanticIndexes();
        if (!rawName || !indexes) return invalid("Invalid DXIL semantic metadata");
        std::string name{rawName};
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (element.GetSemanticKind() == PSVSemanticKind::Arbitrary && upper != "TEXCOORD")
            return unsupported("Canonical DXIL user IO requires TEXCOORDn semantics: " + name);
        const auto width = element.GetCols();
        const auto type = static_cast<hlsl::DxilProgramSigCompType>(element.GetComponentType());
        const bool floating = type == hlsl::DxilProgramSigCompType::Float32;
        const bool signedInt = type == hlsl::DxilProgramSigCompType::SInt32;
        if ((!floating && !signedInt && type != hlsl::DxilProgramSigCompType::UInt32) ||
            width == 0 || width > 4)
            return unsupported("IO requires 32-bit scalar/vector leaves");
        const auto spelling = width == 1 ? std::string{floating ? "float" : signedInt ? "int" : "uint"}
            : std::string{floating ? "vec" : signedInt ? "ivec" : "uvec"} + std::to_string(width);
        std::string interpolation;
        switch (static_cast<hlsl::DXIL::InterpolationMode>(element.GetInterpolationMode())) {
        case hlsl::DXIL::InterpolationMode::Undefined:
        case hlsl::DXIL::InterpolationMode::Linear: interpolation = "smooth"; break;
        case hlsl::DXIL::InterpolationMode::Constant: interpolation = "flat"; break;
        case hlsl::DXIL::InterpolationMode::LinearCentroid: interpolation = "centroid smooth"; break;
        case hlsl::DXIL::InterpolationMode::LinearNoperspective: interpolation = "noperspective"; break;
        case hlsl::DXIL::InterpolationMode::LinearNoperspectiveCentroid:
            interpolation = "centroid noperspective"; break;
        case hlsl::DXIL::InterpolationMode::LinearSample: interpolation = "sample smooth"; break;
        case hlsl::DXIL::InterpolationMode::LinearNoperspectiveSample:
            interpolation = "sample noperspective"; break;
        default: return unsupported("Unknown interpolation mode");
        }
        for (std::uint32_t row = 0; row < element.GetRows(); ++row) {
            if (reflection.interfaces.size() >= 4096)
                return unsupported("Too many interface leaves");
            reflection.interfaces.push_back({
                stage, input ? rhi::ShaderInterfaceDirection::input : rhi::ShaderInterfaceDirection::output,
                indexes[row], name + std::to_string(indexes[row]), spelling, interpolation});
        }
    }
    return rhi::Status::success();
}
} // namespace

rhi::Status validate_dxil(IDxcBlob& code) {
    auto created = instance<IDxcValidator>(CLSID_DxcValidator);
    if (!created.ok()) return created.status();
    const auto validator = std::move(created).value();
    IDxcOperationResult* raw = nullptr;
    const auto invoked = validator->Validate(&code, DxcValidatorFlags_Default, &raw);
    ComPtr<IDxcOperationResult> result{raw};
    if (FAILED(invoked) || !result) return invalid("DXIL validator invocation failed");
    HRESULT status = E_FAIL;
    if (FAILED(result->GetStatus(&status))) return invalid("DXIL validator status unavailable");
    if (FAILED(status)) {
        IDxcBlobEncoding* rawErrors = nullptr;
        if (FAILED(result->GetErrorBuffer(&rawErrors)) || !rawErrors)
            return invalid("DXIL validation failed; diagnostics unavailable");
        ComPtr<IDxcBlobEncoding> errors{rawErrors};
        auto createdUtils = instance<IDxcUtils>(CLSID_DxcUtils);
        if (!createdUtils.ok()) return createdUtils.status();
        IDxcBlobUtf8* rawText = nullptr;
        if (FAILED(createdUtils.value()->GetBlobAsUtf8(errors.get(), &rawText)) || !rawText)
            return invalid("DXIL validation failed; diagnostics conversion failed");
        ComPtr<IDxcBlobUtf8> text{rawText};
        return invalid("DXIL validation failed: " +
            std::string{text->GetStringPointer(), std::min(text->GetStringLength(), std::size_t{16384})});
    }
    return rhi::Status::success();
}

rhi::Status reflect_dxil(IDxcUtils& utils, IDxcBlob& code,
                         rhi::ShaderStage stage, rhi::ShaderReflection& reflection) {
    const DxcBuffer buffer{code.GetBufferPointer(), code.GetBufferSize(), 0};
    ID3D12ShaderReflection* raw = nullptr;
    const auto reflected = utils.CreateReflection(&buffer, IID_PPV_ARGS(&raw));
    ComPtr<ID3D12ShaderReflection> shader{raw};
    if (FAILED(reflected) || !shader) return invalid("Cannot reflect emitted DXIL");
    D3D12_SHADER_DESC desc{};
    if (FAILED(shader->GetDesc(&desc))) return invalid("Cannot retrieve DXIL shader description");
    if (desc.BoundResources > 4096) return unsupported("Too many resources");
    std::set<std::pair<std::uint32_t, std::uint32_t>> occupied;
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC resource{};
        if (FAILED(shader->GetResourceBindingDesc(i, &resource)) || !resource.Name)
            return invalid("Cannot retrieve DXIL resource description");
        if (!resource.BindCount || resource.BindCount == std::numeric_limits<UINT>::max() ||
            !occupied.emplace(resource.Space, resource.BindPoint).second)
            return unsupported("Resources need bounded arrays and unique register indices across classes");
        rhi::ResourceBinding binding;
        binding.name = resource.Name;
        binding.stage = stage;
        binding.group = resource.Space;
        binding.binding = resource.BindPoint;
        binding.arrayCount = resource.BindCount;
        switch (resource.Type) {
        case D3D_SIT_CBUFFER: {
            binding.type = rhi::ResourceBindingType::buffer;
            auto* constant = shader->GetConstantBufferByName(resource.Name);
            D3D12_SHADER_BUFFER_DESC description{};
            if (!constant || FAILED(constant->GetDesc(&description)))
                return invalid("Cannot retrieve DXIL constant-buffer size");
            binding.minimumSize = description.Size;
            break;
        }
        case D3D_SIT_STRUCTURED:
            binding.type = rhi::ResourceBindingType::buffer;
            if (!resource.NumSamples || resource.NumSamples == std::numeric_limits<UINT>::max())
                return invalid("DXIL structured-buffer stride unavailable");
            binding.minimumSize = resource.NumSamples;
            break;
        case D3D_SIT_BYTEADDRESS:
            binding.type = rhi::ResourceBindingType::buffer;
            binding.minimumSize = 4;
            break;
        case D3D_SIT_UAV_RWSTRUCTURED:
            binding.type = rhi::ResourceBindingType::buffer;
            binding.readOnly = false;
            if (!resource.NumSamples || resource.NumSamples == std::numeric_limits<UINT>::max())
                return invalid("DXIL structured-buffer stride unavailable");
            binding.minimumSize = resource.NumSamples;
            break;
        case D3D_SIT_UAV_RWBYTEADDRESS:
            binding.type = rhi::ResourceBindingType::buffer;
            binding.readOnly = false;
            binding.minimumSize = 4;
            break;
        case D3D_SIT_TEXTURE:
            if (resource.Dimension == D3D_SRV_DIMENSION_BUFFER)
                return unsupported("Typed buffer resources are not representable");
            binding.type = rhi::ResourceBindingType::texture;
            break;
        case D3D_SIT_UAV_RWTYPED:
            if (resource.Dimension == D3D_SRV_DIMENSION_BUFFER)
                return unsupported("Typed buffer resources are not representable");
            binding.type = rhi::ResourceBindingType::texture;
            binding.readOnly = false;
            break;
        case D3D_SIT_SAMPLER: binding.type = rhi::ResourceBindingType::sampler; break;
        default: return unsupported("Resource kind is not representable: " + binding.name);
        }
        reflection.bindings.push_back(std::move(binding));
    }
    if (stage == rhi::ShaderStage::compute) {
        auto& size = reflection.requiredWorkgroupSize;
        if (!shader->GetThreadGroupSize(&size.width, &size.height, &size.depth) ||
            !size.width || !size.height || !size.depth)
            return invalid("Invalid DXIL compute workgroup");
        reflection.preferredWorkgroupSize = size;
    }
    auto created = instance<IDxcContainerReflection>(CLSID_DxcContainerReflection);
    if (!created.ok()) return created.status();
    const auto container = std::move(created).value();
    UINT32 index = 0;
    IDxcBlob* rawPart = nullptr;
    if (FAILED(container->Load(&code)) ||
        FAILED(container->FindFirstPartKind(hlsl::DFCC_PipelineStateValidation, &index)) ||
        FAILED(container->GetPartContent(index, &rawPart)) || !rawPart)
        return invalid("DXIL pipeline-state validation metadata unavailable");
    ComPtr<IDxcBlob> part{rawPart};
    if (part->GetBufferSize() > std::numeric_limits<UINT32>::max())
        return invalid("DXIL pipeline-state validation metadata is too large");
    DxilPipelineStateValidation psv;
    if (!psv.InitFromPSV0(part->GetBufferPointer(), static_cast<UINT32>(part->GetBufferSize())) ||
        !psv.GetPSVRuntimeInfo1())
        return invalid("Cannot decode DXIL pipeline-state validation metadata");
    auto status = interfaces(psv, stage, true, reflection);
    if (!status.ok()) return status;
    return interfaces(psv, stage, false, reflection);
}

} // namespace truffle::shader::detail
