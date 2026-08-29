#include "truffle/rhi/backend_support.hpp"

#include <cassert>
#include <set>
#include <utility>

int main() {
    using namespace truffle::rhi;
    const auto matrix = backend_platform_support();
    assert(!matrix.empty());
    std::set<std::pair<BackendKind, PlatformKind>> unique;
    bool foundNull = false;
    bool foundHostGpu = false;
    for (const auto& row : matrix) {
        assert(unique.emplace(row.backend, row.platform).second);
        assert(!backend_name(row.backend).empty());
        assert(!platform_name(row.platform).empty());
        assert(!maturity_name(row.maturity).empty());
        if (row.maturity == BackendMaturity::native_smoke ||
            row.maturity == BackendMaturity::conformant ||
            row.maturity == BackendMaturity::supported) {
            assert(row.evidence.compiles);
            assert(row.evidence.nativeSmoke);
        }
        if (row.backend == BackendKind::null_validation) {
            foundNull = true;
            assert(!row.gpuBackend);
            assert(row.maturity == BackendMaturity::validation_only);
        } else if (row.platform == host_platform() && row.gpuBackend) {
            foundHostGpu = true;
        }
    }
    assert(foundNull);
    assert(foundHostGpu);
    return 0;
}
