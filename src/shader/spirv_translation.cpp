#include "spirv_translation.hpp"

#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>

#include <algorithm>
#include <cstring>
#include <set>

namespace spirv_cross = SPIRV_CROSS_NAMESPACE;
namespace spv = SPIRV_CROSS_SPV_HEADER_NAMESPACE;

namespace truffle::shader::detail {

std::string translation_provenance() {
    return ";spirv-cross=" TRUFFLE_SPIRV_CROSS_REVISION
        ";truffle-cross=2;msl=macos-2.1;glsl=410-core;essl=300-es;"
        "flat-scalar-resources;gl-fixup-clipspace;metal-vertex-reserve=16;"
        "metal-no-auxiliary-bindings;"
        "cross-namespace=truffle_spirv_cross;cross-spv-namespace=truffle_spirv_cross_spv;cross-exceptions";
}

namespace {

rhi::Status unsupported(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::unsupported, "SPIRV-Cross: " + std::move(message));
}

spv::ExecutionModel execution_model(rhi::ShaderStage stage) {
    switch (stage) {
    case rhi::ShaderStage::vertex: return spv::ExecutionModelVertex;
    case rhi::ShaderStage::fragment: return spv::ExecutionModelFragment;
    case rhi::ShaderStage::compute: return spv::ExecutionModelGLCompute;
    }
    return spv::ExecutionModelMax;
}

void payload(rhi::ShaderVariantDesc& variant, const std::string& text) {
    variant.format = rhi::ShaderByteFormat::native_source;
    variant.code.resize(text.size());
    std::memcpy(variant.code.data(), text.data(), text.size());
}

void remap(std::vector<rhi::ShaderBindingRemap>& output,
           const rhi::ShaderVariantDesc& variant, const rhi::ResourceBinding& binding,
           std::uint32_t slot) {
    output.push_back({.target = variant.target, .stage = variant.stage,
                      .group = binding.group, .binding = binding.binding,
                      .nativeBinding = slot});
}

rhi::ResourceBinding* binding_for(spirv_cross::Compiler& compiler,
                                 rhi::ShaderVariantDesc& variant, std::uint32_t id) {
    const auto group = compiler.get_decoration(id, spv::DecorationDescriptorSet);
    const auto binding = compiler.get_decoration(id, spv::DecorationBinding);
    auto& bindings = variant.reflection.bindings;
    const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& value) {
        return value.group == group && value.binding == binding;
    });
    return found == bindings.end() ? nullptr : &*found;
}

rhi::Status metal(std::span<const unsigned int> words, const CompileRequest& request,
                  rhi::ShaderVariantDesc& variant, std::vector<rhi::ShaderBindingRemap>& remaps) {
    spirv_cross::CompilerMSL compiler{words.data(), words.size()};
    if (!compiler.get_shader_resources().storage_images.empty())
        return unsupported("Native storage image generation is not supported");
    auto options = compiler.get_msl_options();
    options.platform = spirv_cross::CompilerMSL::Options::macOS;
    options.set_msl_version(2, 1);
    options.argument_buffers = false;
    compiler.set_msl_options(options);
    // RHI exposes 16 vertex streams; 30 belongs to its push-data contract.
    std::uint32_t buffer = 16, texture = 0, sampler = 0;
    for (const auto& binding : variant.reflection.bindings) {
        auto& next = binding.type == rhi::ResourceBindingType::buffer ? buffer :
                     binding.type == rhi::ResourceBindingType::texture ? texture : sampler;
        const auto limit = binding.type == rhi::ResourceBindingType::buffer ? 30u :
                           binding.type == rhi::ResourceBindingType::texture ? 128u : 16u;
        if (next >= limit) return unsupported("Metal flat resource slot limit exceeded");
        spirv_cross::MSLResourceBinding mapping;
        mapping.stage = execution_model(request.stage);
        mapping.desc_set = binding.group;
        mapping.binding = binding.binding;
        mapping.msl_buffer = mapping.msl_texture = mapping.msl_sampler = next;
        compiler.add_msl_resource_binding(mapping);
        remap(remaps, variant, binding, next++);
    }
    if (!variant.reflection.pushConstants.empty()) {
        spirv_cross::MSLResourceBinding mapping;
        mapping.stage = execution_model(request.stage);
        mapping.desc_set = spirv_cross::kPushConstDescSet;
        mapping.binding = spirv_cross::kPushConstBinding;
        mapping.msl_buffer = 30;
        compiler.add_msl_resource_binding(mapping);
    }
    const auto text = compiler.compile();
    if (compiler.needs_buffer_size_buffer())
        return unsupported("Metal runtime buffer length requires an auxiliary buffer-size binding "
                           "outside the RHI contract");
    if (compiler.needs_swizzle_buffer() || compiler.needs_view_mask_buffer() ||
        compiler.needs_dispatch_base_buffer() || compiler.needs_output_buffer() ||
        compiler.needs_patch_output_buffer() || compiler.needs_input_threadgroup_mem())
        return unsupported("Metal auxiliary resource bindings are outside the RHI contract");
    payload(variant, text);
    variant.nativeEntryPoint = compiler.get_cleansed_entry_point_name(
        request.entryPoint, execution_model(request.stage));
    return rhi::Status::success();
}

