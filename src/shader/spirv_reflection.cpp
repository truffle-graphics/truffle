#include "spirv_reflection.hpp"

#define SPV_ENABLE_UTILITY_CODE
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
#include <SPIRV/spirv.hpp11>
#else
#include <spirv/unified1/spirv.hpp11>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

namespace truffle::shader::detail {
namespace {

using Word = unsigned int;
using Words = std::span<const Word>;

rhi::Status unsupported(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::unsupported, std::move(message));
}

struct Node {
    spv::Op op = spv::Op::OpNop;
    std::vector<Word> args;
};

struct Decoration {
    std::optional<Word> location, binding, set, offset, arrayStride, matrixStride, specId, builtin;
    bool block = false, bufferBlock = false, nonWritable = false, rowMajor = false;
    bool flat = false, noperspective = false, centroid = false, sample = false;
    bool packed = false, extendedIO = false;
};

void decorate(Decoration& d, Words args) {
    const auto value = args.size() > 1 ? args[1] : 0;
    switch (static_cast<spv::Decoration>(args[0])) {
    case spv::Decoration::Location: d.location = value; break;
    case spv::Decoration::Binding: d.binding = value; break;
    case spv::Decoration::DescriptorSet: d.set = value; break;
    case spv::Decoration::Offset: d.offset = value; break;
    case spv::Decoration::ArrayStride: d.arrayStride = value; break;
    case spv::Decoration::MatrixStride: d.matrixStride = value; break;
    case spv::Decoration::SpecId: d.specId = value; break;
    case spv::Decoration::BuiltIn: d.builtin = value; break;
    case spv::Decoration::Block: d.block = true; break;
    case spv::Decoration::BufferBlock: d.bufferBlock = true; break;
    case spv::Decoration::NonWritable: d.nonWritable = true; break;
    case spv::Decoration::RowMajor: d.rowMajor = true; break;
    case spv::Decoration::Flat: d.flat = true; break;
    case spv::Decoration::NoPerspective: d.noperspective = true; break;
    case spv::Decoration::Centroid: d.centroid = true; break;
    case spv::Decoration::Sample: d.sample = true; break;
    case spv::Decoration::Component:
    case spv::Decoration::Index: d.packed |= value != 0; break;
    case spv::Decoration::Patch:
    case spv::Decoration::PerPrimitiveEXT:
    case spv::Decoration::PerVertexKHR:
    case spv::Decoration::PerViewNV:
    case spv::Decoration::PerTaskNV:
    case spv::Decoration::ExplicitInterpAMD: d.extendedIO = true; break;
    default: break;
    }
}

std::string text(Words args) {
    const auto* data = reinterpret_cast<const char*>(args.data());
    const auto size = args.size_bytes();
    const auto* end = static_cast<const char*>(std::memchr(data, 0, size));
    return end ? std::string(data, end) : std::string{};
}

class Module {
public:
    rhi::Status read(Words words) {
        if (words.size() < 5 || words[0] != spv::MagicNumber)
            return unsupported("Invalid SPIR-V reflection module");
        for (std::size_t i = 5; i < words.size();) {
            const auto count = words[i] >> 16;
            const auto op = static_cast<spv::Op>(words[i] & 0xffff);
            if (count == 0 || count > words.size() - i)
                return unsupported("Malformed SPIR-V reflection instruction");
            const auto args = words.subspan(i + 1, count - 1);
            switch (op) {
            case spv::Op::OpName: names[args[0]] = text(args.subspan(1)); break;
            case spv::Op::OpMemberName:
                memberNames[{args[0], args[1]}] = text(args.subspan(2)); break;
            case spv::Op::OpDecorate: decorate(decorations[args[0]], args.subspan(1)); break;
            case spv::Op::OpMemberDecorate:
                decorate(members[{args[0], args[1]}], args.subspan(2)); break;
            case spv::Op::OpExecutionMode:
                if (args[1] == static_cast<Word>(spv::ExecutionMode::LocalSize))
                    workgroup = {args[2], args[3], args[4]};
                break;
            case spv::Op::OpExecutionModeId:
                if (args[1] == static_cast<Word>(spv::ExecutionMode::LocalSizeId))
                    workgroupIds = std::array<Word, 3>{args[2], args[3], args[4]};
                break;
            case spv::Op::OpTypeBool: case spv::Op::OpTypeInt: case spv::Op::OpTypeFloat:
            case spv::Op::OpTypeVector: case spv::Op::OpTypeMatrix: case spv::Op::OpTypeArray:
            case spv::Op::OpTypeRuntimeArray: case spv::Op::OpTypeStruct: case spv::Op::OpTypePointer:
            case spv::Op::OpTypeImage: case spv::Op::OpTypeSampler: case spv::Op::OpTypeSampledImage:
                nodes[args[0]] = {op, {args.begin() + 1, args.end()}}; break;
            case spv::Op::OpVariable:
            case spv::Op::OpConstant: case spv::Op::OpConstantTrue: case spv::Op::OpConstantFalse:
            case spv::Op::OpConstantComposite:
            case spv::Op::OpSpecConstant: case spv::Op::OpSpecConstantTrue:
            case spv::Op::OpSpecConstantFalse: case spv::Op::OpSpecConstantComposite:
            case spv::Op::OpSpecConstantOp:
                nodes[args[1]] = {op, {args.begin(), args.end()}};
                break;
            default: {
                bool hasResult = false, hasType = false;
                spv::HasResultAndType(op, &hasResult, &hasType);
                // Retain unknown type declarations so unsupported resources fail
                // explicitly rather than disappearing or failing an ID lookup.
                if (hasResult && !hasType)
                    nodes[args[0]] = {op, {args.begin() + 1, args.end()}};
                break;
            }
            }
            i += count;
        }
        return rhi::Status::success();
    }

