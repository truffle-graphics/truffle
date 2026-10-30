#include "renderer_native_test_utils.hpp"
#include "truffle/rhi/metal_backend.hpp"
#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace truffle;

std::string commonSource = R"MSL(#include <metal_stdlib>
using namespace metal;
#ifndef TRUFFLE_MSL_CONFIG
#error MSL configuration was not supplied
#endif
#if TRUFFLE_MSL_CONFIG != 1
#error MSL configuration mismatch
#endif
struct VertexInput {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
};
struct VertexOutput {
    float4 position [[position]];
    float2 uv [[user(locn0)]];
};
struct Parameters {
    float4 tint;
};
)MSL";

const std::string graphicsSource = R"MSL(vertex VertexOutput vertex_main(VertexInput input [[stage_in]]) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

fragment float4 fragment_main(
    VertexOutput input [[stage_in]],
    texture2d<float> image [[texture(0)]],
    sampler imageSampler [[sampler(0)]],
    constant Parameters& parameters [[buffer(16)]]) {
    return image.sample(imageSampler, input.uv) * parameters.tint;
}
)MSL";

const std::string computeSource = R"MSL(constant uint multiplier [[function_constant(7)]];
kernel void compute_main(
    device uint* output [[buffer(17)]],
    constant uint& pushData [[buffer(30)]],
    uint tid [[thread_position_in_grid]]) {
    output[tid] = (pushData + tid) * multiplier;
}
)MSL";

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void add_interface(rhi::ShaderReflection& reflection,
                   rhi::ShaderStage stage,
                   rhi::ShaderInterfaceDirection direction,
                   std::uint32_t location, std::string name,
                   std::string type, std::string interpolation = "smooth") {
    reflection.interfaces.push_back({
        stage, direction, location, std::move(name), std::move(type),
        std::move(interpolation)});
}

rhi::ShaderReflection vertex_interface() {
    rhi::ShaderReflection reflection;
    add_interface(reflection, rhi::ShaderStage::vertex,
                  rhi::ShaderInterfaceDirection::input, 0, "position", "vec2");
    add_interface(reflection, rhi::ShaderStage::vertex,
                  rhi::ShaderInterfaceDirection::input, 1, "uv", "vec2");
    add_interface(reflection, rhi::ShaderStage::vertex,
                  rhi::ShaderInterfaceDirection::output, 0, "uv", "vec2");
    return reflection;
}

rhi::ShaderReflection fragment_interface() {
    rhi::ShaderReflection reflection;
    add_interface(reflection, rhi::ShaderStage::fragment,
                  rhi::ShaderInterfaceDirection::input, 0, "uv", "vec2");
    add_interface(reflection, rhi::ShaderStage::fragment,
                  rhi::ShaderInterfaceDirection::output, 0, "color", "vec4");
    reflection.bindings = {
        {"image", rhi::ShaderStage::fragment, rhi::ResourceBindingType::texture,
         1, 5, 1, 0, true},
        {"imageSampler", rhi::ShaderStage::fragment,
         rhi::ResourceBindingType::sampler, 1, 6, 1, 0, true},
        {"parameters", rhi::ShaderStage::fragment,
         rhi::ResourceBindingType::buffer, 1, 7, 1, 16, true},
    };
    return reflection;
}

rhi::ShaderReflection compute_interface() {
    rhi::ShaderReflection reflection;
    reflection.bindings = {{
        "output", rhi::ShaderStage::compute, rhi::ResourceBindingType::buffer,
        0, 0, 1, sizeof(std::uint32_t), false}};
    reflection.pushConstants = {
        {rhi::ShaderStage::compute, 0, sizeof(std::uint32_t)}};
    reflection.specializationConstants = {
        {7, "multiplier", rhi::ShaderValueType::uint32, 2}};
    reflection.requiredWorkgroupSize = {1, 1, 1};
    return reflection;
}

