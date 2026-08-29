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
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
#include "truffle/rhi/vulkan_backend.hpp"
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
    auto metalResult = rhi::create_metal_instance();
    if (metalResult.ok()) {
        auto metal = std::move(metalResult).value();
        auto adapter = metal.adapter(0);
        reports.push_back({"metal", "native",
                           adapter.ok() ? "native_smoke" : "cross_compiles",
                           adapter.ok(),
                           adapter.ok() && adapter.value().info().native, false});
    } else {
        reports.push_back(
            {"metal", "native", "cross_compiles", false, false, false});
    }
#endif
#ifdef TRUFFLE_HAS_VULKAN_BACKEND
    reports.push_back({"vulkan", "unavailable", "source_only",
                       rhi::create_vulkan_instance().ok(), false, false});
#endif
#ifdef TRUFFLE_HAS_OPENGL_BACKEND
    reports.push_back({"opengl", "unavailable", "source_only",
                       rhi::create_opengl_instance().ok(), false, false});
#endif
#ifdef TRUFFLE_HAS_DIRECT3D_BACKEND
    reports.push_back({"direct3d12", "unavailable", "source_only",
                       rhi::create_direct3d12_instance().ok(), false, false});
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
