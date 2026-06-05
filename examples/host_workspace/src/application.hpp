#pragma once

#include "workspace.hpp"

namespace truffle::examples::host_workspace {

struct ApplicationOptions {
    WorkspaceKind workspace = WorkspaceKind::editor;
    bool smoke      = false;
    bool useMetal   = false;
    bool useVulkan  = false;
};

[[nodiscard]] int run_application(const ApplicationOptions& options);

} // namespace truffle::examples::host_workspace
