#include "glslang_compiler.hpp"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>

namespace truffle::shaderc {
namespace {

constexpr std::size_t maximum_diagnostic_bytes = 16u * 1024u;

struct ProcessLifetime {
    ProcessLifetime() : initialized(glslang_initialize_process() != 0) {}
    ~ProcessLifetime() {
        if (initialized) {
            glslang_finalize_process();
        }
    }

    bool initialized = false;
};

struct ShaderDeleter {
    void operator()(glslang_shader_t* shader) const noexcept {
        glslang_shader_delete(shader);
    }
};

struct ProgramDeleter {
    void operator()(glslang_program_t* program) const noexcept {
        glslang_program_delete(program);
    }
};

[[nodiscard]] glslang_stage_t glslang_stage(rhi::ShaderStage stage) noexcept {
    switch (stage) {
    case rhi::ShaderStage::vertex:
        return GLSLANG_STAGE_VERTEX;
    case rhi::ShaderStage::fragment:
        return GLSLANG_STAGE_FRAGMENT;
    case rhi::ShaderStage::compute:
        return GLSLANG_STAGE_COMPUTE;
    }
    return GLSLANG_STAGE_COUNT;
}

[[nodiscard]] std::string bounded_diagnostics(const char* first,
                                              const char* second = nullptr) {
    std::string result;
    const auto append = [&](const char* text) {
        if (text == nullptr || *text == '\0' ||
            result.size() >= maximum_diagnostic_bytes) {
            return;
        }
        if (!result.empty()) {
            result.push_back('\n');
        }
        const std::string_view view{text};
        const auto remaining = maximum_diagnostic_bytes - result.size();
        result.append(view.substr(0, remaining));
    };
    append(first);
    append(second);
    return result;
}

[[nodiscard]] rhi::Status compilation_failure(std::string_view step,
                                              const char* first,
                                              const char* second = nullptr) {
    auto message = std::string{"glslang "} + std::string{step} + " failed";
    const auto diagnostics = bounded_diagnostics(first, second);
    if (!diagnostics.empty()) {
        message += ": " + diagnostics;
    }
    return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                std::move(message));
}

} // namespace

rhi::Result<GlslangOutput> compile_glsl_to_spirv(
    std::string_view source, rhi::ShaderStage stage, std::string_view entryPoint,
    bool embeddedProfile) {
    if (source.empty() || entryPoint.empty() ||
        source.find('\0') != std::string_view::npos) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                    "GLSL source and entry point must be non-empty");
    }
    const auto nativeStage = glslang_stage(stage);
    if (nativeStage == GLSLANG_STAGE_COUNT) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                    "glslang does not support this shader stage");
    }

    ProcessLifetime process;
    if (!process.initialized) {
        return rhi::Status::failure(rhi::StatusCode::unavailable,
                                    "glslang process initialization failed");
    }

    std::string sourceStorage{source};
    std::string entryStorage{entryPoint};
    const auto messages = static_cast<glslang_messages_t>(
        GLSLANG_MSG_DEFAULT_BIT | GLSLANG_MSG_SPV_RULES_BIT |
        GLSLANG_MSG_VULKAN_RULES_BIT);
    const glslang_input_t input{
        .language = GLSLANG_SOURCE_GLSL,
        .stage = nativeStage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_1,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_3,
        .code = sourceStorage.c_str(),
        .default_version = embeddedProfile ? 310 : 450,
        .default_profile = embeddedProfile ? GLSLANG_ES_PROFILE
                                           : GLSLANG_CORE_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = messages,
        .resource = glslang_default_resource(),
        .callbacks = {},
        .callbacks_ctx = nullptr,
    };
    std::unique_ptr<glslang_shader_t, ShaderDeleter> shader{
        glslang_shader_create(&input)};
    if (!shader) {
        return rhi::Status::failure(rhi::StatusCode::out_of_memory,
                                    "glslang shader allocation failed");
    }
    glslang_shader_set_entry_point(shader.get(), entryStorage.c_str());
    if (!glslang_shader_preprocess(shader.get(), &input)) {
        return compilation_failure("preprocessing",
                                   glslang_shader_get_info_log(shader.get()),
                                   glslang_shader_get_info_debug_log(shader.get()));
    }
    glslang_shader_set_preprocessed_code(
        shader.get(), glslang_shader_get_preprocessed_code(shader.get()));
    if (!glslang_shader_parse(shader.get(), &input)) {
        return compilation_failure("parsing",
                                   glslang_shader_get_info_log(shader.get()),
                                   glslang_shader_get_info_debug_log(shader.get()));
    }

    std::unique_ptr<glslang_program_t, ProgramDeleter> program{
        glslang_program_create()};
    if (!program) {
        return rhi::Status::failure(rhi::StatusCode::out_of_memory,
                                    "glslang program allocation failed");
    }
    glslang_program_add_shader(program.get(), shader.get());
    if (!glslang_program_link(program.get(), messages)) {
        return compilation_failure("linking",
                                   glslang_program_get_info_log(program.get()),
                                   glslang_program_get_info_debug_log(program.get()));
    }
    glslang_program_SPIRV_generate(program.get(), nativeStage);
    const auto wordCount = glslang_program_SPIRV_get_size(program.get());
    if (wordCount == 0 ||
        wordCount > std::numeric_limits<std::size_t>::max() /
                        sizeof(std::uint32_t)) {
        return compilation_failure(
            "SPIR-V generation",
            glslang_program_SPIRV_get_messages(program.get()));
    }
    std::vector<std::uint32_t> words(wordCount);
    glslang_program_SPIRV_get(program.get(), words.data());
    if (words.front() != 0x07230203u) {
        return rhi::Status::failure(rhi::StatusCode::backend_error,
                                    "glslang returned invalid SPIR-V bytecode");
    }

    GlslangOutput output;
    output.spirv.resize(words.size() * sizeof(std::uint32_t));
    std::memcpy(output.spirv.data(), words.data(), output.spirv.size());
    glslang_version_t version{};
    glslang_get_version(&version);
    std::ostringstream versionText;
    versionText << version.major << '.' << version.minor << '.' << version.patch;
    if (version.flavor != nullptr && *version.flavor != '\0') {
        versionText << '-' << version.flavor;
    }
    output.compilerVersion = versionText.str();
    output.compilerRevision = TRUFFLE_GLSLANG_REVISION;
    output.diagnostics = bounded_diagnostics(
        glslang_shader_get_info_log(shader.get()),
        glslang_program_SPIRV_get_messages(program.get()));
    return output;
}

} // namespace truffle::shaderc
