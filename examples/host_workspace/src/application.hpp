#pragma once

#include "workspace.hpp"

#include <cstdint>

namespace truffle::examples::host_workspace {

struct ApplicationOptions {
    WorkspaceKind workspace = WorkspaceKind::editor;
    bool smoke      = false;
    bool useMetal   = false;
    bool useVulkan  = false;
    bool useOpenGL  = false;
    // With useMetal or useOpenGL: present this many host-window frames (including one
    // resize) and exit; zero runs until the window closes.
    std::uint32_t frames = 0;
};

[[nodiscard]] int run_application(const ApplicationOptions& options);

} // namespace truffle::examples::host_workspace
