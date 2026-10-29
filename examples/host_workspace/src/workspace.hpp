#pragma once

#include "truffle/encom/world.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace truffle::examples::host_workspace {

struct FrameShape {
    std::size_t cameras     = 0;
    std::size_t lights      = 0;
    std::size_t meshBatches = 0; // batches produced by EnComRenderAdapter (not per-entity)
};

enum class WorkspaceKind {
    editor,
    cad,
    simulation,
};

class IWorkspace {
public:
    virtual ~IWorkspace() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void build_scene(encom::World& world) const = 0;
    [[nodiscard]] virtual std::vector<encom::World::System> systems() const = 0;
    [[nodiscard]] virtual FrameShape expected_frame_shape() const noexcept = 0;
};

[[nodiscard]] std::optional<WorkspaceKind>
parse_workspace_kind(std::string_view name);
[[nodiscard]] std::string_view workspace_name(WorkspaceKind kind) noexcept;
[[nodiscard]] std::unique_ptr<IWorkspace> create_workspace(WorkspaceKind kind);

} // namespace truffle::examples::host_workspace
