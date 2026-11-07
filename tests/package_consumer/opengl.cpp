#include "truffle/rhi/opengl_backend.hpp"

#include <cstdio>

int main() {
    auto instance = truffle::rhi::create_opengl_instance({.enableValidation = true});
    if (!instance.ok()) {
        std::fprintf(stderr, "%s\n", instance.status().message.c_str());
        return 1;
    }
    return 0;
}
