#pragma once

#include "truffle/base/status.hpp"
#include "truffle/renderer/types.hpp"

#include <memory>

namespace truffle::renderer {

struct RhiInterop;
struct DeviceOptions {
  Backend backend = Backend::automatic;
  bool enableValidation = true;
  WindowHandle presentationWindow;
};

// Owned native devices retain their instance. Explicit specialist imports borrow
// the app's device; it stays alive/unmoved. Resources share stable device state.
class Device {
public:
  Device() = default;
  Device(Device &&) noexcept = default;
  Device &operator=(Device &&) noexcept = default;
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  // Native factory is in optional Truffle::RendererPresentation.
  [[nodiscard]] static base::Result<Device> create(const DeviceOptions &options = {});
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool lost() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

} // namespace truffle::renderer