shader::CompileRequest request_for(
    shader::Compiler& compiler, rhi::ShaderTarget target,
    rhi::ShaderStage stage, const std::string& root,
    rhi::ShaderReflection reflection) {
    shader::CompileRequest request;
    request.name = "native-msl-validation";
    request.source = {"root.metal", root};
    request.stage = stage;
    request.entryPoint = stage == rhi::ShaderStage::vertex ? "vertex_main" :
                         stage == rhi::ShaderStage::fragment ? "fragment_main" :
                                                              "compute_main";
    request.language = rhi::ShaderSourceLanguage::msl;
    request.target = target;
    request.sourceUnits = {"common.metal"};
    request.interfaceDeclaration = std::move(reflection);
    request.defines = {{"TRUFFLE_MSL_CONFIG", "1"}};
    request.resolver = [&](std::string_view requested, std::string_view,
                           bool) -> rhi::Result<shader::Source> {
        if (requested != "common.metal") {
            return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                        "unexpected source-unit request");
        }
        return shader::Source{"common.metal", commonSource};
    };
    (void)compiler;
    return request;
}

rhi::ShaderPackage combine(
    const shader::Compilation& vertex,
    const shader::Compilation& fragment) {
    auto desc = vertex.package.desc();
    const auto& fragmentDesc = fragment.package.desc();
    desc.variants.insert(desc.variants.end(), fragmentDesc.variants.begin(),
                         fragmentDesc.variants.end());
    for (const auto& source : fragmentDesc.sources) {
        if (std::find(desc.sources.begin(), desc.sources.end(), source) ==
            desc.sources.end()) {
            desc.sources.push_back(source);
        }
    }
    for (const auto& compiler : fragmentDesc.compilers) {
        if (std::find(desc.compilers.begin(), desc.compilers.end(), compiler) ==
            desc.compilers.end()) {
            desc.compilers.push_back(compiler);
        }
    }
    desc.remaps.insert(desc.remaps.end(), fragmentDesc.remaps.begin(),
                       fragmentDesc.remaps.end());
    return tests::require(rhi::ShaderPackage::create(
        std::move(desc), {}));
}

