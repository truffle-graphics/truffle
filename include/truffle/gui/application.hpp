#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/window.hpp"

namespace truffle::gui {

class Rendering;
enum class GraphicsBackend { automatic, metal, vulkan };
enum class ImageFormat { rgba8_unorm, bgra8_unorm };
enum class ImageSampling { nearest, linear };
struct ApplicationDesc {
  WindowHandle window;
  PixelSize pixels{1, 1};
  float dpi_scale = 1.0F;
  Host *host = nullptr;
  GraphicsBackend backend = GraphicsBackend::automatic;
  bool enable_validation = true;
  std::uint32_t frames_in_flight = 2;
  Color clear{0, 0, 0, 0};
};
struct ImageUpload {
  PixelSize pixels;
  std::span<const std::byte> bytes;
  std::size_t bytes_per_row = 0;
  ImageFormat format = ImageFormat::rgba8_unorm;
  ImageSampling sampling = ImageSampling::linear;
};
struct ApplicationFrameStats {
  FrameStats gui;
  std::size_t batches = 0;
  bool submitted = false, presented = false, suboptimal = false;
};

// GUI-only entry point. The host lends its window/services and retains its loop.
// Wait for submitted work before destroying the native window. Shared-rendering
// and borrowed-device entry points are opt-in in gui/rendering.hpp.
class Application {
public:
  Application() = default;
  ~Application();
  Application(Application &&) noexcept;
  Application &operator=(Application &&) noexcept;
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  [[nodiscard]] static base::Result<Application> create(const ApplicationDesc &);
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Context &context() noexcept; // requires valid()
  [[nodiscard]] const Context &context() const noexcept;
  [[nodiscard]] base::Status handle(const InputEvent &,
                                    Context::Clock::time_point = Context::Clock::now());
  [[nodiscard]] base::Status resize(PixelSize, float dpi_scale);
  [[nodiscard]] base::Status replace_window(WindowHandle, PixelSize, float dpi_scale);
  [[nodiscard]] bool
      needs_frame(Context::Clock::time_point = Context::Clock::now()) const noexcept;
  [[nodiscard]] std::optional<Context::Clock::time_point> next_update() const noexcept;
  // No work when the tree is unchanged. Minimized windows report out_of_date.
  [[nodiscard]] base::Status draw(Context::Clock::time_point = Context::Clock::now());
  [[nodiscard]] ApplicationFrameStats last_frame_stats() const;
  [[nodiscard]] base::Status
      wait_idle(std::chrono::nanoseconds = std::chrono::seconds(10));

  [[nodiscard]] base::Result<TextureId> add_image(const ImageUpload &);
  [[nodiscard]] base::Status update_image(TextureId, PixelRect,
                                          std::span<const std::byte>,
                                          std::size_t bytes_per_row = 0);
  [[nodiscard]] base::Status remove_image(TextureId);
  // Font bytes/atlas belong to this Application and outlive its Context.
  [[nodiscard]] base::Result<std::reference_wrapper<TextEngine>> text();
  [[nodiscard]] TextureId text_atlas() const noexcept;
  [[nodiscard]] base::Result<FontId> load_font(std::span<const std::byte>, float size);
  [[nodiscard]] base::Status set_text(ElementId, FontId, std::string);

private:
  struct State;
  std::shared_ptr<State> state_;
  friend class Rendering;
};

} // namespace truffle::gui