    rhi::Status reflect(rhi::ShaderStage stage, rhi::ShaderReflection& reflection) {
        auto& interfaces = reflection.interfaces;
        for (const auto& [id, node] : nodes) {
            const auto& d = decorations[id];
            if (d.builtin == static_cast<Word>(spv::BuiltIn::WorkgroupSize)) {
                if ((node.op != spv::Op::OpSpecConstantComposite &&
                     node.op != spv::Op::OpConstantComposite) || node.args.size() != 5)
                    return unsupported("Workgroup dimensions require direct scalar constants, not expressions");
                workgroupIds = std::array<Word, 3>{node.args[2], node.args[3], node.args[4]};
            }
            if (d.specId) {
                rhi::ShaderValueType type;
                const auto& scalar = nodes.at(node.args[0]);
                if (scalar.op == spv::Op::OpTypeBool) type = rhi::ShaderValueType::boolean;
                else if (scalar.op == spv::Op::OpTypeInt && scalar.args[0] == 32)
                    type = scalar.args[1] ? rhi::ShaderValueType::sint32 : rhi::ShaderValueType::uint32;
                else if (scalar.op == spv::Op::OpTypeFloat && scalar.args[0] == 32)
                    type = rhi::ShaderValueType::float32;
                else return unsupported("Specialization requires a bool or 32-bit scalar: " + names[id]);
                Word bits = 0;
                if (node.op == spv::Op::OpSpecConstantTrue) bits = 1;
                else if (node.op == spv::Op::OpSpecConstantFalse) bits = 0;
                else if (node.op == spv::Op::OpSpecConstant) bits = node.args[2];
                else return unsupported("Specialization default must be a scalar literal: " + names[id]);
                const auto existing = std::find_if(reflection.specializationConstants.begin(),
                    reflection.specializationConstants.end(), [&](const auto& value) { return value.id == *d.specId; });
                if (existing == reflection.specializationConstants.end())
                    reflection.specializationConstants.push_back({*d.specId, names[id], type, bits});
                else if (existing->type != type || existing->defaultValueBits != bits)
                    return unsupported("Shared specialization ID has inconsistent scalar types/defaults");
            }
        }
        if (workgroupIds) {
            std::array<Word, 3> dimensions{};
            for (std::size_t axis = 0; axis < dimensions.size(); ++axis) {
                const auto id = (*workgroupIds)[axis];
                const auto& node = nodes.at(id);
                if ((node.op != spv::Op::OpConstant && node.op != spv::Op::OpSpecConstant) ||
                    node.args.size() != 3)
                    return unsupported("Workgroup dimensions require direct scalar constants, not expressions");
                const auto& type = nodes.at(node.args[0]);
                if (type.op != spv::Op::OpTypeInt || type.args[0] != 32 || node.args[2] == 0 ||
                    (type.args[1] && node.args[2] > 0x7fffffffu))
                    return unsupported("Workgroup dimensions require positive 32-bit integers");
                if (node.op == spv::Op::OpSpecConstant) {
                    if (!decorations[id].specId)
                        return unsupported("Workgroup specialization requires a direct scalar specialization ID");
                    reflection.workgroupSpecializationIds[axis] = decorations[id].specId;
                }
                dimensions[axis] = node.args[2];
            }
            workgroup = {dimensions[0], dimensions[1], dimensions[2]};
        }
        for (const auto& [id, node] : nodes) {
            if (node.op != spv::Op::OpVariable) continue;
            const auto typeId = nodes.at(node.args[0]).args[1];
            const auto storage = static_cast<spv::StorageClass>(node.args[2]);
            if (storage == spv::StorageClass::Input || storage == spv::StorageClass::Output) {
                Word location = decorations[id].location.value_or(0);
                auto status = io(typeId, decorations[id], names[id], stage,
                                 storage == spv::StorageClass::Input, location,
                                 decorations[id].location.has_value(), interfaces);
                if (!status.ok()) return status;
            } else if (storage == spv::StorageClass::PushConstant) {
                const auto& type = nodes.at(typeId);
                if (type.op != spv::Op::OpTypeStruct || type.args.empty())
                    return unsupported("Push constant must be a non-empty block");
                Word begin = std::numeric_limits<Word>::max();
                for (Word i = 0; i < type.args.size(); ++i) {
                    if (!members[{typeId, i}].offset) return unsupported("Push member has no offset");
                    begin = std::min(begin, *members[{typeId, i}].offset);
                }
                const auto end = size(typeId, {});
                if (!end || *end <= begin || *end - begin > std::numeric_limits<Word>::max() ||
                    begin % 4 || (*end - begin) % 4)
                    return unsupported("Push constant layout is not a representable 4-byte range");
                reflection.pushConstants.push_back({stage, begin, static_cast<Word>(*end - begin)});
            } else if (storage == spv::StorageClass::Uniform ||
                       storage == spv::StorageClass::StorageBuffer ||
                       storage == spv::StorageClass::UniformConstant) {
                auto status = resource(id, typeId, storage, stage, reflection);
                if (!status.ok()) return status;
            } else if (storage == spv::StorageClass::AtomicCounter) {
                return unsupported("Atomic counter resources are not representable");
            }
        }
        std::sort(interfaces.begin(), interfaces.end(), [](const auto& a, const auto& b) {
            return std::tuple{a.direction, a.location} < std::tuple{b.direction, b.location};
        });
        for (std::size_t i = 1; i < interfaces.size(); ++i) {
            if (interfaces[i - 1].direction == interfaces[i].direction &&
                interfaces[i - 1].location == interfaces[i].location)
                return unsupported("Overlapping expanded interface locations");
        }
        if (stage == rhi::ShaderStage::compute) {
            reflection.requiredWorkgroupSize = workgroup;
            reflection.preferredWorkgroupSize = workgroup;
        }
        return rhi::Status::success();
    }

private:
    std::optional<Word> length(Word id) const {
        const auto& n = nodes.at(id);
        if (n.op != spv::Op::OpConstant || n.args.size() != 3 || n.args[2] == 0)
            return {};
        return n.args[2];
    }

