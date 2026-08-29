#include "truffle/rhi/null_backend.hpp"

#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
#include "truffle/rhi/direct3d_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_METAL_BACKEND
#include "truffle/rhi/metal_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
#include "truffle/rhi/opengl_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
#include "truffle/rhi/opengles_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
#include "truffle/rhi/vulkan_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
#include "truffle/rhi/webgpu_backend.hpp"
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
#include "truffle/rhi/webgl2_backend.hpp"
#endif

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Report {
    std::string name;
    std::string kind;
    std::string maturity;
    bool available = false;
    bool native = false;
    bool validationOnly = false;
};

template <typename Factory>
[[nodiscard]] Report native_report(std::string name, Factory&& factory) {
    auto result = std::forward<Factory>(factory)();
    if (!result.ok()) {
        return {std::move(name), "native", "source_only", false, false, false};
    }
    auto instance = std::move(result).value();
    auto adapter = instance.adapter(0);
    if (!adapter.ok()) {
        return {std::move(name), "native", "source_only", false, false, false};
    }
    const auto& info = adapter.value().info();
    return {std::move(name), "native",
            std::string{truffle::rhi::maturity_name(info.maturity)}, true,
            info.native, info.validationOnly};
}

void write_report(std::ostream& output, const std::vector<Report>& reports) {
    output << "{\n  \"reports\": [\n";
    for (std::size_t index = 0; index < reports.size(); ++index) {
        const auto& report = reports[index];
        output << "    {\"backend\": \"" << report.name
               << "\", \"kind\": \"" << report.kind
               << "\", \"maturity\": \"" << report.maturity
               << "\", \"available\": " << (report.available ? "true" : "false")
               << ", \"native\": " << (report.native ? "true" : "false")
               << ", \"validationOnly\": "
               << (report.validationOnly ? "true" : "false") << "}";
        output << (index + 1 == reports.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace truffle;
    std::vector<Report> reports;

    auto nullResult = rhi::create_null_instance();
    if (!nullResult.ok()) {
        return 1;
    }
    auto nullInstance = std::move(nullResult).value();
    auto nullAdapter = nullInstance.adapter(0);
    if (!nullAdapter.ok()) {
        return 1;
    }
    reports.push_back({"null", "contract_interpreter", "validation_only", true,
                       nullAdapter.value().info().native,
                       nullAdapter.value().info().validationOnly});

#ifdef TRUFFLE_HAS_METAL_BACKEND
    reports.push_back(native_report("metal", [] {
        return rhi::create_metal_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
    reports.push_back(native_report("vulkan", [] {
        return rhi::create_vulkan_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
    reports.push_back(native_report("opengl", [] {
        return rhi::create_opengl_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
    reports.push_back(native_report("direct3d12", [] {
        return rhi::create_direct3d12_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_OPENGLES_BACKEND
    reports.push_back(native_report("opengles", [] {
        return rhi::create_opengles_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_WEBGPU_BACKEND
    reports.push_back(native_report("webgpu", [] {
        return rhi::create_webgpu_instance();
    }));
#endif
#ifdef TRUFFLE_HAS_WEBGL2_BACKEND
    reports.push_back(native_report("webgl2", [] {
        return rhi::create_webgl2_instance();
    }));
#endif

    write_report(std::cout, reports);
    if (argc > 1) {
        std::ofstream file{argv[1]};
        if (!file) {
            return 1;
        }
        write_report(file, reports);
    }
    return 0;
}