shader::Compilation compile(
    shader::Compiler& compiler, rhi::ShaderTarget target,
    rhi::ShaderStage stage, const std::string& root,
    rhi::ShaderReflection reflection) {
    auto request = request_for(compiler, target, stage, root,
                               std::move(reflection));
    auto result = compiler.compile(request);
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

void graphics(rhi::Device& device, shader::Compiler& compiler,
              rhi::ShaderTarget target) {
    auto vertex = compile(compiler, target, rhi::ShaderStage::vertex,
                         graphicsSource, vertex_interface());
    auto fragment = compile(compiler, target, rhi::ShaderStage::fragment,
                            graphicsSource, fragment_interface());
    const auto package = combine(vertex, fragment);
    auto loaded = tests::require(rhi::ShaderPackage::load(
        package.bytes(), {}));
    auto vertexShader = tests::require(device.create_shader(
        loaded, target, "vertex_main", rhi::ShaderStage::vertex,
        loaded.desc().variants[0].permutation));
    auto fragmentShader = tests::require(device.create_shader(
        loaded, target, "fragment_main", rhi::ShaderStage::fragment,
        loaded.desc().variants[1].permutation));

    auto textureLayout = tests::require(device.create_bind_group_layout({
        .group = 1,
        .entries = {
            {.binding = 5, .type = rhi::BindingType::sampled_texture,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 6, .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 7, .type = rhi::BindingType::uniform_buffer,
             .visibility = rhi::ShaderStageMask::fragment,
             .minimumBufferSize = 16},
        }}));
    auto pipelineLayout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&textureLayout}}));
    auto pipeline = tests::require(device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .layout = &pipelineLayout,
        .vertexBuffers = {{
            .stride = 16,
            .attributes = {
                {0, rhi::VertexFormat::float32x2, 0},
                {1, rhi::VertexFormat::float32x2, 8},
            },
        }},
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    }));

    const std::array<float, 12> vertices{
        -1, -1, .5F, .5F, 3, -1, .5F, .5F, -1, 3, .5F, .5F};
    auto vertexBuffer = tests::require(device.create_buffer({
        .size = sizeof(vertices), .usage = rhi::BufferUsage::vertex,
        .memory = rhi::MemoryDomain::upload}));
    tests::require(vertexBuffer.write(0, std::as_bytes(std::span{vertices})));
    const std::array<float, 4> tint{.5F, 1, 1, 1};
    auto uniformBuffer = tests::require(device.create_buffer({
        .size = sizeof(tint), .usage = rhi::BufferUsage::uniform,
        .memory = rhi::MemoryDomain::upload}));
    tests::require(uniformBuffer.write(0, std::as_bytes(std::span{tint})));
    auto image = tests::require(device.create_texture({
        .extent = {1, 1, 1}, .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled |
                 rhi::TextureUsage::copy_destination}));
    const std::array pixel{
        std::byte{128}, std::byte{64}, std::byte{32}, std::byte{255}};
    auto upload = tests::require(device.create_buffer({
        .size = pixel.size(), .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload}));
    tests::require(upload.write(0, pixel));
    auto copyPool = tests::require(
        device.create_command_pool(rhi::QueueKind::graphics));
    auto copyList = tests::require(copyPool.allocate());
    tests::require(copyList.begin());
    auto copy = tests::require(copyList.begin_copy());
    tests::require(copy.copy_buffer_to_texture(
        upload, image, {.texture = {.extent = {1, 1, 1}}}));
    tests::require(copy.end());
    tests::require(copyList.end());
    tests::submit_graphics(device, copyList);
    auto view = tests::require(device.create_texture_view(image));
    auto sampler = tests::require(device.create_sampler({}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &textureLayout,
        .arena = &arena,
        .entries = {
            {.binding = 5, .textureView = &view},
            {.binding = 6, .sampler = &sampler},
            {.binding = 7, .buffer = &uniformBuffer, .size = sizeof(tint)},
        }}));

    auto output = tests::make_color_target(device);
    auto renderPool = tests::require(
        device.create_command_pool(rhi::QueueKind::graphics));
    auto renderList = tests::require(renderPool.allocate());
    tests::require(renderList.begin());
    auto render = tests::require(renderList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{
            .texture = &output, .clear = {0, 0, 0, 1}}}}));
    tests::require(render.bind_pipeline(pipeline));
    tests::require(render.bind_group(1, group));
    tests::require(render.bind_vertex_buffer(0, vertexBuffer));
    tests::require(render.draw(3));
    tests::require(render.end());
    tests::require(renderList.end());
    tests::submit_graphics(device, renderList);
    tests::check_color(
        device, output,
        {std::byte{64}, std::byte{64}, std::byte{32}, std::byte{255}},
        target == rhi::ShaderTarget::metal_library
            ? "native MSL metallib graphics" : "native MSL source graphics");
}