rhi::Status glsl(std::span<const unsigned int> words, const CompileRequest& request,
                 rhi::ShaderVariantDesc& variant, std::vector<rhi::ShaderBindingRemap>& remaps) {
    if (!variant.reflection.specializationConstants.empty() ||
        !variant.reflection.pushConstants.empty())
        return unsupported("GLSL 410/ES 300 generation does not yet lower specialization or push constants");
    spirv_cross::CompilerGLSL compiler{words.data(), words.size()};
    auto options = compiler.get_common_options();
    options.es = request.target == rhi::ShaderTarget::glsl_es;
    options.version = options.es ? 300 : 410;
    options.enable_420pack_extension = false;
    options.vertex.fixup_clipspace = true;
    compiler.set_common_options(options);
    const auto resources = compiler.get_shader_resources();
    const auto active = compiler.get_active_interface_variables();
    for (const auto* collection : {&resources.uniform_buffers, &resources.separate_images,
                                   &resources.separate_samplers}) {
        for (const auto& resource : *collection)
            if (!active.contains(resource.id))
                return unsupported("GL native reflection requires statically used resources");
    }
    if (!resources.storage_buffers.empty() || !resources.storage_images.empty())
        return unsupported("GLSL 410/ES 300 have no storage resource route");
    // Linked GL programs use names rather than internal varying locations.
    for (const auto& resource : request.stage == rhi::ShaderStage::vertex
                                    ? resources.stage_outputs : resources.stage_inputs) {
        const auto& type = compiler.get_type(resource.type_id);
        if (!type.array.empty() || type.columns != 1 || type.basetype == spirv_cross::SPIRType::Struct)
            return unsupported("GLSL internal IO generation currently requires scalar/vector leaves");
        compiler.set_name(resource.id, "TruffleVarying" +
            std::to_string(compiler.get_decoration(resource.id, spv::DecorationLocation)));
    }
    const auto stageBase = static_cast<std::uint32_t>(request.stage) * 16;
    std::uint32_t buffer = stageBase, texture = stageBase;
    for (const auto& resource : resources.uniform_buffers) {
        auto* binding = binding_for(compiler, variant, resource.id);
        if (!binding) return unsupported("Uniform has no canonical logical binding");
        binding->name = "TruffleUniform" + std::to_string(static_cast<unsigned int>(request.stage)) +
                        "_" + std::to_string(binding->group) + "_" + std::to_string(binding->binding);
        compiler.set_name(resource.base_type_id, binding->name);
        compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
        compiler.unset_decoration(resource.id, spv::DecorationBinding);
        if (buffer >= stageBase + 16) return unsupported("GL uniform slot budget exceeded");
        remap(remaps, variant, *binding, buffer++);
    }
    if (compiler.build_dummy_sampler_for_combined_images() != 0)
        return unsupported("Texture operations requiring a synthesized sampler are not supported");
    compiler.build_combined_image_samplers();
    std::set<std::uint32_t> images, samplers;
    for (const auto& combined : compiler.get_combined_image_samplers()) {
        if (!images.insert(combined.image_id).second || !samplers.insert(combined.sampler_id).second)
            return unsupported("GL combined sampling requires one-to-one scalar texture/sampler pairs");
        auto* image = binding_for(compiler, variant, combined.image_id);
        auto* sampler = binding_for(compiler, variant, combined.sampler_id);
        if (!image || !sampler) return unsupported("Combined sampler has no logical texture/sampler pair");
        if (texture >= stageBase + 16) return unsupported("GL texture unit budget exceeded");
        const auto name = "TruffleSample" + std::to_string(static_cast<unsigned int>(request.stage)) +
                          "_" + std::to_string(image->group) + "_" + std::to_string(image->binding);
        compiler.set_name(combined.combined_id, name);
        compiler.unset_decoration(combined.combined_id, spv::DecorationDescriptorSet);
        compiler.unset_decoration(combined.combined_id, spv::DecorationBinding);
        image->name = sampler->name = name;
        remap(remaps, variant, *image, texture);
        remap(remaps, variant, *sampler, texture++);
    }
    if (images.size() != resources.separate_images.size() ||
        samplers.size() != resources.separate_samplers.size())
        return unsupported("Every GL texture and sampler must have one reflected combined use");
    const auto text = compiler.compile();
    if (!compiler.get_required_extensions().empty())
        return unsupported("GLSL 410/ES 300 generation requires an extension outside the selected core profile: " +
                           compiler.get_required_extensions().front());
    payload(variant, text);
    variant.nativeEntryPoint = "main";
    return rhi::Status::success();
}

} // namespace

rhi::Status translate_spirv(std::span<const unsigned int> words,
                            const CompileRequest& request, rhi::ShaderVariantDesc& variant,
                            std::vector<rhi::ShaderBindingRemap>& remaps) {
    for (const auto& binding : variant.reflection.bindings) {
        if (binding.arrayCount != 1)
            return unsupported("Native generation currently requires scalar resource descriptors");
        if (binding.type == rhi::ResourceBindingType::texture && !binding.readOnly)
            return unsupported("Native storage image generation is not supported");
    }
    try {
        return request.target == rhi::ShaderTarget::metal_source
                   ? metal(words, request, variant, remaps) : glsl(words, request, variant, remaps);
    } catch (const spirv_cross::CompilerError& error) {
        return unsupported(std::string{error.what()}.substr(0, 16384));
    }
}

} // namespace truffle::shader::detail
