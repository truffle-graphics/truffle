#include "truffle/renderer/source_program.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace truffle::renderer {
namespace {

base::Status mismatch(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Renderer: " + std::move(message));
}

// Validate the package's canonical directional IO before publication.
base::Status validate_interfaces(const shader::Compilation& vertex,
                                 const shader::Compilation& fragment,
                                 const rhi::PipelineDesc& state) {
    for (const auto& input : fragment.interfaces()) {
        if (input.direction != rhi::ShaderInterfaceDirection::input) {
            continue;
        }
        const auto matched = std::any_of(
            vertex.interfaces().begin(), vertex.interfaces().end(), [&](const auto& output) {
                return output.direction == rhi::ShaderInterfaceDirection::output &&
                       output.location == input.location && output.type == input.type;
            });
        if (!matched) {
            return mismatch("fragment input location " +
                            std::to_string(input.location) + " (" +
                            input.type + ") has no matching vertex output");
        }
    }
    for (const auto& input : vertex.interfaces()) {
        if (input.direction != rhi::ShaderInterfaceDirection::input) {
            continue;
        }
        const auto matched = std::any_of(
            state.vertexBuffers.begin(), state.vertexBuffers.end(), [&](const auto& buffer) {
                return std::any_of(buffer.attributes.begin(), buffer.attributes.end(),
                                   [&](const auto& attribute) {
                                       return attribute.location == input.location;
                                   });
            });
        if (!matched) {
            return mismatch("vertex input location " +
                            std::to_string(input.location) +
                            " has no pipeline vertex attribute");
        }
    }
    return base::Status::success();
}

ShaderSelection select(const shader::Compilation& compilation) {
    const auto& variant = compilation.package.desc().variants.front();
    return {&compilation.package, variant.target, variant.entryPoint, variant.stage,
            variant.permutation};
}

} // namespace

struct SourceProgram::Published {
    shader::Compilation vertex;
    shader::Compilation fragment;
    rhi::ObjectId pipeline{};
};

SourceProgram::SourceProgram(PipelineCache& programs, MaterialId material,
                             rhi::PipelineDesc state, std::size_t variantHash)
    : programs_(&programs), material_(material), variantHash_(variantHash),
      state_(std::move(state)) {}

SourceProgram::SourceProgram(SourceProgram&&) noexcept = default;
SourceProgram& SourceProgram::operator=(SourceProgram&&) noexcept = default;
SourceProgram::~SourceProgram() = default;

base::Result<SourceProgramUpdate> SourceProgram::rebuild(
    shader::Compiler& compiler, const SourceProgramRequest& request) {
    if (request.vertex.stage != rhi::ShaderStage::vertex ||
        request.fragment.stage != rhi::ShaderStage::fragment) {
        return mismatch("source program requires vertex and fragment requests");
    }
    auto vertex = compiler.compile(request.vertex);
    if (!vertex.ok()) {
        return vertex.status();
    }
    auto fragment = compiler.compile(request.fragment);
    if (!fragment.ok()) {
        return fragment.status();
    }
    SourceProgramUpdate update{.vertexCacheHit = vertex.value().cacheHit,
                               .fragmentCacheHit = fragment.value().cacheHit};
    if (published_ && vertex.value().cacheKey == published_->vertex.cacheKey &&
        fragment.value().cacheKey == published_->fragment.cacheKey) {
        // The slot may have been invalidated or replaced through the cache directly.
        const auto* current = programs_->published(material_, variantHash_);
        if (current != nullptr && current->id() == published_->pipeline) {
            return update;
        }
    }
    if (auto status = validate_interfaces(vertex.value(), fragment.value(), state_);
        !status.ok()) {
        return status;
    }
    ProgramDesc desc;
    desc.vertex = select(vertex.value());
    desc.fragment = select(fragment.value());
    desc.pipeline = state_;
    auto candidate = programs_->stage_program(desc);
    if (!candidate.ok()) {
        return candidate.status();
    }
    std::unique_ptr<Published> next;
    try {
        // Allocate before commit so publication and published() cannot diverge.
        next = std::make_unique<Published>(
            Published{std::move(vertex).value(), std::move(fragment).value()});
    } catch (const std::bad_alloc&) {
        return base::Status::failure(base::StatusCode::out_of_memory,
                                     "Renderer: source program allocation failed");
    }
    if (auto status = programs_->commit_program(material_, std::move(candidate).value(),
                                                variantHash_);
        !status.ok()) {
        return status;
    }
    next->pipeline = programs_->published(material_, variantHash_)->id();
    published_ = std::move(next);
    update.published = true;
    return update;
}

const shader::Compilation* SourceProgram::published_vertex() const noexcept {
    return published_ ? &published_->vertex : nullptr;
}

const shader::Compilation* SourceProgram::published_fragment() const noexcept {
    return published_ ? &published_->fragment : nullptr;
}

} // namespace truffle::renderer
