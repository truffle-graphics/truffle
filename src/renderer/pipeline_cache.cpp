#include "truffle/renderer/pipeline_cache.hpp"
#include "truffle/renderer/data_layout.hpp"

#include <algorithm>
#include <new>
#include <tuple>
#include <utility>

namespace truffle::renderer {
namespace {

struct AttachmentState {
    std::vector<rhi::TextureFormat> colors;
    rhi::TextureFormat depth = rhi::TextureFormat::unknown;
    std::uint32_t samples = 1;
};

base::Result<AttachmentState> attachments(const rhi::RenderPassDesc& pass) {
    AttachmentState state;
    bool samplesSet = false;
    const auto add = [&](rhi::Texture* texture) -> base::Status {
        if (texture == nullptr || !texture->valid()) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "Renderer: invalid attachment");
        }
        const auto desc = texture->desc();
        if (samplesSet && state.samples != desc.sampleCount) {
            return base::Status::failure(
                base::StatusCode::invalid_argument,
                "Renderer: attachment sample counts do not match");
        }
        state.samples = desc.sampleCount;
        samplesSet = true;
        return base::Status::success();
    };
    for (const auto& color : pass.colorAttachments) {
        if (auto status = add(color.texture); !status.ok()) {
            return status;
        }
        state.colors.push_back(color.texture->desc().format);
    }
    if (pass.depthStencilAttachment.texture != nullptr) {
        if (auto status = add(pass.depthStencilAttachment.texture); !status.ok()) {
            return status;
        }
        state.depth = pass.depthStencilAttachment.texture->desc().format;
    }
    return state;
}

bool same_bindings(const rhi::Pipeline& first, const rhi::Pipeline& second) {
    const auto signature = [](const rhi::ResourceBinding& binding) {
        return std::tuple{binding.group, binding.binding, binding.stage,
                          binding.type, binding.arrayCount, binding.minimumSize,
                          binding.readOnly};
    };
    const auto a = first.reflection().bindings();
    const auto b = second.reflection().bindings();
    return a.size() == b.size() &&
           std::all_of(a.begin(), a.end(), [&](const auto& binding) {
               return std::any_of(b.begin(), b.end(), [&](const auto& candidate) {
                   return signature(binding) == signature(candidate);
               });
           });
}

bool same_vertex_layouts(
    const std::vector<rhi::VertexBufferLayoutDesc>& first,
    const std::vector<rhi::VertexBufferLayoutDesc>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& a = first[index];
        const auto& b = second[index];
        if (a.stride != b.stride || a.stepMode != b.stepMode ||
            a.attributes.size() != b.attributes.size()) {
            return false;
        }
        for (std::size_t attribute = 0; attribute < a.attributes.size(); ++attribute) {
            const auto& lhs = a.attributes[attribute];
            const auto& rhs = b.attributes[attribute];
            if (lhs.location != rhs.location || lhs.format != rhs.format ||
                lhs.offset != rhs.offset) {
                return false;
            }
        }
    }
    return true;
}

base::Status validate_state(const rhi::PipelineDesc& state) {
    if (state.vertexShader != nullptr || state.fragmentShader != nullptr) {
        return base::Status::failure(
            base::StatusCode::invalid_argument,
            "Renderer: pipeline shader pointers must be null; supply owned shaders");
    }
    if (state.dynamicState != rhi::DynamicState::none) {
        return base::Status::failure(
            base::StatusCode::unsupported,
            "Renderer: batches currently support static graphics state only");
    }
    return base::Status::success();
}

} // namespace

base::Status IPipelineCache::validate_batch(const RenderBatch& batch) const {
    return validate_batch_data(batch, {});
}

NullPipelineCache::NullPipelineCache(rhi::Device& device) : device_(&device) {}

base::Status NullPipelineCache::prepare_render_pass(const rhi::RenderPassDesc& pass) {
    auto result = attachments(pass);
    if (!result.ok()) {
        return result.status();
    }
    auto state = std::move(result).value();
    if (colorFormats_ != state.colors || depthStencilFormat_ != state.depth ||
        sampleCount_ != state.samples) {
        colorFormats_ = std::move(state.colors);
        depthStencilFormat_ = state.depth;
        sampleCount_ = state.samples;
        pipeline_ = {};
    }
    return base::Status::success();
}

