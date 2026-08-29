#include "truffle/rhi/backend_support.hpp"
#include "truffle/rhi/null_backend.hpp"

#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
#include "truffle/rhi/direct3d_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_METAL_BACKEND
#include "truffle/rhi/metal_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
#include "truffle/rhi/opengles_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
#include "truffle/rhi/opengl_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
#include "truffle/rhi/vulkan_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
#include "truffle/rhi/webgl2_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
#include "truffle/rhi/webgpu_backend.hpp"
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using truffle::rhi::BackendKind;
using truffle::rhi::BackendMaturity;
struct Probe {
    BackendKind backend = BackendKind::null_validation;
    bool runtimeAvailable = false;
    bool adapterNative = false;
    bool validationOnly = false;
    truffle::rhi::PlatformKind platform = truffle::rhi::PlatformKind::all;
    BackendMaturity maturity = BackendMaturity::source_only;
    std::string adapterName;
    std::string status;
};

[[nodiscard]] std::string escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

template <typename Factory>
[[nodiscard]] Probe run_probe(BackendKind backend, Factory&& factory) {
    auto result = std::forward<Factory>(factory)();
    if (!result.ok()) {
        return Probe{
            .backend = backend,
            .adapterName = {},
            .status = result.status().message,
        };
    }
    auto instance = std::move(result).value();
    auto adapterResult = instance.adapter(0);
    if (!adapterResult.ok()) {
        return Probe{
            .backend = backend,
            .adapterName = {},
            .status = adapterResult.status().message,
        };
    }
    auto adapter = std::move(adapterResult).value();
    const auto& info = adapter.info();
    return Probe{
        .backend = backend,
        .runtimeAvailable = true,
        .adapterNative = info.native,
        .validationOnly = info.validationOnly,
        .platform = info.platform,
        .maturity = info.maturity,
        .adapterName = info.name,
        .status = "ok",
    };
}

[[nodiscard]] std::vector<Probe> run_probes() {
    std::vector<Probe> probes;
    probes.push_back(run_probe(BackendKind::null_validation, [] {
        return truffle::rhi::create_null_instance();
    }));
#ifdef TRUFFLE_HAS_METAL_BACKEND
    probes.push_back(run_probe(BackendKind::metal, [] {
        return truffle::rhi::create_metal_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
    probes.push_back(run_probe(BackendKind::vulkan, [] {
        return truffle::rhi::create_vulkan_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
    probes.push_back(run_probe(BackendKind::direct3d12, [] {
        return truffle::rhi::create_direct3d12_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
    probes.push_back(run_probe(BackendKind::opengl, [] {
        return truffle::rhi::create_opengl_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
    probes.push_back(run_probe(BackendKind::opengles, [] {
        return truffle::rhi::create_opengles_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
    probes.push_back(run_probe(BackendKind::webgpu, [] {
        return truffle::rhi::create_webgpu_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
    probes.push_back(run_probe(BackendKind::webgl2, [] {
        return truffle::rhi::create_webgl2_instance();
    }));
#endif
    return probes;
}

void write_json(std::ostream& output, const std::vector<Probe>& probes) {
    output << "{\n  \"hostPlatform\": \""
           << truffle::rhi::platform_name(truffle::rhi::host_platform())
           << "\",\n  \"matrix\": [\n";
    const auto matrix = truffle::rhi::backend_platform_support();
    for (std::size_t index = 0; index < matrix.size(); ++index) {
        const auto& row = matrix[index];
        output << "    {\"backend\": \""
               << truffle::rhi::backend_name(row.backend)
               << "\", \"platform\": \""
               << truffle::rhi::platform_name(row.platform)
               << "\", \"maturity\": \""
               << truffle::rhi::maturity_name(row.maturity)
               << "\", \"gpuBackend\": "
               << (row.gpuBackend ? "true" : "false")
               << ", \"evidence\": {\"compile\": "
               << (row.evidence.compiles ? "true" : "false")
               << ", \"smoke\": "
               << (row.evidence.nativeSmoke ? "true" : "false")
               << ", \"conformance\": "
               << (row.evidence.conformance ? "true" : "false")
               << ", \"validation\": "
               << (row.evidence.validation ? "true" : "false")
               << ", \"presentation\": "
               << (row.evidence.presentation ? "true" : "false")
               << "}, \"notes\": \"" << escape(row.notes) << "\"}"
               << (index + 1 == matrix.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"probes\": [\n";
    for (std::size_t index = 0; index < probes.size(); ++index) {
        const auto& probe = probes[index];
        output << "    {\"backend\": \""
               << truffle::rhi::backend_name(probe.backend)
               << "\", \"runtimeAvailable\": "
               << (probe.runtimeAvailable ? "true" : "false")
               << ", \"adapterNative\": "
               << (probe.adapterNative ? "true" : "false")
               << ", \"validationOnly\": "
               << (probe.validationOnly ? "true" : "false")
               << ", \"platform\": \""
               << truffle::rhi::platform_name(probe.platform)
               << "\", \"maturity\": \""
               << truffle::rhi::maturity_name(probe.maturity)
               << "\", \"adapter\": \"" << escape(probe.adapterName)
               << "\", \"status\": \"" << escape(probe.status) << "\"}"
               << (index + 1 == probes.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

[[nodiscard]] bool strict_ok(const std::vector<Probe>& probes) {
    const auto host = truffle::rhi::host_platform();
    for (const auto& probe : probes) {
        const auto expected = std::find_if(
            truffle::rhi::backend_platform_support().begin(),
            truffle::rhi::backend_platform_support().end(),
            [&](const truffle::rhi::BackendPlatformSupport& row) {
                return row.backend == probe.backend &&
                       (row.platform == host ||
                        row.platform == truffle::rhi::PlatformKind::all);
            });
        if (expected == truffle::rhi::backend_platform_support().end()) {
            continue;
        }
        const auto requiresRuntime =
            expected->maturity == BackendMaturity::native_smoke ||
            expected->maturity == BackendMaturity::conformant ||
            expected->maturity == BackendMaturity::supported ||
            expected->maturity == BackendMaturity::validation_only;
        if (requiresRuntime && !probe.runtimeAvailable) {
            return false;
        }
        if (requiresRuntime && probe.maturity != expected->maturity) {
            return false;
        }
        if (probe.backend == BackendKind::null_validation) {
            if (probe.adapterNative || !probe.validationOnly) {
                return false;
            }
        } else if (probe.runtimeAvailable && !probe.adapterNative) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool strict = false;
    std::string outputPath;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--strict") {
            strict = true;
        } else if (argument == "--json" && index + 1 < argc) {
            outputPath = argv[++index];
        } else {
            std::cerr << "usage: truffle-rhi-doctor [--strict] [--json PATH]\n";
            return 2;
        }
    }

    const auto probes = run_probes();
    if (outputPath.empty()) {
        write_json(std::cout, probes);
    } else {
        std::ofstream output{outputPath};
        if (!output) {
            std::cerr << "could not open doctor output: " << outputPath << '\n';
            return 2;
        }
        write_json(output, probes);
    }
    return !strict || strict_ok(probes) ? 0 : 1;
}
