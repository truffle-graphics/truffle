#include "truffle/base/config.hpp"
#include "truffle/base/handle.hpp"
#include "truffle/base/status.hpp"

namespace truffle::base {
// The Base target gives shared public contracts a stable link boundary.
namespace detail {
void link_anchor() noexcept {}
}
}