base::Result<rhi::Pipeline*> NullPipelineCache::get_or_create(
    const InstanceLayout&, MaterialId, std::size_t) {
    if (!pipeline_.valid()) {
        rhi::PipelineDesc desc;
        for (const auto format : colorFormats_) {
            desc.colorTargets.push_back({.format = format});
        }
        desc.depthStencil.format = depthStencilFormat_;
        desc.multisample.sampleCount = sampleCount_;
        desc.debugName = "null pipeline";
        auto result = device_->create_pipeline(desc);
        if (!result.ok()) {
            return result.status();
        }
        pipeline_ = std::move(result).value();
    }
    return &pipeline_;
}

PipelineCache::PipelineCache(rhi::Device& device) : device_(&device) {}

base::Result<StagedProgram> PipelineCache::stage_program(const ProgramDesc& desc) {
    if (desc.vertex.package == nullptr || desc.fragment.package == nullptr ||
        desc.vertex.stage != rhi::ShaderStage::vertex ||
        desc.fragment.stage != rhi::ShaderStage::fragment) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: vertex and fragment packages/stages required");
    }
    if (auto status = validate_state(desc.pipeline); !status.ok()) {
        return status;
    }
    for (const auto* selection : {&desc.vertex, &desc.fragment}) {
        const auto& available = device_->adapter_info().supportedFeatures;
        for (const auto feature : selection->package->desc().requiredFeatures) {
            if (std::find(available.begin(), available.end(), feature) == available.end()) {
                return base::Status::failure(
                    base::StatusCode::unsupported,
                    "Renderer: package requires an unavailable device capability");
            }
        }
        auto variant = selection->package->select_variant(
            selection->target, selection->entryPoint, selection->stage,
            selection->permutation);
        if (!variant.ok()) {
            return variant.status();
        }
        if (!variant.value()->reflection.pushConstants.empty()) {
            return base::Status::failure(
                base::StatusCode::unsupported,
                "Renderer: push-constant batches are not implemented");
        }
        if (selection->stage == rhi::ShaderStage::vertex) {
            if (auto status = validate_vertex_layout(
                    desc.pipeline.vertexBuffers, variant.value()->reflection.interfaces);
                !status.ok()) {
                return status;
            }
        }
    }
    auto vertex = device_->create_shader(*desc.vertex.package, desc.vertex.target,
                                        desc.vertex.entryPoint, desc.vertex.stage,
                                        desc.vertex.permutation);
    if (!vertex.ok()) {
        return vertex.status();
    }
    auto fragment = device_->create_shader(
        *desc.fragment.package, desc.fragment.target, desc.fragment.entryPoint,
        desc.fragment.stage, desc.fragment.permutation);
    if (!fragment.ok()) {
        return fragment.status();
    }
    return stage_shaders({std::move(vertex).value(), std::move(fragment).value()},
                         desc.pipeline);
}

base::Result<StagedProgram> PipelineCache::stage_shaders(
    ShaderBinding shaders, const rhi::PipelineDesc& state) {
    if (auto status = validate_state(state); !status.ok()) {
        return status;
    }
    if (auto status = validate_vertex_layout(state.vertexBuffers, {}); !status.ok()) {
        return status;
    }
    if (!shaders.vertexShader.valid() || !shaders.fragmentShader.valid() ||
        shaders.vertexShader.stage() != rhi::ShaderStage::vertex ||
        shaders.fragmentShader.stage() != rhi::ShaderStage::fragment) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: owned vertex/fragment shaders required");
    }
    try {
        auto nativeDesc = state;
        nativeDesc.vertexShader = &shaders.vertexShader;
        nativeDesc.fragmentShader = &shaders.fragmentShader;
        auto pipeline = device_->create_pipeline(nativeDesc);
        if (!pipeline.ok()) {
            return pipeline.status();
        }
        StagedProgram program;
        program.owner_ = this;
        program.shaders_ = std::move(shaders);
        program.pipeline_ = std::move(pipeline).value();
        if (state.layout != nullptr) {
            program.layoutId_ = state.layout->id();
        }
        for (const auto& target : state.colorTargets) {
            program.colorFormats_.push_back(target.format);
        }
        program.depthStencilFormat_ = state.depthStencil.format;
        program.sampleCount_ = state.multisample.sampleCount;
        program.vertexBuffers_ = state.vertexBuffers;
        return program;
    } catch (const std::bad_alloc&) {
        return base::Status::failure(base::StatusCode::out_of_memory,
                                     "Renderer: staging program allocation failed");
    }
}

