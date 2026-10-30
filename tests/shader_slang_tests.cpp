#include "shader_test_utils.hpp"
#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace {
using namespace truffle;

shader::CompileRequest fixture() {
    shader::CompileRequest request;
    request.name = "slang-modules";
    request.source = tests::require(shader::read_source(TRUFFLE_SLANG_FIXTURES "/main.slang"));
    request.language = rhi::ShaderSourceLanguage::slang;
    request.stage = rhi::ShaderStage::compute;
    request.entryPoint = "computeMain";
    request.defines = {{"QUALITY", "3"}};
    request.includeRoots = {TRUFFLE_SLANG_FIXTURES};
    return request;
}

void check_single_entry(const shader::Compilation& result, std::string_view entry) {
    TRUFFLE_SHADER_CHECK(result.package.desc().variants.size() == 1);
    const auto& variant = result.package.desc().variants[0];
    TRUFFLE_SHADER_CHECK(variant.entryPoint == entry);
    std::vector<uint32_t> words(variant.code.size() / sizeof(uint32_t));
    std::memcpy(words.data(), variant.code.data(), variant.code.size());
    TRUFFLE_SHADER_CHECK(words.size() >= 5 && words[1] == 0x00010300u);
    size_t entries = 0;
    for (size_t i = 5; i < words.size();) {
        const auto count = words[i] >> 16;
        TRUFFLE_SHADER_CHECK(count > 0 && i + count <= words.size());
        if ((words[i] & 0xffffu) == 15) ++entries;
        i += count;
    }
    TRUFFLE_SHADER_CHECK(entries == 1);
    tests::require(rhi::ShaderPackage::load(result.package.bytes()));
}

