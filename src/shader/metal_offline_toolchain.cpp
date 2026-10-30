#include "metal_offline_toolchain.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TRUFFLE_XCRUN_EXECUTABLE
#define TRUFFLE_XCRUN_EXECUTABLE "xcrun"
#endif

#ifndef POSIX_SPAWN_CLOEXEC_DEFAULT
#error "Native Metal subprocess isolation requires POSIX_SPAWN_CLOEXEC_DEFAULT"
#endif

extern char** environ;

namespace truffle::shader::detail {
namespace {

using rhi::Status;
using rhi::StatusCode;

Status unsupported(std::string message) {
    return Status::failure(StatusCode::unsupported, std::move(message));
}

Status invalid(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

struct CommandOutput {
    int status = -1;
    std::string output;
    std::string errorOutput;
    std::string error;
};

struct Pipe {
    int read = -1;
    int write = -1;
};

void close_descriptor(int& descriptor) {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}

void close_pipe(Pipe& pipe) {
    close_descriptor(pipe.read);
    close_descriptor(pipe.write);
}

int make_pipe(Pipe& pipe) {
    int descriptors[2]{-1, -1};
    if (::pipe(descriptors) != 0) return errno;
    pipe.read = descriptors[0];
    pipe.write = descriptors[1];
    for (const int descriptor : descriptors) {
        const int flags = fcntl(descriptor, F_GETFD);
        if (flags < 0 ||
            fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) {
            const int error = errno;
            close_pipe(pipe);
            return error;
        }
    }
    return 0;
}

int add_child_actions(posix_spawn_file_actions_t& actions,
                      int input, int output, int errorOutput,
                      std::span<const int> descriptors) {
    int result = posix_spawn_file_actions_adddup2(
        &actions, input, STDIN_FILENO);
    if (result != 0) return result;
    result = posix_spawn_file_actions_adddup2(
        &actions, output, STDOUT_FILENO);
    if (result != 0) return result;
    result = posix_spawn_file_actions_adddup2(
        &actions, errorOutput, STDERR_FILENO);
    if (result != 0) return result;
    for (const int descriptor : descriptors) {
        if (descriptor > STDERR_FILENO) {
            result = posix_spawn_file_actions_addclose(&actions, descriptor);
            if (result != 0) return result;
        }
    }
    return 0;
}

int set_close_by_default(posix_spawnattr_t& attributes) {
    return posix_spawnattr_setflags(
        &attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
}

int wait_for(pid_t child, int& status) {
    pid_t result = -1;
    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);
    return result == child ? 0 : (result < 0 ? errno : ECHILD);
}

std::string read_pipe(int descriptor, std::vector<std::byte>* binary,
                      std::string* text) {
    std::array<std::byte, 8192> buffer{};
    for (;;) {
        const auto count = read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            if (binary != nullptr) {
                binary->insert(binary->end(), buffer.begin(),
                               buffer.begin() + count);
            }
            if (text != nullptr) {
                text->append(reinterpret_cast<const char*>(buffer.data()),
                             static_cast<std::size_t>(count));
            }
        } else if (count == 0) {
            return {};
        } else if (errno != EINTR) {
            return "Unable to read Apple Metal tool pipe: " +
                   std::string{std::strerror(errno)};
        }
    }
}

std::string write_pipe(int descriptor, std::string_view source) {
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const int maskResult = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (maskResult != 0) {
        close(descriptor);
        return "Unable to block SIGPIPE for Apple Metal tool input: " +
               std::string{std::strerror(maskResult)};
    }

    std::size_t offset = 0;
    int writeError = 0;
    while (offset < source.size()) {
        const auto count = write(descriptor, source.data() + offset,
                                 source.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            writeError = count == 0 ? EIO : errno;
            break;
        }
    }
    close(descriptor);
    if (writeError == EPIPE) {
        sigset_t pending;
        if (sigpending(&pending) == 0 &&
            sigismember(&pending, SIGPIPE) == 1) {
            int signal = 0;
            sigwait(&blocked, &signal);
        }
    }
    const int restoreResult =
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    std::string error;
    if (writeError != 0) {
        error = "Unable to write Apple Metal tool input: " +
                std::string{std::strerror(writeError)};
    }
    if (restoreResult != 0) {
        if (!error.empty()) error += "; ";
        error += "Unable to restore SIGPIPE mask: " +
                 std::string{std::strerror(restoreResult)};
    }
    return error;
}

std::vector<char*> argv_for(std::vector<std::string>& arguments) {
    std::vector<char*> values;
    values.reserve(arguments.size() + 1);
    for (auto& argument : arguments) values.push_back(argument.data());
    values.push_back(nullptr);
    return values;
}

CommandOutput run_capture(std::vector<std::string> arguments,
                          std::string_view input = {}) {
    CommandOutput result;
    Pipe inputPipe;
    Pipe outputPipe;
    Pipe errorPipe;
    for (auto* pipe : {&inputPipe, &outputPipe, &errorPipe}) {
        const int error = make_pipe(*pipe);
        if (error != 0) {
            close_pipe(inputPipe);
            close_pipe(outputPipe);
            close_pipe(errorPipe);
            result.error = "Unable to create Apple Metal tool pipes: " +
                           std::string{std::strerror(error)};
            return result;
        }
    }

    posix_spawn_file_actions_t actions;
    int actionError = posix_spawn_file_actions_init(&actions);
    const bool actionsInitialized = actionError == 0;
    if (actionsInitialized) {
        const std::array<int, 6> descriptors{
            inputPipe.read, inputPipe.write, outputPipe.read,
            outputPipe.write, errorPipe.read, errorPipe.write};
        actionError = add_child_actions(actions, inputPipe.read,
                                        outputPipe.write, errorPipe.write,
                                        descriptors);
    }
    posix_spawnattr_t attributes;
    int attributeError = 0;
    bool attributesInitialized = false;
    if (actionError == 0) {
        attributeError = posix_spawnattr_init(&attributes);
        attributesInitialized = attributeError == 0;
    }
    if (attributesInitialized) {
        attributeError = set_close_by_default(attributes);
    }
    if (actionError != 0 || attributeError != 0) {
        if (actionsInitialized) posix_spawn_file_actions_destroy(&actions);
        if (attributesInitialized) {
            posix_spawnattr_destroy(&attributes);
        }
        close_pipe(inputPipe);
        close_pipe(outputPipe);
        close_pipe(errorPipe);
        const auto error = actionError != 0 ? actionError : attributeError;
        result.error = "Unable to configure Apple Metal tool process: " +
                       std::string{std::strerror(error)};
        return result;
    }

    auto argv = argv_for(arguments);
    pid_t child = 0;
    const int spawnError = posix_spawn(
        &child, argv.front(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close_descriptor(inputPipe.read);
    close_descriptor(outputPipe.write);
    close_descriptor(errorPipe.write);
    if (spawnError != 0) {
        close_pipe(inputPipe);
        close_pipe(outputPipe);
        close_pipe(errorPipe);
        result.error = "Unable to launch Apple Metal tool: " +
                       std::string{std::strerror(spawnError)};
        return result;
    }

    std::string inputError;
    std::string outputError;
    std::string stderrError;
    const int inputDescriptor = inputPipe.write;
    const int outputDescriptor = outputPipe.read;
    const int errorDescriptor = errorPipe.read;
    inputPipe.write = -1;
    outputPipe.read = -1;
    errorPipe.read = -1;
    std::thread writer([&] { inputError = write_pipe(inputDescriptor, input); });
    std::thread outputReader([&] {
        outputError = read_pipe(outputDescriptor, nullptr, &result.output);
        close(outputDescriptor);
    });
    std::thread errorReader([&] {
        stderrError = read_pipe(errorDescriptor, nullptr, &result.errorOutput);
        close(errorDescriptor);
    });
    writer.join();
    outputReader.join();
    errorReader.join();
    int childStatus = 0;
    const int waitError = wait_for(child, childStatus);
    if (waitError != 0) {
        result.error = "Unable to reap Apple Metal tool process: " +
                       std::string{std::strerror(waitError)};
    } else if (WIFEXITED(childStatus)) {
        result.status = WEXITSTATUS(childStatus);
    } else {
        result.error = "Apple Metal tool terminated abnormally";
    }
    for (const auto& error : {inputError, outputError, stderrError}) {
        if (error.empty()) continue;
        if (!result.error.empty()) result.error += "; ";
        result.error += error;
    }
    return result;
}

#if defined(TRUFFLE_SHADER_HAS_METALLIB_COMPILER)
std::string first_line(std::string_view value) {
    const auto end = value.find_first_of("\r\n");
    auto result = std::string{value.substr(0, end)};
    if (result.size() > 1024) result.resize(1024);
    return result;
}
#endif

rhi::Result<OfflineMetalToolchain> query_toolchain() {
#if !defined(TRUFFLE_SHADER_HAS_METALLIB_COMPILER)
    return unsupported(
        "Offline metallib output requires the installed Apple Metal Toolchain");
#else
    if (access(TRUFFLE_XCRUN_EXECUTABLE, X_OK) != 0) {
        return unsupported("The configured xcrun executable is unavailable");
    }
    auto metal = run_capture(
        {TRUFFLE_XCRUN_EXECUTABLE, "--find", "metal"});
    auto metallib = run_capture(
        {TRUFFLE_XCRUN_EXECUTABLE, "--find", "metallib"});
    if (metal.status != 0 || metallib.status != 0 ||
        !metal.error.empty() || !metallib.error.empty()) {
        return unsupported("Unable to locate the installed Apple Metal Toolchain");
    }
    auto trim = [](std::string& value) {
        while (!value.empty() &&
               (value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }
    };
    trim(metal.output);
    trim(metallib.output);
    if (metal.output.empty() || metallib.output.empty() ||
        access(metal.output.c_str(), X_OK) != 0 ||
        access(metallib.output.c_str(), X_OK) != 0) {
        return unsupported("The installed Apple Metal Toolchain is incomplete");
    }
    std::error_code filesystemError;
    const auto canonicalMetal =
        std::filesystem::canonical(metal.output, filesystemError);
    if (filesystemError) {
        return unsupported("Unable to resolve the installed Metal compiler path");
    }
    OfflineMetalToolchain result;
    result.compilerRoot =
        canonicalMetal.parent_path().parent_path() / "metal";
    result.metalPath =
        (result.compilerRoot / "current/bin/metal").string();
    result.metallibPath =
        (result.compilerRoot / "current/bin/air-lld").string();
    if (access(result.metalPath.c_str(), X_OK) != 0 ||
        access(result.metallibPath.c_str(), X_OK) != 0) {
        return unsupported(
            "The installed Metal Toolchain does not expose its offline compiler/linker");
    }
    auto version = run_capture({result.metalPath, "--version"});
    auto linkerVersion = run_capture({result.metallibPath, "--version"});
    if (version.status != 0 || linkerVersion.status != 0 ||
        !version.error.empty() || !linkerVersion.error.empty()) {
        return unsupported("Unable to query the installed Apple Metal Toolchain version");
    }
    result.compilerVersion = first_line(version.output);
    result.linkerVersion = first_line(linkerVersion.output);
    if (result.compilerVersion.empty() || result.linkerVersion.empty()) {
        return unsupported("The installed Apple Metal Toolchain returned no version identity");
    }
    const auto targetStart = version.output.find("Target: air64-apple-darwin");
    if (targetStart == std::string::npos) {
        return unsupported(
            "The installed Metal compiler did not report its target SDK version");
    }
    const auto sdkStart = targetStart + std::string_view{
        "Target: air64-apple-darwin"}.size();
    const auto sdkEnd = version.output.find_first_of(".\r\n", sdkStart);
    if (sdkEnd == std::string::npos || version.output[sdkEnd] != '.') {
        return unsupported(
            "The installed Metal compiler returned an invalid target SDK version");
    }
    const auto majorEnd = version.output.find('.', sdkEnd + 1);
    if (majorEnd == std::string::npos) {
        return unsupported(
            "The installed Metal compiler returned an invalid target SDK version");
    }
    result.sdkVersion =
        std::string{version.output.substr(sdkStart, majorEnd - sdkStart)};
    return result;
#endif
}

std::vector<std::string> make_dependency_paths(std::string_view output) {
    std::string flattened;
    flattened.reserve(output.size());
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (output[index] == '\\' && index + 1 < output.size() &&
            output[index + 1] == '\n') {
            flattened.push_back(' ');
            ++index;
        } else if (output[index] == '\\' && index + 2 < output.size() &&
                   output[index + 1] == '\r' && output[index + 2] == '\n') {
            flattened.push_back(' ');
            index += 2;
        } else {
            flattened.push_back(output[index]);
        }
    }
    const auto colon = flattened.find(':');
    if (colon == std::string::npos) return {};
    std::vector<std::string> result;
    std::string token;
    bool escaped = false;
    for (std::size_t index = colon + 1; index < flattened.size(); ++index) {
        const char value = flattened[index];
        if (escaped) {
            token.push_back(value);
            escaped = false;
        } else if (value == '\\') {
            escaped = true;
        } else if (std::isspace(static_cast<unsigned char>(value))) {
            if (!token.empty()) {
                result.push_back(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(value);
        }
    }
    if (escaped) token.push_back('\\');
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

bool within(const std::filesystem::path& path,
            const std::filesystem::path& root) {
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

Status validate_native_dependencies(const OfflineMetalToolchain& toolchain,
                                    std::string_view source) {
    auto dependencies = run_capture(
        {TRUFFLE_XCRUN_EXECUTABLE, "metal", "-x", "metal",
         "-std=macos-metal2.1", "-target", "air64-apple-macos13.0",
         "-M", "-"},
        source);
    if (dependencies.status != 0 || !dependencies.error.empty()) {
        auto detail = dependencies.errorOutput.empty()
            ? dependencies.output : dependencies.errorOutput;
        if (!dependencies.error.empty()) {
            if (!detail.empty()) detail += "\n";
            detail += dependencies.error;
        }
        if (detail.empty()) detail = "native Metal dependency scan failed";
        return invalid("Unable to validate offline MSL dependencies: " +
                       detail.substr(0, 16384));
    }
    const auto paths = make_dependency_paths(dependencies.output);
    if (paths.empty()) {
        return Status::failure(StatusCode::backend_error,
                               "Metal dependency scan returned no dependency list");
    }
    for (const auto& dependency : paths) {
        std::error_code error;
        const auto canonical =
            std::filesystem::canonical(dependency, error);
        if (error || !within(canonical, toolchain.compilerRoot)) {
            return unsupported(
                "Offline MSL resolved an untracked dependency outside the "
                "installed Metal Toolchain: " + dependency);
        }
    }
    return Status::success();
}

} // namespace

bool offline_metallib_available() noexcept {
    try {
        auto toolchain = query_toolchain();
        return toolchain.ok();
    } catch (...) {
        return false;
    }
}

rhi::Result<OfflineMetalToolchain> offline_metallib_toolchain() {
    return query_toolchain();
}

rhi::Status validate_metallib_dependencies(
    const OfflineMetalToolchain& toolchain, std::string_view source) {
    return validate_native_dependencies(toolchain, source);
}

rhi::Result<OfflineMetalResult> compile_metallib(
    const OfflineMetalToolchain& toolchain, std::string_view source,
    bool debugInfo) {
    Pipe sourcePipe;
    Pipe airPipe;
    Pipe binaryPipe;
    Pipe metalErrorPipe;
    Pipe metallibErrorPipe;
    for (auto* pipe : {&sourcePipe, &airPipe, &binaryPipe,
                       &metalErrorPipe, &metallibErrorPipe}) {
        const int error = make_pipe(*pipe);
        if (error != 0) {
            for (auto* cleanup : {&sourcePipe, &airPipe, &binaryPipe,
                                  &metalErrorPipe, &metallibErrorPipe}) {
                close_pipe(*cleanup);
            }
            return unsupported(
                "Unable to create offline Metal compiler pipes: " +
                std::string{std::strerror(error)});
        }
    }
    const std::array<int, 10> descriptors{
        sourcePipe.read, sourcePipe.write, airPipe.read, airPipe.write,
        binaryPipe.read, binaryPipe.write, metalErrorPipe.read,
        metalErrorPipe.write, metallibErrorPipe.read, metallibErrorPipe.write};

    std::vector<std::string> metalArguments{
        TRUFFLE_XCRUN_EXECUTABLE, "metal", "-x", "metal",
        "-std=macos-metal2.1", "-target", "air64-apple-macos13.0"};
    if (debugInfo) metalArguments.emplace_back("-g");
    metalArguments.insert(metalArguments.end(), {"-c", "-", "-o", "-"});
    std::vector<std::string> metallibArguments{
        toolchain.metallibPath,
        "--arch", "air64",
        "--platform_version", "macos", "13.0", toolchain.sdkVersion,
        "--no-compress-reflection-list-section",
        "/dev/stdin", "-o", "/dev/stdout"};
    auto metalArgv = argv_for(metalArguments);
    auto metallibArgv = argv_for(metallibArguments);

    posix_spawn_file_actions_t metalActions;
    int actionError = posix_spawn_file_actions_init(&metalActions);
    const bool metalActionsInitialized = actionError == 0;
    posix_spawn_file_actions_t metallibActions;
    int metallibActionError = actionError == 0
        ? posix_spawn_file_actions_init(&metallibActions) : actionError;
    const bool metallibActionsInitialized = metallibActionError == 0;
    if (actionError == 0) {
        actionError = add_child_actions(
            metalActions, sourcePipe.read, airPipe.write, metalErrorPipe.write,
            descriptors);
    }
    if (metallibActionError == 0) {
        metallibActionError = add_child_actions(
            metallibActions, airPipe.read, binaryPipe.write,
            metallibErrorPipe.write, descriptors);
    }
    posix_spawnattr_t attributes;
    int attributeError = 0;
    bool attributesInitialized = false;
    if (actionError == 0 && metallibActionError == 0) {
        attributeError = posix_spawnattr_init(&attributes);
        attributesInitialized = attributeError == 0;
    }
    if (attributesInitialized) {
        attributeError = set_close_by_default(attributes);
    }
    if (actionError != 0 || metallibActionError != 0 ||
        attributeError != 0) {
        if (metalActionsInitialized) {
            posix_spawn_file_actions_destroy(&metalActions);
        }
        if (metallibActionsInitialized) {
            posix_spawn_file_actions_destroy(&metallibActions);
        }
        if (attributesInitialized) {
            posix_spawnattr_destroy(&attributes);
        }
        for (auto* pipe : {&sourcePipe, &airPipe, &binaryPipe,
                           &metalErrorPipe, &metallibErrorPipe}) {
            close_pipe(*pipe);
        }
        const int error = actionError != 0 ? actionError :
                          metallibActionError != 0 ? metallibActionError :
                                                     attributeError;
        return unsupported(
            "Unable to configure offline Metal compiler processes: " +
            std::string{std::strerror(error)});
    }

    pid_t metalChild = 0;
    pid_t metallibChild = 0;
    const int metalSpawn = posix_spawn(
        &metalChild, metalArgv.front(), &metalActions, &attributes,
        metalArgv.data(), environ);
    const int metallibSpawn = metalSpawn == 0
        ? posix_spawn(&metallibChild, metallibArgv.front(), &metallibActions,
                      &attributes, metallibArgv.data(), environ)
        : metalSpawn;
    posix_spawn_file_actions_destroy(&metalActions);
    posix_spawn_file_actions_destroy(&metallibActions);
    posix_spawnattr_destroy(&attributes);

    close_descriptor(sourcePipe.read);
    close_descriptor(airPipe.read);
    close_descriptor(airPipe.write);
    close_descriptor(binaryPipe.write);
    close_descriptor(metalErrorPipe.write);
    close_descriptor(metallibErrorPipe.write);
    if (metallibSpawn != 0) {
        close_descriptor(sourcePipe.write);
        close_descriptor(binaryPipe.read);
        close_descriptor(metalErrorPipe.read);
        close_descriptor(metallibErrorPipe.read);
        if (metalSpawn == 0) {
            const int killError = kill(metalChild, SIGTERM) == 0
                ? 0 : (errno == ESRCH ? 0 : errno);
            int ignored = 0;
            const int waitError = wait_for(metalChild, ignored);
            if (killError != 0) {
                return unsupported(
                    "Unable to stop partially launched Metal compiler: " +
                    std::string{std::strerror(killError)});
            }
            if (waitError != 0) {
                return unsupported(
                    "Unable to reap partially launched Metal compiler: " +
                    std::string{std::strerror(waitError)});
            }
        }
        return unsupported("Unable to launch Apple Metal Toolchain: " +
                           std::string{std::strerror(metallibSpawn)});
    }

    OfflineMetalResult result;
    std::string inputError;
    std::string airError;
    std::string metalError;
    std::string metallibError;
    const int sourceDescriptor = sourcePipe.write;
    const int binaryDescriptor = binaryPipe.read;
    const int metalErrorDescriptor = metalErrorPipe.read;
    const int metallibErrorDescriptor = metallibErrorPipe.read;
    sourcePipe.write = -1;
    binaryPipe.read = -1;
    metalErrorPipe.read = -1;
    metallibErrorPipe.read = -1;
    std::thread writer([&] {
        inputError = write_pipe(sourceDescriptor, source);
    });
    std::thread reader([&] {
        airError = read_pipe(binaryDescriptor, &result.bytes, nullptr);
        close(binaryDescriptor);
    });
    std::thread metalReader([&] {
        metalError = read_pipe(metalErrorDescriptor, nullptr,
                               &result.diagnostics);
        close(metalErrorDescriptor);
    });
    std::string metallibLog;
    std::thread metallibReader([&] {
        metallibError = read_pipe(metallibErrorDescriptor, nullptr,
                                  &metallibLog);
        close(metallibErrorDescriptor);
    });
    writer.join();
    reader.join();
    metalReader.join();
    metallibReader.join();

    int metalStatus = 0;
    int metallibStatus = 0;
    const int metalWaitError = wait_for(metalChild, metalStatus);
    const int metallibWaitError = wait_for(metallibChild, metallibStatus);
    result.diagnostics += metallibLog;
    std::string ioErrors;
    for (const auto& error : {inputError, airError, metalError, metallibError}) {
        if (error.empty()) continue;
        if (!ioErrors.empty()) ioErrors += "; ";
        ioErrors += error;
    }
    if (metalWaitError != 0 || metallibWaitError != 0) {
        const auto error = metalWaitError != 0 ? metalWaitError :
                                                 metallibWaitError;
        return rhi::Status::failure(
            StatusCode::backend_error,
            "Unable to reap Apple Metal tool process: " +
                std::string{std::strerror(error)});
    }
    if (!ioErrors.empty()) {
        return rhi::Status::failure(
            StatusCode::backend_error,
            "Offline Metal compiler pipe failure: " + ioErrors);
    }
    if (!WIFEXITED(metalStatus) || WEXITSTATUS(metalStatus) != 0 ||
        !WIFEXITED(metallibStatus) || WEXITSTATUS(metallibStatus) != 0) {
        auto detail = result.diagnostics;
        if (detail.size() > 16384) detail.resize(16384);
        if (detail.empty()) detail = "Apple Metal offline compilation failed";
        return invalid("Apple Metal offline compilation failed: " + detail);
    }
    if (result.bytes.empty()) {
        return rhi::Status::failure(StatusCode::backend_error,
                                    "metallib produced an empty library");
    }

    result.compilerVersion = toolchain.compilerVersion;
    result.linkerVersion = toolchain.linkerVersion;
    result.sdkVersion = toolchain.sdkVersion;
    if (result.diagnostics.size() > 16384) {
        result.diagnostics.resize(16384);
    }
    return result;
}

} // namespace truffle::shader::detail