    // Minimum addressable bytes, including one element of a runtime buffer tail.
    // Offsets and strides are emitted SPIR-V layout decorations.
    std::optional<std::uint64_t> size(Word id, const Decoration& layout) {
        const auto& n = nodes.at(id);
        if ((n.op == spv::Op::OpTypeFloat || n.op == spv::Op::OpTypeInt) && n.args[0] == 32) return 4;
        if (n.op == spv::Op::OpTypeVector) {
            auto element = size(n.args[0], {});
            if (element) return *element * n.args[1];
        } else if (n.op == spv::Op::OpTypeMatrix) {
            if (!layout.matrixStride) return {};
            const auto& column = nodes.at(n.args[0]);
            if (size(column.args[0], {}) != std::optional<std::uint64_t>{4}) return {};
            const auto count = layout.rowMajor ? column.args[1] : n.args[1];
            const auto width = layout.rowMajor ? n.args[1] : column.args[1];
            return std::uint64_t{*layout.matrixStride} * (count - 1) + width * 4;
        } else if (n.op == spv::Op::OpTypeArray || n.op == spv::Op::OpTypeRuntimeArray) {
            const auto stride = decorations[id].arrayStride;
            const auto count = n.op == spv::Op::OpTypeArray ? length(n.args[1]) : std::optional<Word>{1};
            const auto element = size(n.args[0], layout);
            if (stride && count && element) {
                const auto prefix = std::uint64_t{*stride} * (*count - 1);
                if (*element <= std::numeric_limits<std::uint64_t>::max() - prefix)
                    return prefix + *element;
            }
        } else if (n.op == spv::Op::OpTypeStruct) {
            std::uint64_t end = 0;
            for (Word i = 0; i < n.args.size(); ++i) {
                const auto& member = members[{id, i}];
                const auto bytes = size(n.args[i], member);
                if (!member.offset || !bytes) return {};
                if (*bytes > std::numeric_limits<std::uint64_t>::max() - *member.offset) return {};
                end = std::max(end, *member.offset + *bytes);
            }
            return end;
        }
        return {};
    }

