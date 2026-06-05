#include "cli.hpp"

#include "workspace.hpp"

#include <string_view>
#include <utility>

namespace truffle::examples::host_workspace {

namespace {

[[nodiscard]] CommandLineParse fail(std::string message) {
    CommandLineParse result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

} // namespace

CommandLineParse parse_command_line(int argc, char* argv[]) {
    CommandLineParse result;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (argument == "--help" || argument == "-h") {
            result.showHelp = true;
            continue;
        }

        if (argument == "--smoke") {
            result.options.smoke = true;
            continue;
        }

        if (argument == "--metal") {
            result.options.useMetal = true;
            continue;
        }

        if (argument == "--vulkan") {
            result.options.useVulkan = true;
            continue;
        }

        if (argument == "--workspace") {
            if (index + 1 >= argc) {
                return fail("--workspace requires a value");
            }

            const std::string_view workspaceValue{argv[++index]};
            const auto workspace = parse_workspace_kind(workspaceValue);
            if (!workspace.has_value()) {
                return fail("unknown workspace: " + std::string{workspaceValue});
            }
            result.options.workspace = *workspace;
            continue;
        }

        return fail("unknown argument: " + std::string{argument});
    }

    return result;
}

void print_usage(std::ostream& output) {
    output << "Usage: truffle_host_workspace_example [--smoke] "
              "[--metal] [--vulkan] "
              "[--workspace editor|cad|simulation]\n";
}

} // namespace truffle::examples::host_workspace