void compute(rhi::Device& device, shader::Compiler& compiler,
             rhi::ShaderTarget target) {
    auto compiled = compile(compiler, target, rhi::ShaderStage::compute,
                            computeSource, compute_interface());
    auto package = tests::require(rhi::ShaderPackage::load(
        compiled.package.bytes(),
        {}));
    auto shaderObject = tests::require(device.create_shader(
        package, target, "compute_main", rhi::ShaderStage::compute,
        package.desc().variants[0].permutation));
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 0,
        .entries = {{
            .binding = 0, .type = rhi::BindingType::storage_buffer,
            .visibility = rhi::ShaderStageMask::compute,
            .minimumBufferSize = sizeof(std::uint32_t) * 4,
        }}}));
    auto layout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&groupLayout},
        .pushConstants = {{rhi::ShaderStage::compute, 0,
                           sizeof(std::uint32_t)}}}));
    auto output = tests::require(device.create_buffer({
        .size = sizeof(std::uint32_t) * 4,
        .usage = rhi::BufferUsage::storage,
        .memory = rhi::MemoryDomain::readback}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout,
        .arena = &arena,
        .entries = {{
            .binding = 0, .buffer = &output,
            .size = sizeof(std::uint32_t) * 4,
        }}}));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    const std::array<std::uint32_t, 1> pushData{5};
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    const auto execute = [&](std::uint32_t multiplier, bool overrideConstant) {
        rhi::ComputePipelineDesc pipelineDesc{
            .computeShader = &shaderObject, .layout = &layout};
        if (overrideConstant) {
            pipelineDesc.specializationConstants.push_back(
                {7, rhi::ShaderValueType::uint32, multiplier});
        }
        auto pipeline = tests::require(
            device.create_compute_pipeline(pipelineDesc));
        auto list = tests::require(pool.allocate());
        tests::require(list.begin());
        auto encoder = tests::require(list.begin_compute());
        tests::require(encoder.bind_pipeline(pipeline));
        tests::require(encoder.bind_group(0, group));
        tests::require(encoder.push_constants(
            0, std::as_bytes(std::span{pushData})));
        tests::require(encoder.dispatch(4, 1, 1));
        tests::require(encoder.end());
        tests::require(list.end());
        const std::array<rhi::CommandList*, 1> lists{&list};
        tests::require(queue.submit(lists));
        std::array<std::uint32_t, 4> values{};
        tests::require(
            output.read(0, std::as_writable_bytes(std::span{values})));
        const std::array<std::uint32_t, 4> expected{
            5 * multiplier, 6 * multiplier, 7 * multiplier, 8 * multiplier};
        check(values == expected,
              "Native MSL compute output must match specialization/push data");
    };
    execute(2, false);
    execute(3, true);
    std::cout << (target == rhi::ShaderTarget::metal_library
                      ? "native MSL metallib" : "native MSL source")
              << ": exact default/override compute 10,12,14,16 / 15,18,21,24\n";
}

