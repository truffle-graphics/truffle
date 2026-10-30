#include "truffle/shader/compiler.hpp"
#include "../src/shader/digest.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace truffle;
    check(shader::detail::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty vector");
    check(shader::detail::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc vector");
    check(shader::detail::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 multi-block vector");
    shader::Compiler compiler;
    shader::CompileRequest request;
    request.source = {"main.vert", R"(#version 450
#include "module.glsl"
#include "shared.glsl"
layout(location=0) in vec3 position;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform Params { vec4 tint; } params;
void main() { gl_Position = vec4(position * SCALE, 1); color = shade(params.tint); }
)"};
    if (!shader::Compiler::available()) {
        check(!compiler.compile(request).ok(), "Disabled adapter reports failure");
        return 0;
    }
    std::map<std::string, std::string> files{
        {"module.glsl", "#include \"shared.glsl\"\nvec4 shade(vec4 v) { return v * FACTOR; }\n"},
        {"shared.glsl", "#ifndef SHARED\n#define SHARED\n#define FACTOR 1.0\n#endif\n"}};
    request.resolver = [&](std::string_view name, std::string_view, bool) -> rhi::Result<shader::Source> {
        auto it = files.find(std::string{name});
        if (it == files.end()) return rhi::Status::failure(rhi::StatusCode::invalid_argument, "Missing " + std::string{name});
        return shader::Source{it->first, it->second};
    };
    request.defines = {{"SCALE", "2.0"}};
    auto first = compiler.compile(request);
    if (!first.ok()) std::cerr << first.status().message << '\n';
    check(first.ok(), "Compile modular configured GLSL");
    check(!first.value().cacheHit && first.value().dependencies.size() == 3, "Transitive/reused include identity");
    const auto& variant = first.value().package.desc().variants.front();
    check(variant.reflection.bindings.size() == 1, "Reflect uniform buffer");
    check(variant.reflection.bindings[0].binding == 1 && variant.reflection.bindings[0].minimumSize == 16 &&
          variant.reflection.bindings[0].readOnly, "Reflect uniform layout");
    check(variant.reflection.interfaces.size() == 1 && first.value().interfaces.size() == 2, "Reflect both IO directions and TSP pipeline edge");
    check(rhi::ShaderPackage::load(first.value().package.bytes()).ok(), "Valid in-memory TSP");
    auto second = compiler.compile(request);
    check(second.ok() && second.value().cacheHit, "Repeat cache hit");
    check(second.value().cacheKey == first.value().cacheKey, "Deterministic key");
    check(first.value().compiler.name == "glslang" &&
          !first.value().compiler.version.empty() &&
          first.value().compiler.revision ==
              "a8d28bd082bff18ffbe80996e922b012f915cf07;truffle-glslang=2;vulkan=1.1;spirv=1.3",
          "Pinned compiler identity accompanies result and cache");
    request.defines[0].value = "3.0";
    auto configured = compiler.compile(request);
    check(configured.ok() && !configured.value().cacheHit && configured.value().cacheKey != first.value().cacheKey, "Configuration invalidation");
    files["shared.glsl"] += "// dependency edit\n";
    auto edited = compiler.compile(request);
    check(edited.ok() && !edited.value().cacheHit && edited.value().cacheKey != configured.value().cacheKey, "Transitive dependency invalidation including comments");
    shader::RebuildState reload;
    auto good = reload.rebuild(compiler, request);
    check(good.ok(), "Initial rebuild");
    const auto lastKey = reload.last_good()->cacheKey;
    files["shared.glsl"] = "#error deliberate failure\n";
    auto failed = reload.rebuild(compiler, request);
    check(!failed.ok() && failed.status().message.find("deliberate failure") != std::string::npos, "Failed rebuild explicit diagnostics");
    check(reload.last_good() && reload.last_good()->cacheKey == lastKey, "Last-good retained separately");
    files.erase("shared.glsl");
    check(!compiler.compile(request).ok(), "Missing dependency rejects stale cache");
    files["shared.glsl"] = "#include \"module.glsl\"\n";
    auto cycle = compiler.compile(request);
    check(!cycle.ok() && cycle.status().message.find("Cyclic") != std::string::npos, "Cyclic dependency diagnostics");
    files["shared.glsl"] = "#define FACTOR 4.0\n";
    auto recovered = reload.rebuild(compiler, request);
    check(recovered.ok() && reload.last_good()->cacheKey != lastKey, "Rebuild recovery");
    request.generateDebugInfo = true;
    auto debug = compiler.compile(request);
    check(debug.ok() && !debug.value().cacheHit, "Compiler option invalidation");
    request.target = rhi::ShaderTarget::wgsl;
    check(!compiler.compile(request).ok(), "Unsupported target must not hit cache");
    request.target = rhi::ShaderTarget::spirv;
    request.defines.push_back({"SCALE", "4"});
    check(!compiler.compile(request).ok(), "Duplicate defines rejected");
    request.defines.clear();
    request.source = {"work.comp", R"(#version 450
layout(local_size_x=4,local_size_y=2) in;
layout(set=1,binding=2,std430) buffer Data { uint value; } data;
void main() { data.value = gl_GlobalInvocationID.x; }
)"};
    request.stage = rhi::ShaderStage::compute;
    auto compute = compiler.compile(request);
    if (!compute.ok()) std::cerr << compute.status().message << '\n';
    check(compute.ok(), "Compute reflection");
    const auto& computeReflection = compute.value().package.desc().variants[0].reflection;
    check(computeReflection.requiredWorkgroupSize == rhi::Extent3D{4,2,1}, "Workgroup dimensions");
    check(computeReflection.bindings[0].group == 1 && !computeReflection.bindings[0].readOnly, "Storage access");
    request.source.text = R"(#version 450
layout(local_size_x=1) in;
layout(set=0,binding=0) buffer Data { uint values[]; } data;
void main() { data.values[gl_GlobalInvocationID.x]=1; }
)";
    auto runtimeArray = compiler.compile(request);
    if (!runtimeArray.ok()) std::cerr << runtimeArray.status().message << '\n';
    check(runtimeArray.ok(), "Runtime-sized storage buffer tail");
    check(runtimeArray.value().package.desc().variants[0].reflection.bindings[0].minimumSize == 4,
          "Runtime-sized buffer requires at least one element");
    request.source.text = R"(#version 450
layout(local_size_x=1) in;
layout(constant_id=0) const uint N=2;
layout(set=0,binding=0) buffer Data { uint value; } data;
void main() { data.value=N; }
)";
    check(!compiler.compile(request).ok(), "Unsupported specialization rejected");
    request.source.text = R"(#version 450
layout(local_size_x=1) in;
layout(push_constant) uniform Push { uint value; } pc;
layout(set=0,binding=0) buffer Data { uint value; } data;
void main() { data.value=pc.value; }
)";
    check(!compiler.compile(request).ok(), "Unsupported push constant rejected");
    request.source.text = R"(#version 450
