#pragma once

#include "truffle/renderer/frame_graph.hpp"
#include "truffle/renderer/pipeline_cache.hpp"

namespace truffle::renderer {

struct RendererFrameStats {
  std::uint32_t computeNodesExecuted = 0, renderNodesExecuted = 0;
  std::uint32_t renderBatchesExecuted = 0, renderBatchesClipped = 0;
  bool submitted = false, presented = false, presentationSuboptimal = false;
};
class RenderSignal {
public:
  RenderSignal() = default;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};
class Renderer {
public:
  explicit Renderer(Device &, PipelineCache *programs = nullptr);
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] base::Status render(const FrameGraph &, RenderSignal signal = {});
  [[nodiscard]] RendererFrameStats last_frame_stats() const noexcept;
  [[nodiscard]] Device &device() noexcept;

private:
  void set_frame_stats(RendererFrameStats);
  friend class Presentation;
  struct State;
  std::unique_ptr<State> state_;
  friend struct RhiInterop;
};

} // namespace truffle::renderer
