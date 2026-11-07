#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/window.hpp"

namespace truffle::gui {

class Rendering;
enum class GraphicsBackend { automatic, metal, vulkan };
enum class ImageFormat { rgba8_unorm, bgra8_unorm };
enum class ImageSampling { nearest, linear };
struct GraphicsDesc {
  WindowHandle window;
  PixelSize pixels{1, 1};
  float dpi_scale = 1.0F;
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
struct GraphicsFrameStats {
  FrameStats gui;
  std::size_t batches = 0;
  bool submitted = false, presented = false, suboptimal = false;
};

// Optional graphics integration. The host lends its native window and keeps
// ownership of its loop, input, services and application lifetime. Context is
// always supplied explicitly; this object never owns one and only borrows it
// for operations and the duration of an open frame.
class Graphics {
public:
  Graphics() = default;
  ~Graphics();
  Graphics(Graphics &&) noexcept;
  Graphics &operator=(Graphics &&) noexcept;
  Graphics(const Graphics &) = delete;
  Graphics &operator=(const Graphics &) = delete;
  [[nodiscard]] static base::Result<Graphics> create(const GraphicsDesc &);
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] base::Status resize(Context &, PixelSize, float dpi_scale);
  [[nodiscard]] base::Status replace_window(Context &, WindowHandle, PixelSize,
                                            float dpi_scale);
  [[nodiscard]] bool needs_frame(
      const Context &,
      Context::Clock::time_point = Context::Clock::now()) const noexcept;
  // No work when the tree is unchanged. Minimized windows report out_of_date.
  [[nodiscard]] base::Status draw(
      Context &, Context::Clock::time_point = Context::Clock::now());
  [[nodiscard]] GraphicsFrameStats last_frame_stats() const;
  [[nodiscard]] base::Status
      wait_idle(std::chrono::nanoseconds = std::chrono::seconds(10));

  [[nodiscard]] base::Result<TextureId> add_image(Context &, const ImageUpload &);
  [[nodiscard]] base::Status update_image(Context &, TextureId, PixelRect,
                                          std::span<const std::byte>,
                                          std::size_t bytes_per_row = 0);
  [[nodiscard]] base::Status remove_image(Context &, TextureId);
  // The text engine uses this Graphics device. Keep Graphics alive while the
  // supplied Context has text sources created by set_text().
  [[nodiscard]] base::Result<std::reference_wrapper<TextEngine>> text();
  [[nodiscard]] TextureId text_atlas() const noexcept;
  [[nodiscard]] base::Result<FontId> load_font(std::span<const std::byte>, float size);
  [[nodiscard]] base::Status set_text(Context &, ElementId, FontId, std::string);

private:
  struct State;
  std::shared_ptr<State> state_;
  friend class Rendering;
};

} // namespace truffle::gui