void source_contract(shader::Compiler& compiler) {
    auto request = request_for(compiler, rhi::ShaderTarget::metal_source,
                               rhi::ShaderStage::vertex, graphicsSource,
                               vertex_interface());
    shader::RebuildState rebuild;
    auto first = rebuild.rebuild(compiler, request);
    if (!first.ok()) throw std::runtime_error(first.status().message);
    check(first.value().dependencies.size() == 2,
          "MSL composition must track source unit and root");
    const auto& packageCode =
        first.value().package.desc().variants.front().code;
    const std::string packagedSource(
        reinterpret_cast<const char*>(packageCode.data()), packageCode.size());
    check(packagedSource.starts_with("#define TRUFFLE_MSL_CONFIG 1\n"),
          "Native MSL package preserves compiler defines for runtime compilation");
    auto hit = rebuild.rebuild(compiler, request);
    check(hit.ok() && hit.value().cacheHit,
          "Unchanged MSL composition must hit its cache");
    auto fileRootRequest = request;
    fileRootRequest.resolver = {};
    fileRootRequest.includeRoots = {TRUFFLE_NATIVE_MSL_FIXTURE_DIR};
    auto fileRootCompilation = compiler.compile(fileRootRequest);
    check(fileRootCompilation.ok() &&
              fileRootCompilation.value().dependencies.size() == 2 &&
              fileRootCompilation.value().dependencies.front().identity.find(
                  "common.metal") != std::string::npos,
          "MSL source units resolve through the shared include-root mechanism");
    std::filesystem::create_directories(TRUFFLE_NATIVE_MSL_TEST_OUTPUT_DIR);
    const auto interfacePath = std::filesystem::path{
        TRUFFLE_NATIVE_MSL_TEST_OUTPUT_DIR} / "interface.tsp";
    std::ofstream interfaceFile{interfacePath, std::ios::binary};
    const auto interfaceBytes = fileRootCompilation.value().package.bytes();
    interfaceFile.write(
        reinterpret_cast<const char*>(interfaceBytes.data()),
        static_cast<std::streamsize>(interfaceBytes.size()));
    check(interfaceFile.good(),
          "Native MSL test must persist its typed interface package for shaderc");
    const auto write_variant_package = [&](bool differingReflection,
                                           const char* filename) {
        auto desc = fileRootCompilation.value().package.desc();
        auto second = desc.variants.front();
        second.permutation = "additional-interface-variant";
        if (differingReflection) {
            second.reflection.interfaces.front().type = "vec4";
        }
        desc.permutations.push_back({
            "additional-interface-variant", {}, {}});
        desc.variants.push_back(std::move(second));
        auto package = tests::require(rhi::ShaderPackage::create(
            std::move(desc),
            {}));
        const auto path =
            std::filesystem::path{TRUFFLE_NATIVE_MSL_TEST_OUTPUT_DIR} /
            filename;
        std::ofstream file{path, std::ios::binary};
        const auto bytes = package.bytes();
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        check(file.good(),
              "Native MSL test must persist its interface-variant package");
    };
    write_variant_package(false, "interface-agreeing.tsp");
    write_variant_package(true, "interface-differing.tsp");
    const auto previousKey = first.value().cacheKey;
    auto otherConfiguration = request;
    otherConfiguration.defines.push_back({"CACHE_ONLY", "7"});
    auto configurationChange = compiler.compile(otherConfiguration);
    check(configurationChange.ok() &&
              configurationChange.value().cacheKey != previousKey,
          "Native MSL defines must distinguish effective cache identity");
    commonSource += "\n// dependency edit\n";
    auto edited = rebuild.rebuild(compiler, request);
    check(edited.ok() && !edited.value().cacheHit &&
              edited.value().cacheKey != previousKey,
          "MSL source-unit edits must invalidate the cache");
    const auto editedKey = edited.value().cacheKey;
    commonSource.resize(commonSource.find("\n// dependency edit"));

    auto invalidRequest = request;
    invalidRequest.source.text = "not valid MSL";
    auto failed = rebuild.rebuild(compiler, invalidRequest);
    check(!failed.ok() && rebuild.last_good() &&
              rebuild.last_good()->cacheKey == editedKey,
          "Failed native compile must preserve last-good output");

    auto missingInterface = request;
    missingInterface.interfaceDeclaration.reset();
    check(!compiler.compile(missingInterface).ok(),
          "Native MSL requires typed interface metadata");
    auto mismatched = request;
    mismatched.interfaceDeclaration->interfaces.front().type = "vec4";
    const auto badInterface = compiler.compile(mismatched);
    check(!badInterface.ok() &&
              badInterface.status().code == rhi::StatusCode::invalid_argument,
          "Metal-observable vertex attribute mismatch must fail");
    auto unsupportedInclude = request;
    unsupportedInclude.source.text =
        "#include \"not-resolved.metal\"\n" + graphicsSource;
    check(!compiler.compile(unsupportedInclude).ok(),
          "Unresolved user includes must fail explicitly");
    auto commentedDirectives = request;
    commentedDirectives.source.text =
        "// #include \"unused.metal\"\n"
        "/* #import \"also-unused.metal\" */\n"
        "#include <metal_stdlib> // supported system header\n"
        "#/**/include <metal_stdlib>\n" + graphicsSource;
    check(compiler.compile(commentedDirectives).ok(),
          "MSL comments and built-in includes must not be misclassified");
    auto continuedInclude = request;
    continuedInclude.source.text =
        "#include \\\n\"not-resolved.metal\"\n" + graphicsSource;
    check(!compiler.compile(continuedInclude).ok(),
          "Continued user includes must fail explicitly");
    auto unsupportedImport = request;
    unsupportedImport.source.text =
        "#import \"not-resolved.metal\"\n" + graphicsSource;
    check(!compiler.compile(unsupportedImport).ok(),
          "Native MSL imports must fail explicitly");
    auto digraphInclude = request;
    digraphInclude.source.text =
        "%:include \"not-resolved.metal\"\n" + graphicsSource;
    check(!compiler.compile(digraphInclude).ok(),
          "MSL source guard must recognize the native %: directive marker");
    auto splicedIncludeName = request;
    splicedIncludeName.source.text =
        "#inc\\\nlude \"not-resolved.metal\"\n" + graphicsSource;
    check(!compiler.compile(splicedIncludeName).ok(),
          "MSL source guard must remove phase-two line splices before inspection");

    auto writablePush = request_for(
        compiler, rhi::ShaderTarget::metal_source,
        rhi::ShaderStage::compute,
        R"MSL(kernel void compute_main(
    device uint* output [[buffer(17)]],
    device uint* pushData [[buffer(30)]],
    uint tid [[thread_position_in_grid]]) {
    output[tid] = pushData[tid];
}
)MSL",
        compute_interface());
    writablePush.interfaceDeclaration->specializationConstants.clear();
    const auto writablePushResult = compiler.compile(writablePush);
    check(!writablePushResult.ok() &&
              writablePushResult.status().code ==
                  rhi::StatusCode::invalid_argument,
          "Writable Metal buffer(30) must not be declared as push constants");

    auto wrongEntry = request;
    wrongEntry.entryPoint = "missing_entry";
    check(!compiler.compile(wrongEntry).ok(),
          "Native MSL wrong entrypoint must fail");
    auto wrongStage = request;
    wrongStage.source.text = computeSource;
    wrongStage.entryPoint = "compute_main";
    const auto stageMismatch = compiler.compile(wrongStage);
    check(!stageMismatch.ok() &&
              stageMismatch.status().code == rhi::StatusCode::invalid_argument,
          "Native MSL entrypoint stage mismatch must fail");
    check(rebuild.last_good()->cacheKey == editedKey,
          "Invalid requests leave the successful rebuild state intact");
}

