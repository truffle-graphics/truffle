#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/source_program.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace {

using namespace truffle;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "check failed: " << message << '\n';
        std::exit(1);
    }
}

rhi::ShaderPackage prepared_package() {
    rhi::ShaderPackageDesc desc;
    desc.name = "prepared beside source programs";
    desc.sources = {{.path = "fixture", .language = rhi::ShaderSourceLanguage::backend_native,
                     .sha256 = "df7e70e5021544f4834bbee64a9e3789febc4be81470df629cad6ddb03320a5c"}};
    desc.compilers = {{.name = "Null fixture", .version = "1"}};
    desc.permutations = {{.name = "configured"}};
    for (const auto stage : {rhi::ShaderStage::vertex, rhi::ShaderStage::fragment}) {
        desc.variants.push_back({
            .target = rhi::ShaderTarget::metal_source,
            .format = rhi::ShaderByteFormat::native_source,
            .kind = rhi::ShaderVariantKind::native_override,
            .stage = stage,
            .entryPoint = "main",
            .permutation = "configured",
            .code = {std::byte{0x42}},
        });
    }
    auto result = rhi::ShaderPackage::create(std::move(desc));
    expect(result.ok(), "prepared package");
    return std::move(result).value();
}

} // namespace

int main() {
    auto context = tests::make_null_context();
    renderer::PipelineCache cache(context.device);
    constexpr renderer::MaterialId prepared = 7;
    constexpr renderer::MaterialId compiled = 8;
    {
        auto package = prepared_package();
        renderer::ProgramDesc desc;
        desc.vertex = {&package, rhi::ShaderTarget::metal_source, "main",
                       rhi::ShaderStage::vertex, "configured"};
        desc.fragment = {&package, rhi::ShaderTarget::metal_source, "main",
                         rhi::ShaderStage::fragment, "configured"};
        TRUFFLE_CHECK(cache.register_program(prepared, desc).ok());
    }
    const auto preparedId = cache.published(prepared)->id();
    TRUFFLE_CHECK(cache.get_or_create({}, prepared).value()->id() == preparedId);
    TRUFFLE_CHECK(cache.published(compiled) == nullptr);

    std::map<std::string, std::string> files{
        {"color.glsl", "vec4 shade() { return vec4(COLOR, 1.0); }\n"}};
    renderer::SourceProgramRequest request;
    request.vertex.source = {"program.vert", R"(#version 450
layout(location=0) flat out vec4 tint;
void main() { gl_Position = vec4(0); tint = vec4(1); }
)"};
    request.fragment.source = {"program.frag", R"(#version 450
#include "color.glsl"
layout(location=0) flat in vec4 tint;
#ifdef EXTRA_INPUT
layout(location=1) flat in vec4 extra;
#endif
layout(location=0) out vec4 color;
void main() { color = shade() * tint; }
)"};
    request.fragment.resolver = [&](std::string_view name, std::string_view,
                                    bool) -> rhi::Result<shader::Source> {
        const auto found = files.find(std::string{name});
        if (found == files.end()) {
            return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                        "missing " + std::string{name});
        }
        return shader::Source{found->first, found->second};
    };
    request.fragment.defines = {{"COLOR", "vec3(1, 0, 0)"}};

    shader::Compiler compiler;
    renderer::SourceProgram program(cache, compiled, {});
    if (!shader::Compiler::available()) {
        // Compiler-disabled build: Renderer links Shader's core only, prepared
        // programs keep working and source requests are explicitly unsupported.
        auto attempt = program.rebuild(compiler, request);
        TRUFFLE_CHECK(!attempt.ok());
        TRUFFLE_CHECK(attempt.status().code == base::StatusCode::unsupported);
        TRUFFLE_CHECK(program.published_vertex() == nullptr);
        TRUFFLE_CHECK(cache.published(compiled) == nullptr);
        renderer::SourceProgram over(cache, prepared, {});
        TRUFFLE_CHECK(over.rebuild(compiler, request).status().code ==
                      base::StatusCode::unsupported);
        TRUFFLE_CHECK(cache.published(prepared)->id() == preparedId);
        return 0;
    }

    auto first = program.rebuild(compiler, request);
    TRUFFLE_CHECK(first.ok());
    TRUFFLE_CHECK(first.value().published && !first.value().fragmentCacheHit);
    const auto firstId = cache.published(compiled)->id();
    TRUFFLE_CHECK(program.published_fragment()->package.desc().variants.size() == 1);
    TRUFFLE_CHECK(program.published_fragment()->dependencies.size() == 2);
    const auto created = context.instance.stats().pipelinesCreated;

    auto unchanged = program.rebuild(compiler, request);
    TRUFFLE_CHECK(unchanged.ok() && !unchanged.value().published);
    TRUFFLE_CHECK(unchanged.value().vertexCacheHit && unchanged.value().fragmentCacheHit);
    TRUFFLE_CHECK(context.instance.stats().pipelinesCreated == created);
    TRUFFLE_CHECK(cache.published(compiled)->id() == firstId);

    // A slot changed behind the source program is republished from cached results.
    cache.invalidate(compiled);
    auto restored = program.rebuild(compiler, request);
    TRUFFLE_CHECK(restored.ok() && restored.value().published &&
                  restored.value().fragmentCacheHit);
    const auto restoredId = cache.published(compiled)->id();
    TRUFFLE_CHECK(restoredId != firstId);

    files["color.glsl"] = "vec4 shade() { return vec4(COLOR.bgr, 1.0); }\n";
    auto changed = program.rebuild(compiler, request);
    TRUFFLE_CHECK(changed.ok() && changed.value().published &&
                  changed.value().vertexCacheHit && !changed.value().fragmentCacheHit);
    const auto changedId = cache.published(compiled)->id();
    const auto changedKey = program.published_fragment()->cacheKey;

    const auto rejected = [&](const renderer::SourceProgramRequest& bad,
                              base::StatusCode code) {
        auto attempt = program.rebuild(compiler, bad);
        expect(!attempt.ok() && attempt.status().code == code, "rejected status code");
        expect(!attempt.status().message.empty(), "rejection diagnostic");
        expect(cache.published(compiled)->id() == changedId, "published program retained");
        expect(program.published_fragment()->cacheKey == changedKey,
               "published compilation retained");
        return attempt.status().message;
    };
    auto bad = request;
    bad.fragment.defines.push_back({"EXTRA_INPUT", "1"});
    TRUFFLE_CHECK(rejected(bad, base::StatusCode::invalid_argument).find("location 1") !=
                  std::string::npos);
    bad = request;
    bad.vertex.source.text = R"(#version 450