void run() {
    shader::Compiler compiler;
    auto request = fixture();
    const auto route = std::find_if(shader::Compiler::routes().begin(), shader::Compiler::routes().end(),
        [](const auto& value) { return value.language == rhi::ShaderSourceLanguage::slang; });
    TRUFFLE_SHADER_CHECK(route != shader::Compiler::routes().end());
    TRUFFLE_SHADER_CHECK(route->stages.size() == 3 && route->vulkanTargets.size() == 1);
    std::map<std::string, std::string> files;
    size_t unrequestedReads = 0;
    for (const auto* name : {"transform.slang", "values.slang"})
        files.emplace(name, tests::require(shader::read_source(
            std::string{TRUFFLE_SLANG_FIXTURES} + "/" + name)).text);
    request.source.identity = "virtual/main.slang";
    request.includeRoots = {"virtual"};
    request.resolver = [&](std::string_view name, std::string_view, bool) -> rhi::Result<shader::Source> {
        if (std::filesystem::path{name}.filename() == "unrequested.slang") ++unrequestedReads;
        const auto found = files.find(std::filesystem::path{name}.filename().string());
        if (found == files.end())
            return rhi::Status::failure(rhi::StatusCode::invalid_argument, "Missing source: " + std::string{name});
        return shader::Source{"virtual/" + found->first, found->second};
    };
    auto first = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(!first.cacheHit && first.dependencies.size() == 3);
    TRUFFLE_SHADER_CHECK(first.compiler.name == "slang" && first.compiler.version == "2026.12.1");
    TRUFFLE_SHADER_CHECK(first.diagnostics.empty());
    const auto& reflection = first.package.desc().variants[0].reflection;
    TRUFFLE_SHADER_CHECK(reflection.requiredWorkgroupSize == rhi::Extent3D(4, 1, 1));
    TRUFFLE_SHADER_CHECK(reflection.bindings.size() == 1);
    TRUFFLE_SHADER_CHECK(reflection.bindings[0].type == rhi::ResourceBindingType::buffer &&
        reflection.bindings[0].group == 0 && reflection.bindings[0].binding == 0 &&
        !reflection.bindings[0].readOnly && reflection.bindings[0].minimumSize == 4);
    check_single_entry(first, "computeMain");
    TRUFFLE_SHADER_CHECK(tests::require(compiler.compile(request)).cacheHit);
    auto remapped = request;
    remapped.resolver = [resolve = request.resolver](std::string_view name, std::string_view parent,
                                                    bool system) -> rhi::Result<shader::Source> {
        auto source = resolve(name, parent, system);
        if (!source.ok()) return source.status();
        auto result = std::move(source).value();
        result.identity = "remapped/" + result.identity;
        return result;
    };
    const auto identities = tests::require(compiler.compile(remapped));
    TRUFFLE_SHADER_CHECK(!identities.cacheHit && identities.cacheKey != first.cacheKey);
    files["values.slang"] = "#include \"constants.h\"\n" + files["values.slang"];
    files["constants.h"] = "#define INCLUDED_BIAS 0\n";
    const auto included = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(included.dependencies.size() == 4 && included.cacheKey != first.cacheKey);
    files["constants.h"] += "// include content edit\n";
    const auto includeEdited = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(!includeEdited.cacheHit && includeEdited.cacheKey != included.cacheKey);
    files["values.slang"] += "\n// transitive dependency edit\n";
    auto edited = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(!edited.cacheHit && edited.cacheKey != first.cacheKey);
    request.defines[0].value = "4";
    auto configured = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(!configured.cacheHit && configured.cacheKey != edited.cacheKey);
    TRUFFLE_SHADER_CHECK(configured.package.desc().permutations.size() == 1);
    request.defines.push_back({"UNUSED", "1"});
    auto twoDefines = tests::require(compiler.compile(request));
    std::reverse(request.defines.begin(), request.defines.end());
    TRUFFLE_SHADER_CHECK(tests::require(compiler.compile(request)).cacheKey == twoDefines.cacheKey);
    shader::RebuildState rebuild;
    auto good = tests::require(rebuild.rebuild(compiler, request));
    files["values.slang"] = "#error deliberate-import-failure\n";
    const auto failed = rebuild.rebuild(compiler, request);
    TRUFFLE_SHADER_CHECK(!failed.ok() && failed.status().message.find("deliberate-import-failure") != std::string::npos);
    TRUFFLE_SHADER_CHECK(rebuild.last_good() && rebuild.last_good()->cacheKey == good.cacheKey);
    files.erase("values.slang");
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    files["values.slang"] = "import transform;\npublic uint value(uint x) { return x; }\n";
    const auto cycle = compiler.compile(request);
    TRUFFLE_SHADER_CHECK(!cycle.ok() &&
        cycle.status().message.find("recursive module import") != std::string::npos);
    files["values.slang"] = "public uint value(uint x) { return QUALITY * 100 + x + 1; }\n";
    auto recovered = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(recovered.cacheKey != good.cacheKey);
    request.generateDebugInfo = true;
    const auto debug = tests::require(compiler.compile(request));
    TRUFFLE_SHADER_CHECK(!debug.cacheHit && debug.cacheKey != recovered.cacheKey && debug.diagnostics.empty());
    request.generateDebugInfo = false;
    for (const auto& [stage, entry] : std::vector<std::pair<rhi::ShaderStage, std::string>>{
            {rhi::ShaderStage::vertex, "vertexMain"}, {rhi::ShaderStage::fragment, "fragmentMain"}}) {
        request.stage = stage;
        request.entryPoint = entry;
        check_single_entry(tests::require(compiler.compile(request)), entry);
    }
    request.stage = rhi::ShaderStage::compute;
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok()); // fragmentMain is not compute.
    request.entryPoint = "absentMain";
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.entryPoint = "computeMain";
    request.sourceUnits = {"ignored.slang"};
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.sourceUnits.clear();
    request.defines.push_back(request.defines.front());
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    auto configuredTarget = fixture();
    configuredTarget.vulkanTarget = {shader::VulkanVersion::v1_0, shader::SpirvVersion::v1_0};
    TRUFFLE_SHADER_CHECK(!compiler.compile(configuredTarget).ok());
    configuredTarget = fixture();
    configuredTarget.target = rhi::ShaderTarget::metal_source;
    TRUFFLE_SHADER_CHECK(!compiler.compile(configuredTarget).ok());
    auto drawParameters = fixture();
    drawParameters.source = {"unsupported.slang",
        "float4 usesBaseVertex(uint id : SV_VertexID) : SV_Position { return float4(float(id),0,0,1); }"};
    drawParameters.stage = rhi::ShaderStage::vertex;
    drawParameters.entryPoint = "usesBaseVertex";
    const auto unsupportedFeature = compiler.compile(drawParameters);
    TRUFFLE_SHADER_CHECK(!unsupportedFeature.ok() &&
        unsupportedFeature.status().code == rhi::StatusCode::unsupported &&
        unsupportedFeature.status().message.find("DrawParameters") != std::string::npos);
    compiler.clear_cache();
    TRUFFLE_SHADER_CHECK(!tests::require(compiler.compile(fixture())).cacheHit);
    auto arbitraryIdentity = fixture();
    arbitraryIdentity.source.identity = "virtual/arbitrary.vert";
    arbitraryIdentity.includeRoots = {"virtual"};
    arbitraryIdentity.resolver = request.resolver;
    const auto arbitrary = tests::require(compiler.compile(arbitraryIdentity));
    TRUFFLE_SHADER_CHECK(std::any_of(arbitrary.dependencies.begin(), arbitrary.dependencies.end(),
        [](const auto& value) { return value.identity == "virtual/arbitrary.vert"; }));
    auto conditional = fixture();
    conditional.source = {"virtual/configuration.slang", R"(
#if QUALITY == 3
import transform;
#else
import unrequested;
#endif
[shader("compute")][numthreads(1,1,1)] void chosen() {}
)"};
    conditional.entryPoint = "chosen";
    conditional.includeRoots = {"virtual"};
    conditional.resolver = request.resolver;
    files["unrequested.slang"] = "#error deliberate-unrequested-module\n";
    tests::require(compiler.compile(conditional));
    TRUFFLE_SHADER_CHECK(unrequestedReads == 0);
    conditional.defines = {{"QUALITY", "4"}};
    const auto unrequested = compiler.compile(conditional);
    TRUFFLE_SHADER_CHECK(!unrequested.ok() && unrequestedReads > 0 &&
        unrequested.status().message.find("deliberate-unrequested-module") != std::string::npos);
    std::thread concurrent{[] {
        shader::Compiler other;
        tests::require(other.compile(fixture()));
    }};
    tests::require(compiler.compile(fixture()));
    concurrent.join();
    std::cout << "Slang native imports/configuration/cache/rebuild, single entry, all stages passed\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            std::ifstream input{argv[1], std::ios::binary};
            const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
            const auto result = tests::require(truffle::shader::Compiler{}.compile(fixture()));
            TRUFFLE_SHADER_CHECK(bytes.size() == result.package.bytes().size() &&
                std::memcmp(bytes.data(), result.package.bytes().data(), bytes.size()) == 0);
            return 0;
        }
        run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