int run() {
    shader::Compiler compiler;
    const auto routes = shader::Compiler::routes();
    const auto sourceRoute = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::msl &&
                   route.target == rhi::ShaderTarget::metal_source;
        });
    check(sourceRoute != routes.end(),
          "Native MSL source route must be advertised when enabled");
    check(sourceRoute->stages.size() == 3 &&
              sourceRoute->vulkanTargets.empty(),
          "Native MSL route advertises all stages and no Vulkan targets");

    source_contract(compiler);
    auto instance = tests::require(
        rhi::create_metal_instance({.enableValidation = true}));
    auto adapter = tests::require(instance.adapter(0));
    std::cout << "native Metal adapter: " << adapter.info().name << '\n';
    auto device = tests::require(adapter.request_device());
    graphics(device, compiler, rhi::ShaderTarget::metal_source);
    compute(device, compiler, rhi::ShaderTarget::metal_source);

    const auto libraryRoute = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::msl &&
                   route.target == rhi::ShaderTarget::metal_library;
        });
    if (libraryRoute != routes.end()) {
        graphics(device, compiler, rhi::ShaderTarget::metal_library);
        compute(device, compiler, rhi::ShaderTarget::metal_library);

        const auto dependencyHeader =
            std::filesystem::path{TRUFFLE_NATIVE_MSL_TEST_OUTPUT_DIR} /
            "dependency-added-after-cache.metal";
        std::error_code removeError;
        std::filesystem::remove(dependencyHeader, removeError);
        check(!removeError,
              "Stale test-owned Metal dependency must be removable");
        struct RemoveHeader {
            const std::filesystem::path& path;
            ~RemoveHeader() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } removeHeader{dependencyHeader};
        const auto quotedHeader = "\"" + dependencyHeader.string() + "\"";
        std::string cachedDependencySource =
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "#if __has_include(";
        cachedDependencySource += quotedHeader;
        cachedDependencySource += R"MSL()
#inc\
lude )MSL";
        cachedDependencySource += quotedHeader;
        cachedDependencySource += R"MSL(