    bool readonly(Word id) {
        if (decorations[id].nonWritable) return true;
        const auto& n = nodes.at(id);
        if (n.op != spv::Op::OpTypeStruct) return false;
        for (Word i = 0; i < n.args.size(); ++i)
            if (!members[{id, i}].nonWritable && !readonly(n.args[i])) return false;
        return !n.args.empty();
    }

    rhi::Status resource(Word id, Word typeId, spv::StorageClass storage,
                         rhi::ShaderStage stage, rhi::ShaderReflection& reflection) {
        const auto& d = decorations[id];
        if (!d.set || !d.binding) return unsupported("Resource needs explicit set/binding: " + names[id]);
        std::uint64_t count = 1;
        std::uint32_t dimensions = 0;
        while (nodes.at(typeId).op == spv::Op::OpTypeArray) {
            if (++dimensions > 1)
                return unsupported("Vulkan resource descriptors require a single array dimension: " + names[id]);
            const auto& array = nodes.at(typeId);
            const auto elements = length(array.args[1]);
            if (!elements || count * *elements > std::numeric_limits<Word>::max())
                return unsupported("Descriptor array needs fixed literal dimensions: " + names[id]);
            count *= *elements;
            typeId = array.args[0];
        }
        const auto& type = nodes.at(typeId);
        rhi::ResourceBinding binding{
            .name = names[id], .stage = stage, .group = *d.set, .binding = *d.binding,
            .arrayCount = static_cast<Word>(count)};
        if (type.op == spv::Op::OpTypeStruct &&
            (decorations[typeId].block || decorations[typeId].bufferBlock)) {
            const auto bytes = size(typeId, {});
            if (!bytes || *bytes == 0 || *bytes > std::numeric_limits<std::size_t>::max())
                return unsupported("Buffer layout has no representable minimum size: " + names[id]);
            binding.type = rhi::ResourceBindingType::buffer;
            binding.minimumSize = static_cast<std::size_t>(*bytes);
            binding.readOnly = (storage == spv::StorageClass::Uniform && !decorations[typeId].bufferBlock) ||
                               d.nonWritable || readonly(typeId);
        } else if (type.op == spv::Op::OpTypeSampler) {
            binding.type = rhi::ResourceBindingType::sampler;
            binding.readOnly = true;
        } else if (type.op == spv::Op::OpTypeImage &&
                   (type.args[1] == static_cast<Word>(spv::Dim::Dim1D) ||
                    type.args[1] == static_cast<Word>(spv::Dim::Dim2D) ||
                    type.args[1] == static_cast<Word>(spv::Dim::Dim3D) ||
                    type.args[1] == static_cast<Word>(spv::Dim::Cube))) {
            binding.type = rhi::ResourceBindingType::texture;
            binding.readOnly = type.args[5] == 1 || d.nonWritable;
        } else {
            return unsupported("Resource is not representable (combined sampler, subpass input, "
                               "unsized descriptor array, texel buffer or unsupported type): " + names[id]);
        }
        reflection.bindings.push_back(std::move(binding));
        return rhi::Status::success();
    }

