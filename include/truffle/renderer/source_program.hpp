#pragma once

#include "truffle/renderer/pipeline_cache.hpp"
#include "truffle/shader/compiler.hpp"

#include <cstddef>
#include <memory>

namespace truffle::renderer {

// Requests are compiled exactly as given: only their configured defines are built.
struct SourceProgramRequest {
    shader::CompileRequest vertex{.stage = rhi::ShaderStage::vertex};
    shader::CompileRequest fragment{.stage = rhi::ShaderStage::fragment};
};

struct SourceProgramUpdate {
    // False when both compiled stages equal the published program, which stays in use.
    bool published = false;
    bool vertexCacheHit = false;
    bool fragmentCacheHit = false;
};

// One (material, variantHash) slot whose program is compiled from source through
// truffle::shader and adopted via PipelineCache staging/commit. The host calls
// rebuild() explicitly (for example when polling); there is no watcher or thread.
// Static state shader pointers must be null; its layout/cache pointers are
// borrowed for this object's lifetime because every replacement stages with them.
// Single-thread-owned with the cache; the cache and compiler outlive rebuild().
class SourceProgram {
public:
    SourceProgram(PipelineCache& programs, MaterialId material,
                  rhi::PipelineDesc state, std::size_t variantHash = 0);
    SourceProgram(const SourceProgram&) = delete;
    SourceProgram& operator=(const SourceProgram&) = delete;
    SourceProgram(SourceProgram&&) noexcept;
    SourceProgram& operator=(SourceProgram&&) noexcept;
    ~SourceProgram();

    // Re-resolves sources, compiles, validates the vertex/fragment interface and
    // vertex attributes, stages a native pipeline and commits it. Any failure
    // (including `unsupported` without a compiler adapter) leaves the published
    // program and published() unchanged.
    [[nodiscard]] base::Result<SourceProgramUpdate> rebuild(
        shader::Compiler& compiler, const SourceProgramRequest& request);

    // Compilations of the currently published program, or null before the first
    // successful publication. Independent of failed attempts.
    [[nodiscard]] const shader::Compilation* published_vertex() const noexcept;
    [[nodiscard]] const shader::Compilation* published_fragment() const noexcept;

private:
    struct Published;
    PipelineCache* programs_;
    MaterialId material_;
    std::size_t variantHash_;
    rhi::PipelineDesc state_;
    std::unique_ptr<Published> published_;
};

} // namespace truffle::renderer
