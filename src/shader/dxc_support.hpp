#pragma once

#include "truffle/rhi/status.hpp"

#include <dxc/dxcapi.h>

#include <memory>
#include <string>

namespace truffle::shader::detail::dxc {

inline rhi::Status invalid(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::invalid_argument, "DXC: " + std::move(message));
}

struct Release {
    template<class T> void operator()(T* value) const {
        if (value) value->Release();
    }
};
template<class T> using ComPtr = std::unique_ptr<T, Release>;

template<class T> rhi::Result<ComPtr<T>> instance(REFCLSID clsid) {
    T* value = nullptr;
    const auto status = DxcCreateInstance(clsid, IID_PPV_ARGS(&value));
    if (FAILED(status) || !value) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
            "DXC interface initialization failed: " + std::to_string(status));
    }
    return ComPtr<T>{value};
}

} // namespace truffle::shader::detail::dxc