layout(local_size_x=1) in;
layout(push_constant) uniform Push { uint value; } unusedPush;
void main() {}
)";
    check(!compiler.compile(request).ok(), "Inactive unsupported declarations cannot hide from reflection");
    request.stage = rhi::ShaderStage::fragment;
    request.source = {"resource.frag", R"(#version 450
layout(set=0,binding=1) uniform texture2D tex[2];
layout(set=0,binding=2) uniform sampler sampleState;
layout(location=0) out vec4 color;
void main() { color=texture(sampler2D(tex[1],sampleState),vec2(0)); }
)"};
    auto resources = compiler.compile(request);
    if (!resources.ok()) std::cerr << resources.status().message << '\n';
    check(resources.ok() && resources.value().package.desc().variants[0].reflection.bindings.size() == 2, "Separate sampled images/samplers");
    request.source.text = R"(#version 450
layout(set=0,binding=0) uniform sampler2D tex;
layout(location=0) out vec4 color;
void main() { color=texture(tex,vec2(0)); }
)";
    check(!compiler.compile(request).ok(), "Combined sampler explicitly rejected");
    request.source.text = R"(#version 450
layout(location=0,index=1) out vec4 color;
void main() { color=vec4(1); }
)";
    check(!compiler.compile(request).ok(), "Dual-source indexed IO explicitly rejected");
    request.stage = rhi::ShaderStage::compute;
    request.language = rhi::ShaderSourceLanguage::glsl_es;
    request.source = {"work.es", "#version 310 es\nlayout(local_size_x=2) in;\nvoid main() {}\n"};
    check(compiler.compile(request).ok(), "GLSL ES route");
    request.source.text = "#version 310 es\n#if ENABLED\nlayout(local_size_x=2) in;\n#else\n#error disabled\n#endif\nvoid main() {}\n";
    check(!compiler.compile(request).ok(), "Conditional code negative");
    request.defines = {{"ENABLED","1"}};
    check(compiler.compile(request).ok(), "Conditional code configuration");
    compiler.clear_cache();
    check(!compiler.compile(request).value().cacheHit, "Explicit cache clearing");
    const auto concurrent = [request] {
        shader::Compiler other;
        check(other.compile(request).ok(), "Concurrent compiler instances");
    };
    std::thread a{concurrent}, b{concurrent};
    a.join();
    b.join();
    shader::CompileRequest warning;
    warning.source = {"warning.vert",
        "#version 450\n#extension GL_TRUFFLE_missing_extension : warn\n"
        "void main(){ gl_Position=vec4(0); }\n"};
    auto warned = compiler.compile(warning);
    check(warned.ok() && !warned.value().diagnostics.empty() &&
          !warned.value().package.desc().diagnostics.empty(),
          "Successful compiler diagnostics retained in result and TSP");
    warning.source = {"many-warnings.vert", "#version 450\n"};
    for (int i = 0; i < 100; ++i) {
        warning.source.text += "#extension GL_TRUFFLE_missing_extension_" +
                               std::to_string(i) + " : warn\n";
    }
    warning.source.text += "void main(){ gl_Position=vec4(0); }\n";
    shader::RebuildState noisyReload;
    auto noisy = noisyReload.rebuild(compiler, warning);
    if (!noisy.ok()) std::cerr << noisy.status().message << '\n';
    check(noisy.ok(), "Many compiler warnings must not fail successful compilation/rebuild");
    check(noisy.value().package.desc().diagnostics.size() == 1 &&
          rhi::ShaderPackage::load(noisy.value().package.bytes()).ok(),
          "Warning-heavy result creates a loadable package with one diagnostic");
    const auto& fullLog = noisy.value().diagnostics;
    const auto& packagedLog = noisy.value().package.desc().diagnostics.front().message;
    check(fullLog.size() > 4096 &&
          fullLog.find("GL_TRUFFLE_missing_extension_99") != std::string::npos,
          "Compilation retains warnings beyond the package diagnostic limit");
    check(packagedLog.size() == 4096 && packagedLog == fullLog.substr(0, 4096),
          "Only the packaged diagnostic is truncated to the TSP limit");
    check(noisyReload.last_good() && noisyReload.last_good()->diagnostics == fullLog,
          "Warning-heavy successful rebuild publishes its last-good result");
    auto noisyHit = noisyReload.rebuild(compiler, warning);
    check(noisyHit.ok() && noisyHit.value().cacheHit &&
          noisyHit.value().diagnostics == fullLog,
          "Warning-heavy successful compilation enters the cache");
}
