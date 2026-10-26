#pragma once

#include <string>

namespace truffle::base {

struct RuntimeConfig {
    std::string application_name = "Truffle Consumer";
    bool enable_validation = true;
};

} // namespace truffle::base

