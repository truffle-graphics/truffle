#include "truffle/shader/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace truffle::shader {

#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
rhi::Result<Compilation> compile_glslang(const CompileRequest& request,
                                       std::vector<Compilation>& cache);
#endif

struct Compiler::Impl {
    std::vector<Compilation> cache;
};

Compiler::Compiler() : impl_(std::make_unique<Impl>()) {}
Compiler::~Compiler() = default;

bool Compiler::available() noexcept {
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
    return true;
#else
    return false;
#endif
}

rhi::Result<Compilation> Compiler::compile(const CompileRequest& request) {
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
    return compile_glslang(request, impl_->cache);
#else
    (void)request;
    return rhi::Status::failure(rhi::StatusCode::unsupported,
                               "Shader compiler disabled: enable TRUFFLE_SHADER_ENABLE_GLSLANG");
#endif
}

void Compiler::clear_cache() { impl_->cache.clear(); }

rhi::Result<Compilation> RebuildState::rebuild(
    Compiler& compiler, const CompileRequest& request) {
    auto result = compiler.compile(request);
    if (result.ok()) {
        lastGood_ = std::make_unique<Compilation>(result.value());
    }
    return result;
}

const Compilation* RebuildState::last_good() const noexcept {
    return lastGood_.get();
}

rhi::Result<Source> read_source(std::string_view path) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(
        std::filesystem::path{path}, error);
    if (error) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Cannot resolve shader source: " + std::string{path});
    }
    std::ifstream stream{canonical, std::ios::binary};
    if (!stream) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Cannot read shader source: " + canonical.string());
    }
    std::string text{std::istreambuf_iterator<char>{stream}, {}};
    if (stream.bad()) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Shader source read failed: " + canonical.string());
    }
    return Source{canonical.generic_string(), std::move(text)};
}

} // namespace truffle::shader
