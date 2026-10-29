#include "cli.hpp"

#include "workspace.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace truffle::examples::host_workspace {

namespace {

// A bounded run needs frames before and after its scripted resize.
constexpr std::uint32_t kMinimumBoundedFrames = 2;

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

        if (argument == "--frames") {
            if (index + 1 >= argc) {
                return fail("--frames requires a value");
            }
            const std::string_view value{argv[++index]};
            std::uint32_t frames = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc{} || end != value.data() + value.size() ||
                frames < kMinimumBoundedFrames) {
                return fail("--frames requires an integer of at least " +
                            std::to_string(kMinimumBoundedFrames));
            }
            result.options.frames = frames;
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

    if (result.options.frames != 0 &&
        (!result.options.useMetal || result.options.smoke)) {
        return fail("--frames requires --metal without --smoke");
    }
    return result;
}

void print_usage(std::ostream& output) {
    output << "Usage: truffle_host_workspace_example [--smoke] "
              "[--metal [--frames N]] [--vulkan] "
              "[--workspace editor|cad|simulation]\n";
}

} // namespace truffle::examples::host_workspace