    rhi::Status io(Word id, Decoration d, const std::string& name, rhi::ShaderStage stage,
                   bool input, Word& location, bool located,
                   std::vector<rhi::ShaderInterfaceVariable>& result) {
        if (d.builtin) return rhi::Status::success();
        if (d.packed) return unsupported("Component/index-packed IO is not representable: " + name);
        if (d.extendedIO) return unsupported("Extended per-vertex/primitive/view/task or explicit-interpolation IO is not representable: " + name);
        const auto& n = nodes.at(id);
        if (n.op == spv::Op::OpTypeArray || n.op == spv::Op::OpTypeMatrix) {
            const auto count = n.op == spv::Op::OpTypeArray ? length(n.args[1]) : std::optional<Word>{n.args[1]};
            if (!count || *count > 4096) return unsupported("IO requires bounded fixed dimensions: " + name);
            for (Word i = 0; i < *count; ++i) {
                auto status = io(n.args[0], d, name + "[" + std::to_string(i) + "]",
                                 stage, input, location, located, result);
                if (!status.ok()) return status;
            }
            return rhi::Status::success();
        }
        if (n.op == spv::Op::OpTypeStruct) {
            for (Word i = 0; i < n.args.size(); ++i) {
                auto member = members[{id, i}];
                member.flat |= d.flat;
                member.noperspective |= d.noperspective;
                member.centroid |= d.centroid;
                member.sample |= d.sample;
                member.packed |= d.packed;
                member.extendedIO |= d.extendedIO;
                if (member.location) location = *member.location;
                auto status = io(n.args[i], member, name + "." + memberNames[{id, i}], stage, input,
                                 location, located || member.location.has_value(), result);
                if (!status.ok()) return status;
            }
            return rhi::Status::success();
        }
        const auto scalarId = n.op == spv::Op::OpTypeVector ? n.args[0] : id;
        const auto width = n.op == spv::Op::OpTypeVector ? n.args[1] : 1;
        const auto& scalar = nodes.at(scalarId);
        const bool floating = scalar.op == spv::Op::OpTypeFloat;
        if (!located || (scalar.op != spv::Op::OpTypeInt && !floating) ||
            scalar.args[0] != 32 || width == 0 || width > 4 ||
            location == std::numeric_limits<Word>::max() || result.size() >= 4096)
            return unsupported("IO requires located 32-bit scalar/vector leaves: " + name);
        const bool signedInt = !floating && scalar.args[1] != 0;
        auto spelling = width == 1 ? std::string{floating ? "float" : signedInt ? "int" : "uint"}
            : std::string{floating ? "vec" : signedInt ? "ivec" : "uvec"} + std::to_string(width);
        std::string interpolation = d.flat ? "flat" : d.noperspective ? "noperspective" : "smooth";
        if (d.centroid) interpolation = "centroid " + interpolation;
        if (d.sample) interpolation = "sample " + interpolation;
        result.push_back({stage, input ? rhi::ShaderInterfaceDirection::input : rhi::ShaderInterfaceDirection::output,
                          location++, name, std::move(spelling), std::move(interpolation)});
        return rhi::Status::success();
    }

    std::map<Word, Node> nodes;
    std::map<Word, Decoration> decorations;
    std::map<std::pair<Word, Word>, Decoration> members;
    std::map<Word, std::string> names;
    std::map<std::pair<Word, Word>, std::string> memberNames;
    rhi::Extent3D workgroup{1, 1, 1};
    std::optional<std::array<Word, 3>> workgroupIds;
};

} // namespace

rhi::Status reflect_spirv(Words words, rhi::ShaderStage stage,
                          rhi::ShaderReflection& reflection) {
    Module module;
    auto status = module.read(words);
    if (!status.ok()) return status;
    return module.reflect(stage, reflection);
}

} // namespace truffle::shader::detail
