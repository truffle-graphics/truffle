#include "truffle/rhi/null_backend.hpp"
#include "truffle/rhi/rhi.hpp"
#if defined(TRUFFLE_HAS_VULKAN_BACKEND)
#include "truffle/rhi/vulkan_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_METAL_BACKEND)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_OPENGL_BACKEND)
#include "truffle/rhi/opengl_backend.hpp"
#endif
#if defined(TRUFFLE_HAS_DIRECT3D_BACKEND)
#include "truffle/rhi/direct3d_backend.hpp"
#endif

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* backend_name(truffle::rhi::BackendKind kind) noexcept {
    switch (kind) {
    case truffle::rhi::BackendKind::null_backend:
        return "null";
    case truffle::rhi::BackendKind::vulkan:
        return "vulkan";
    case truffle::rhi::BackendKind::direct3d:
        return "direct3d";
    case truffle::rhi::BackendKind::opengl:
        return "opengl";
    case truffle::rhi::BackendKind::metal:
        return "metal";
    }
    return "unknown";
}

void append_bool(std::ostream& out, const char* name, bool value, bool comma = true) {
    out << "      \"" << name << "\": " << (value ? "true" : "false");
    if (comma) {
        out << ',';
    }
    out << '\n';
}

void append_uint(std::ostream& out,
                 const char* name,
                 std::uint64_t value,
                 bool comma = true) {
    out << "      \"" << name << "\": " << value;
    if (comma) {
        out << ',';
    }
    out << '\n';
}

void append_stats(std::ostream& out, const truffle::rhi::BackendStats& stats) {
    const auto append_stat = [&out](const char* name,
                                    std::uint64_t value,
                                    bool comma = true) {
        out << "        \"" << name << "\": " << value;
        if (comma) {
            out << ',';
        }
        out << '\n';
    };

    out << "      \"stats\": {\n";
    append_stat("devicesCreated", stats.devicesCreated);
    append_stat("buffersCreated", stats.buffersCreated);
    append_stat("texturesCreated", stats.texturesCreated);
    append_stat("samplersCreated", stats.samplersCreated);
    append_stat("shadersCreated", stats.shadersCreated);
    append_stat("graphicsPipelinesCreated", stats.graphicsPipelinesCreated);
    append_stat("computePipelinesCreated", stats.computePipelinesCreated);
    append_stat("bindGroupLayoutsCreated", stats.bindGroupLayoutsCreated);
    append_stat("bindGroupsCreated", stats.bindGroupsCreated);
    append_stat("surfacesCreated", stats.surfacesCreated);
    append_stat("swapchainsCreated", stats.swapchainsCreated);
    append_stat("commandBuffersCreated", stats.commandBuffersCreated);
    append_stat("fencesCreated", stats.fencesCreated);
    append_stat("uploadRingsCreated", stats.uploadRingsCreated);
    append_stat("drawsRecorded", stats.drawsRecorded);
    append_stat("dispatchesRecorded", stats.dispatchesRecorded);
    append_stat("submissions", stats.submissions);
    append_stat("debugLabelsPushed", stats.debugLabelsPushed);
    append_stat("debugMarkersInserted", stats.debugMarkersInserted, false);
    out << "      }\n";
}

void append_report(std::ostream& out, const truffle::rhi::BackendParityReport& report) {
    out << "    {\n";
    out << "      \"backend\": \"" << backend_name(report.backend) << "\",\n";
    append_uint(out, "adapterCount", report.adapterCount);
    append_bool(out, "graphicsQueue", report.graphicsQueue);
    append_bool(out, "computeQueue", report.computeQueue);
    append_bool(out, "transferQueue", report.transferQueue);
    append_bool(out, "presentation", report.presentation);
    append_bool(out, "nativeSurface", report.nativeSurface);
    append_bool(out, "shaderReflection", report.shaderReflection);
    append_bool(out, "debugLabels", report.debugLabels);
    append_bool(out, "descriptorArrays", report.descriptorArrays);
    append_bool(out, "dynamicResourceIndexing", report.dynamicResourceIndexing);
    append_bool(out, "bindlessResources", report.bindlessResources);
    append_bool(out, "unifiedMemory", report.unifiedMemory);
    append_uint(out, "maxFramesInFlight", report.maxFramesInFlight);
    append_uint(out, "maxResourceBindings", report.maxResourceBindings);
    append_uint(out, "maxDescriptorArrayElements",
                report.maxDescriptorArrayElements);
    append_uint(out, "maxBindlessResources", report.maxBindlessResources);
    append_uint(out, "formatCount", report.formatCount);
    append_uint(out, "shaderFormatCount", report.shaderFormatCount);
    append_uint(out, "memoryHeapCount", report.memoryHeapCount);
    append_uint(out, "memoryBudgetBytes", report.memoryBudgetBytes);
    append_bool(out, "dedicatedMemoryHeap", report.dedicatedMemoryHeap);
    append_stats(out, report.stats);
    out << "    }";
}

std::vector<std::unique_ptr<truffle::rhi::IBackend>> create_backends() {
    std::vector<std::unique_ptr<truffle::rhi::IBackend>> backends;
    backends.push_back(truffle::rhi::create_null_backend());
#if defined(TRUFFLE_HAS_VULKAN_BACKEND)
    backends.push_back(truffle::rhi::create_vulkan_backend());
#endif
#if defined(TRUFFLE_HAS_OPENGL_BACKEND)
    backends.push_back(truffle::rhi::create_opengl_backend());
#endif
#if defined(TRUFFLE_HAS_DIRECT3D_BACKEND)
    backends.push_back(truffle::rhi::create_direct3d_backend());
#endif
#if defined(TRUFFLE_HAS_METAL_BACKEND)
    backends.push_back(truffle::rhi::create_metal_backend());
#endif
    return backends;
}

std::string build_report_json() {
    auto backends = create_backends();
    std::ostringstream out;
    out << "{\n";
    out << "  \"reports\": [\n";
    for (std::size_t i = 0; i < backends.size(); ++i) {
        append_report(out, truffle::rhi::collect_backend_parity_report(*backends[i]));
        if (i + 1 < backends.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    const auto json = build_report_json();
    if (argc > 1) {
        std::ofstream file(argv[1]);
        if (!file) {
            std::cerr << "failed to open parity report output: " << argv[1]
                      << '\n';
            return 1;
        }
        file << json;
        return file.good() ? 0 : 1;
    }

    std::cout << json;
    return std::cout.good() ? 0 : 1;
}