layout(location=0) in vec4 position;
layout(location=0) flat out vec4 tint;
void main() { gl_Position = position; tint = vec4(1); }
)";
    TRUFFLE_CHECK(rejected(bad, base::StatusCode::invalid_argument).find("vertex input") !=
                  std::string::npos);
    bad = request;
    bad.fragment.stage = rhi::ShaderStage::vertex;
    (void)rejected(bad, base::StatusCode::invalid_argument);
    bad = request;
    bad.fragment.source.text = "#version 450\n#include \"color.glsl\"\n"
        "layout(location=0) flat in vec4 tint;\n"
        "layout(set=0,binding=0) uniform Tint { vec4 value; } extra;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color = shade() * tint * extra.value; }\n";
    TRUFFLE_CHECK(!program.rebuild(compiler, bad).ok());
    TRUFFLE_CHECK(cache.published(compiled)->id() == changedId);
    files["color.glsl"] = "#error deliberate include failure\n";
    TRUFFLE_CHECK(rejected(request, base::StatusCode::invalid_argument)
                      .find("deliberate include failure") != std::string::npos);

    files["color.glsl"] = "vec4 shade() { return vec4(COLOR, 0.5); }\n";
    auto recovered = program.rebuild(compiler, request);
    TRUFFLE_CHECK(recovered.ok() && recovered.value().published);
    TRUFFLE_CHECK(cache.published(compiled)->id() != changedId);
    TRUFFLE_CHECK(cache.published(prepared)->id() == preparedId);
    return 0;
}