#define TRUFFLE_HAS_OPTIONAL_HEADER 1
#endif
kernel void compute_main(
    device uint* output [[buffer(17)]],
    uint tid [[thread_position_in_grid]]) {
#if defined(TRUFFLE_HAS_OPTIONAL_HEADER)
    output[tid] = optional_value();
#else
    output[tid] = tid;
#endif
}
)MSL";
        rhi::ShaderReflection dependencyReflection;
        dependencyReflection.bindings = {{
            "output", rhi::ShaderStage::compute,
            rhi::ResourceBindingType::buffer, 0, 0, 1,
            sizeof(std::uint32_t), false}};
        auto cachedDependencyRequest = request_for(
            compiler, rhi::ShaderTarget::metal_library,
            rhi::ShaderStage::compute, cachedDependencySource,
            std::move(dependencyReflection));
        cachedDependencyRequest.name = "native-msl-dependency-cache";
        cachedDependencyRequest.sourceUnits.clear();
        cachedDependencyRequest.resolver = {};
        cachedDependencyRequest.defines.clear();
        const auto initialDependencyCompile =
            compiler.compile(cachedDependencyRequest);
        check(initialDependencyCompile.ok() &&
                  !initialDependencyCompile.value().cacheHit,
              "Offline MSL without an optional header must compile and cache");
        {
            std::ofstream header{dependencyHeader, std::ios::binary};
            header << "inline uint optional_value() { return 7u; }\n";
            check(header.good(),
                  "Test-owned optional Metal header must be created");
        }
        const auto activeDependencyRebuild =
            compiler.compile(cachedDependencyRequest);
        check(!activeDependencyRebuild.ok() &&
                  activeDependencyRebuild.status().code ==
                      rhi::StatusCode::unsupported,
              "Native dependency scan must reject a newly active user header before cache lookup");

        rhi::ShaderReflection conditionalInterface;
        conditionalInterface.bindings = {{
            "output", rhi::ShaderStage::compute,
            rhi::ResourceBindingType::buffer, 0, 0, 1,
            sizeof(std::uint32_t), false}};
        auto conditionalTarget = request_for(
            compiler, rhi::ShaderTarget::metal_library,
            rhi::ShaderStage::compute,
            R"MSL(kernel void compute_main(
    device uint* output [[buffer(17)]],
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 260000
    device uint* targetOnly [[buffer(18)]],
#endif
    uint tid [[thread_position_in_grid]]) {
    output[tid] = tid;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 260000
    targetOnly[tid] = tid;
#endif
}
)MSL",
            std::move(conditionalInterface));
        conditionalTarget.name = "native-msl-target-conditional";
        const auto conditionalResult = compiler.compile(conditionalTarget);
        check(!conditionalResult.ok() &&
                  conditionalResult.status().code ==
                      rhi::StatusCode::invalid_argument,
              "Offline target reflection mismatch must not publish metadata");

        const auto concurrentCompile = [](std::string name) {
            shader::Compiler independentCompiler;
            auto request = request_for(
                independentCompiler, rhi::ShaderTarget::metal_library,
                rhi::ShaderStage::vertex, graphicsSource, vertex_interface());
            request.name = std::move(name);
            return independentCompiler.compile(request);
        };
        auto concurrentOne = std::async(
            std::launch::async, concurrentCompile, "native-msl-concurrent-one");
        auto concurrentTwo = std::async(
            std::launch::async, concurrentCompile, "native-msl-concurrent-two");
        check(concurrentOne.get().ok() && concurrentTwo.get().ok(),
              "Concurrent offline Metal compiler processes must complete");
    }
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