base::Status PipelineCache::commit_program(
    MaterialId material, StagedProgram&& program, std::size_t variantHash) {
    if (program.owner_ != this || !program.pipeline_.valid()) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: candidate belongs to another cache or was consumed");
    }
    const CacheKey key{material, variantHash};
    const auto found = programs_.find(key);
    if (found != programs_.end() &&
        (found->second->layoutId_ != program.layoutId_ ||
         found->second->colorFormats_ != program.colorFormats_ ||
         found->second->depthStencilFormat_ != program.depthStencilFormat_ ||
         found->second->sampleCount_ != program.sampleCount_ ||
         !same_vertex_layouts(found->second->vertexBuffers_, program.vertexBuffers_) ||
         !same_bindings(found->second->pipeline_, program.pipeline_))) {
        return base::Status::failure(
            base::StatusCode::invalid_argument,
            "Renderer: replacement changes the published binding/attachment contract");
    }
    try {
        // Allocate before moving so allocation failure cannot consume the candidate.
        auto candidate = std::unique_ptr<StagedProgram>(new StagedProgram);
        if (found == programs_.end()) {
            const auto inserted = programs_.try_emplace(key, nullptr).first;
            *candidate = std::move(program);
            inserted->second = std::move(candidate);
        } else {
            *candidate = std::move(program);
            found->second = std::move(candidate);
        }
        return base::Status::success();
    } catch (const std::bad_alloc&) {
        return base::Status::failure(base::StatusCode::out_of_memory,
                                     "Renderer: publishing program allocation failed");
    }
}

base::Status PipelineCache::register_program(
    MaterialId material, const ProgramDesc& desc, std::size_t variantHash) {
    auto candidate = stage_program(desc);
    if (!candidate.ok()) {
        return candidate.status();
    }
    return commit_program(material, std::move(candidate).value(), variantHash);
}

base::Status PipelineCache::register_shaders(
    MaterialId material, ShaderBinding shaders, const rhi::PipelineDesc& state,
    std::size_t variantHash) {
    auto candidate = stage_shaders(std::move(shaders), state);
    if (!candidate.ok()) {
        return candidate.status();
    }
    return commit_program(material, std::move(candidate).value(), variantHash);
}

base::Status PipelineCache::prepare_render_pass(const rhi::RenderPassDesc& pass) {
    auto result = attachments(pass);
    if (!result.ok()) {
        return result.status();
    }
    auto state = std::move(result).value();
    colorFormats_ = std::move(state.colors);
    depthStencilFormat_ = state.depth;
    sampleCount_ = state.samples;
    passPrepared_ = true;
    return base::Status::success();
}

base::Result<rhi::Pipeline*> PipelineCache::get_or_create(
    const InstanceLayout&, MaterialId material, std::size_t variantHash) {
    const auto found = programs_.find({material, variantHash});
    if (found == programs_.end()) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: material/configuration is not registered");
    }
    auto& program = *found->second;
    if (passPrepared_ &&
        (colorFormats_ != program.colorFormats_ ||
         depthStencilFormat_ != program.depthStencilFormat_ ||
         sampleCount_ != program.sampleCount_)) {
        return base::Status::failure(
            base::StatusCode::invalid_argument,
            "Renderer: program graphics state does not match render-pass attachments");
    }
    return &program.pipeline_;
}

const rhi::Pipeline* PipelineCache::published(
    MaterialId material, std::size_t variantHash) const noexcept {
    const auto found = programs_.find({material, variantHash});
    return found == programs_.end() ? nullptr : &found->second->pipeline_;
}

void PipelineCache::invalidate(MaterialId material) {
    std::erase_if(programs_, [material](const auto& entry) {
        return entry.first.material == material;
    });
}

void PipelineCache::invalidate_all() { programs_.clear(); }

base::Status PipelineCache::validate_batch(const RenderBatch& batch) const {
    const auto found = programs_.find({batch.material, batch.variantHash});
    if (found == programs_.end()) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: batch program is not registered");
    }
    return validate_batch_data(batch, found->second->vertexBuffers_);
}

} // namespace truffle::renderer
