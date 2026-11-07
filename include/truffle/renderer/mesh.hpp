#pragma once

#include "truffle/renderer/render_batch.hpp"

namespace truffle::renderer {

struct MeshStream {
  std::span<const std::byte> bytes;
  std::uint32_t stride = 0;
};
struct MeshUpload {
  std::span<const MeshStream> streams;
  std::uint32_t vertexCount = 0;
  std::span<const std::byte> indices;
  IndexFormat indexFormat = IndexFormat::uint32;
};
class Mesh {
public:
  Mesh() = default;
  [[nodiscard]] static base::Result<Mesh> upload(Device &, const MeshUpload &);
  [[nodiscard]] base::Result<RenderBatch> batch(MaterialId program,
                                                std::size_t variant = 0) const;

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace truffle::renderer
