#pragma once

#include "truffle/renderer/texture.hpp"

namespace truffle::renderer {

struct ColorAttachmentDesc {
  Texture texture, resolveTexture;
  LoadOp loadOp = LoadOp::clear;
  StoreOp storeOp = StoreOp::store;
  ClearColor clear;
};
struct DepthStencilAttachmentDesc {
  Texture texture;
  LoadOp depthLoadOp = LoadOp::clear;
  StoreOp depthStoreOp = StoreOp::store;
  float clearDepth = 1;
  LoadOp stencilLoadOp = LoadOp::clear;
  StoreOp stencilStoreOp = StoreOp::store;
  std::uint32_t clearStencil = 0;
};
struct RenderPassDesc {
  PixelSize extent{0, 0};
  std::vector<ColorAttachmentDesc> colorAttachments;
  DepthStencilAttachmentDesc depthStencilAttachment;
};
enum class ResourceAccess { Read, Write };
struct NodeResourceUsage {
  std::uint64_t resourceId = 0;
  ResourceAccess access = ResourceAccess::Read;
};
struct RenderPassNode {
  RenderPassDesc pass;
  std::vector<RenderBatch> batches;
  bool useWindow = false;
  std::vector<TextureTransition> transitions;
  void transition_before(TextureTransition transition) {
    transitions.push_back(std::move(transition));
  }
};

// Per-frame graph; nodes retain the ordinary resources referenced by their
// passes/batches. Streamed slots still obey pacing, never resubmit stale graphs.
class FrameGraph {
public:
  using NodeId = std::uint32_t;
  FrameGraph();
  ~FrameGraph();
  FrameGraph(FrameGraph &&) noexcept;
  FrameGraph &operator=(FrameGraph &&) noexcept;
  FrameGraph(const FrameGraph &) = delete;
  FrameGraph &operator=(const FrameGraph &) = delete;
  [[nodiscard]] base::Result<NodeId> add_render_pass(RenderPassNode);
  [[nodiscard]] base::Status add_dependency(NodeId before, NodeId after);
  [[nodiscard]] base::Status add_resource_usage(NodeId, NodeResourceUsage);
  [[nodiscard]] base::Result<std::vector<NodeId>> resolve_execution_order() const;
  void retain(std::shared_ptr<void>);

private:
  struct State;
  std::unique_ptr<State> state_;
  friend struct RhiInterop;
};

} // namespace truffle::renderer
