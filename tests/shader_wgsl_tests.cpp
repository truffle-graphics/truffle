#include "shader_test_utils.hpp"
#include "shader_wgsl_fixtures.hpp"

#include <cstring>
#include <iostream>

int main() {
    using namespace truffle;
    try {
        shader::Compiler compiler;
        auto request = tests::wgsl_request(rhi::ShaderStage::compute);
        if (!tests::shader_route_enabled(rhi::ShaderSourceLanguage::wgsl)) {
            const auto disabled = compiler.compile(request);
            TRUFFLE_SHADER_CHECK(!disabled.ok() && disabled.status().code == rhi::StatusCode::unsupported);
            return 0;
        }
        for (const auto stage : {rhi::ShaderStage::vertex, rhi::ShaderStage::fragment, rhi::ShaderStage::compute}) {
            auto input = tests::wgsl_request(stage);
            const auto result = tests::require(compiler.compile(input));
            const auto& variant = result.package.desc().variants.front();
            TRUFFLE_SHADER_CHECK(result.compiler.name == "Tint" && !result.cacheHit);
            TRUFFLE_SHADER_CHECK(result.dependencies.size() == 1 && result.dependencies.front().identity == input.source.identity);
            TRUFFLE_SHADER_CHECK(variant.reflection.bindings.size() ==
                (stage == rhi::ShaderStage::vertex ? 0u : stage == rhi::ShaderStage::fragment ? 3u : 1u));
            if (stage == rhi::ShaderStage::vertex) {
                TRUFFLE_SHADER_CHECK(result.interfaces().size() == 2);
                TRUFFLE_SHADER_CHECK(result.interfaces().front().location == 0);
            } else if (stage == rhi::ShaderStage::fragment) {
                TRUFFLE_SHADER_CHECK(result.interfaces().size() == 2);
                const auto& bindings = variant.reflection.bindings;
                TRUFFLE_SHADER_CHECK(bindings[0].group == 1 && bindings[0].binding == 5 &&
                    bindings[0].type == rhi::ResourceBindingType::texture);
                TRUFFLE_SHADER_CHECK(bindings[1].binding == 6 && bindings[1].type == rhi::ResourceBindingType::sampler);
                TRUFFLE_SHADER_CHECK(bindings[2].binding == 7 && bindings[2].minimumSize == 16 && bindings[2].readOnly);
            } else {
                TRUFFLE_SHADER_CHECK(variant.reflection.requiredWorkgroupSize == rhi::Extent3D({2, 1, 1}));
                TRUFFLE_SHADER_CHECK(variant.reflection.bindings[0].minimumSize == 16 &&
                    !variant.reflection.bindings[0].readOnly && variant.reflection.bindings[0].binding == 9);
            }
            const auto hit = tests::require(compiler.compile(input));
            TRUFFLE_SHADER_CHECK(hit.cacheHit && hit.cacheKey == result.cacheKey);
            TRUFFLE_SHADER_CHECK(rhi::ShaderPackage::load(result.package.bytes()).ok());
            auto intermediate = input;
            intermediate.language = rhi::ShaderSourceLanguage::spirv;
            intermediate.target = rhi::ShaderTarget::wgsl;
            intermediate.source.identity = "intermediate.spv";
            intermediate.source.text.assign(reinterpret_cast<const char*>(variant.code.data()), variant.code.size());
            const auto imported = tests::require(compiler.compile(intermediate));
            TRUFFLE_SHADER_CHECK(imported.package.desc().variants.front().format == rhi::ShaderByteFormat::native_source);
            input.target = rhi::ShaderTarget::wgsl;
            const auto generated = tests::require(compiler.compile(input));
            TRUFFLE_SHADER_CHECK(generated.cacheKey != result.cacheKey);
            TRUFFLE_SHADER_CHECK(!rhi::ShaderPackage::load(generated.package.bytes()).ok());
            TRUFFLE_SHADER_CHECK(rhi::ShaderPackage::load(generated.package.bytes(),
                {.allowExperimentalWgslGeneration = true}).ok());
            const auto& text = generated.package.desc().variants.front().code;
            input.source.text.assign(reinterpret_cast<const char*>(text.data()), text.size());
            input.target = rhi::ShaderTarget::spirv;
            const auto recompiled = tests::require(compiler.compile(input));
            const auto& reflection = recompiled.package.desc().variants.front().reflection;
            TRUFFLE_SHADER_CHECK(reflection.bindings.size() == variant.reflection.bindings.size());
            for (std::size_t i = 0; i < reflection.bindings.size(); ++i) {
                const auto& a = reflection.bindings[i];
                const auto& b = variant.reflection.bindings[i];
                TRUFFLE_SHADER_CHECK(a.stage == b.stage && a.type == b.type &&
                    a.group == b.group && a.binding == b.binding && a.arrayCount == b.arrayCount &&
                    a.minimumSize == b.minimumSize && a.readOnly == b.readOnly);
            }
            TRUFFLE_SHADER_CHECK(reflection.interfaces == variant.reflection.interfaces);
            TRUFFLE_SHADER_CHECK(reflection.requiredWorkgroupSize == variant.reflection.requiredWorkgroupSize);
        }
        const auto reject = [&](shader::CompileRequest input, rhi::StatusCode code) {
            const auto result = compiler.compile(input);
            if (result.ok()) throw std::runtime_error("Expected request failure");
            TRUFFLE_SHADER_CHECK(result.status().code == code && !result.status().message.empty());
        };
        if (tests::shader_route_enabled()) {
            shader::CompileRequest glsl;
            glsl.stage = rhi::ShaderStage::compute;
            glsl.source = {"existing.comp", "#version 450\nlayout(local_size_x=2) in;\n"
                "layout(set=1,binding=9,std430) buffer Data { uint values[4]; } data;\n"
                "void main(){data.values[gl_GlobalInvocationID.x]=12u+gl_GlobalInvocationID.x;}\n"};
            const auto existing = tests::require(compiler.compile(glsl));
            const auto& code = existing.package.desc().variants.front().code;
            auto imported = request;
            imported.language = rhi::ShaderSourceLanguage::spirv;
            imported.target = rhi::ShaderTarget::wgsl;
            imported.source = {"glslang-intermediate.spv", std::string{
                reinterpret_cast<const char*>(code.data()), code.size()}};
            const auto converted = tests::require(compiler.compile(imported));
            TRUFFLE_SHADER_CHECK(converted.package.desc().variants.front().reflection.bindings.front().binding == 9);
        }
        auto invalid = request;
        invalid.source.text += "\nthis is not WGSL\n";
        reject(invalid, rhi::StatusCode::invalid_argument);
        invalid = request; invalid.entryPoint = "missing"; reject(invalid, rhi::StatusCode::invalid_argument);
        invalid = request; invalid.stage = rhi::ShaderStage::vertex; reject(invalid, rhi::StatusCode::invalid_argument);
        invalid = request; invalid.defines = {{"X", "1"}}; reject(invalid, rhi::StatusCode::unsupported);
        invalid = request; invalid.includeRoots = {"."}; reject(invalid, rhi::StatusCode::unsupported);
        invalid = request; invalid.generateDebugInfo = true; reject(invalid, rhi::StatusCode::unsupported);
        invalid = request; invalid.resolver = [](auto, auto, bool) -> rhi::Result<shader::Source> {
            throw std::runtime_error("WGSL resolver must not run");
        }; reject(invalid, rhi::StatusCode::unsupported);
        invalid = request; invalid.vulkanTarget = {shader::VulkanVersion::v1_0, shader::SpirvVersion::v1_0};
        reject(invalid, rhi::StatusCode::unsupported);
        invalid = request; invalid.target = rhi::ShaderTarget::wgsl; invalid.allowExperimentalWgslGeneration = false;
        reject(invalid, rhi::StatusCode::unsupported);
        invalid = request;
        invalid.source.text = "override width: u32 = 2; @compute @workgroup_size(width) fn main() {}";
        reject(invalid, rhi::StatusCode::unsupported);
        invalid = request;
        invalid.language = rhi::ShaderSourceLanguage::spirv;
        invalid.target = rhi::ShaderTarget::wgsl;
        invalid.source.text = "invalid SPIR-V";
        reject(invalid, rhi::StatusCode::invalid_argument);
        shader::RebuildState rebuild;
        const auto first = tests::require(rebuild.rebuild(compiler, request));
        auto changed = request; changed.source.text += "\n// changed source chunk\n";
        const auto second = tests::require(rebuild.rebuild(compiler, changed));
        TRUFFLE_SHADER_CHECK(second.cacheKey != first.cacheKey && !second.cacheHit);
        changed.source.text += "invalid";
        TRUFFLE_SHADER_CHECK(!rebuild.rebuild(compiler, changed).ok());
        TRUFFLE_SHADER_CHECK(rebuild.last_good()->cacheKey == second.cacheKey);
        changed.source.text = request.source.text + "\n// changed source chunk\n";
        TRUFFLE_SHADER_CHECK(tests::require(rebuild.rebuild(compiler, changed)).cacheHit);
        compiler.clear_cache();
        TRUFFLE_SHADER_CHECK(!tests::require(compiler.compile(changed)).cacheHit);
        auto multi = request;
        multi.source.text += "\n@compute @workgroup_size(4) fn other() {}\n";
        const auto selected = tests::require(compiler.compile(multi));
        multi.entryPoint = "other";
        const auto other = tests::require(compiler.compile(multi));
        TRUFFLE_SHADER_CHECK(selected.cacheKey != other.cacheKey);
        TRUFFLE_SHADER_CHECK(other.package.desc().variants.front().reflection.requiredWorkgroupSize == rhi::Extent3D({4, 1, 1}));
        std::cout << "WGSL routes, reflection, real output validation, rejection, cache and rebuild passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
