#include "graphics_state.hpp"
#include "truffle/gui/specialist/surface.hpp"

namespace truffle::gui {
namespace {
base::Status invalid(const char *message) {
  return base::Status::failure(base::StatusCode::invalid_state, message);
}
} // namespace
base::Result<TextureId> Rendering::add_texture(Graphics &graphics, Context &context,
                                               renderer::SampledTexture texture,
                                               const renderer::SamplerDesc &desc) {
  if (!graphics.valid() || graphics.state_->frame_open)
    return invalid("Texture registration requires an initialized, closed GUI frame");
  if (!texture.texture().valid())
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI image is invalid");
  auto registered = graphics.state_->painter.add_texture(renderer::RhiInterop::native(texture),
                                                        renderer::RhiInterop::native(desc));
  if (!registered.ok())
    return registered.status();
  graphics.state_->images.emplace(registered.value().value, std::move(texture));
  context.request_frame();
  return registered.value();
}
base::Result<TextureId> Graphics::add_image(Context &context, const ImageUpload &desc) {
  if (!valid() || state_->frame_open)
    return invalid("Image upload requires an initialized, closed GUI frame");
  if ((desc.format != ImageFormat::rgba8_unorm &&
       desc.format != ImageFormat::bgra8_unorm) ||
      (desc.sampling != ImageSampling::nearest &&
       desc.sampling != ImageSampling::linear)) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown GUI image format/sampling");
  }
  auto image = renderer::SampledTexture::upload(
      Rendering::device(*this), {
                                    .extent = desc.pixels,
                                    .format = desc.format == ImageFormat::rgba8_unorm
                                                  ? renderer::TextureFormat::rgba8_unorm
                                                  : renderer::TextureFormat::bgra8_unorm,
                                    .bytes = desc.bytes,
                                    .bytesPerRow = desc.bytes_per_row,
                                });
  if (!image.ok())
    return image.status();
  const auto filter = desc.sampling == ImageSampling::linear ? renderer::Filter::linear
                                                             : renderer::Filter::nearest;
  return Rendering::add_texture(*this, context, std::move(image).value(),
                                {.minFilter = filter, .magFilter = filter});
}
base::Status Graphics::update_image(Context &context, TextureId id, PixelRect rect,
                                       std::span<const std::byte> bytes,
                                       std::size_t row) {
  if (!valid() || state_->frame_open)
    return invalid("Image update requires an initialized, closed GUI frame");
  const auto found = state_->images.find(id.value);
  if (found == state_->images.end() || rect.x < 0 || rect.y < 0)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI image/region is invalid");
  auto status = found->second.update(
      Rendering::device(*this),
      {static_cast<std::uint32_t>(rect.x), static_cast<std::uint32_t>(rect.y)},
      {rect.width, rect.height}, bytes, row);
  if (status.ok())
    context.request_frame();
  return status;
}
base::Status Graphics::remove_image(Context &context, TextureId id) {
  if (!valid() || state_->frame_open)
    return invalid("Image removal requires an initialized, closed GUI frame");
  if (!state_->images.contains(id.value))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI image is not registered");
  auto status = state_->painter.remove_texture(id);
  if (status.ok()) {
    state_->images.erase(id.value);
    context.request_frame();
  }
  return status;
}
base::Result<std::reference_wrapper<TextEngine>> Graphics::text() {
  if (!valid())
    return invalid("GUI Graphics is not initialized");
  if (!state_->text) {
    if (state_->frame_open)
      return invalid("Create GUI text resources before opening a frame");
    auto engine = specialist::TextInterop::create(
        renderer::RhiInterop::native(Rendering::device(*this)));
    if (!engine.ok())
      return engine.status();
    state_->text.emplace(std::move(engine).value());
    auto atlas = specialist::TextInterop::atlas(*state_->text);
    if (!atlas.ok()) {
      state_->text.reset();
      return atlas.status();
    }
    auto id = state_->painter.add_texture(atlas.value().get());
    if (!id.ok()) {
      state_->text.reset();
      return id.status();
    }
    state_->atlas = id.value();
  }
  return std::ref(*state_->text);
}
TextureId Graphics::text_atlas() const noexcept {
  return valid() ? state_->atlas : TextureId{};
}
base::Result<FontId> Graphics::load_font(std::span<const std::byte> bytes,
                                            float size) {
  auto engine = text();
  if (!engine.ok())
    return engine.status();
  return engine.value().get().load_font(bytes, size);
}
base::Status Graphics::set_text(Context &context, ElementId element, FontId font,
                                std::string value) {
  auto engine = text();
  if (!engine.ok())
    return engine.status();
  return context.set_text_source(element, engine.value().get(), font, std::move(value),
                                 state_->atlas);
}
base::Result<std::vector<renderer::RenderBatch>> Rendering::record(Graphics &graphics,
                                                                   Context &context,
                                                                   std::uint32_t slot,
                                                                   PixelSize pixels,
                                                                   Surface *surface) {
  if (!graphics.valid())
    return invalid("GUI Graphics is not initialized");
  if (graphics.state_->frame_open && graphics.state_->frame_context != &context)
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI frame belongs to a different Context");
  if (pixels.width == 0 || pixels.height == 0)
    return base::Status::failure(base::StatusCode::out_of_date,
                                 "GUI recording target is minimized");
  if (surface && &surface->context() != &context)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI Surface must belong to the supplied Context");
  auto &state = *graphics.state_;
  auto raw =
      surface ? specialist::SurfaceInterop::record(*surface, state.painter, slot,
                                                   {pixels.width, pixels.height})
              : state.painter.record(context, slot, {pixels.width, pixels.height});
  if (!raw.ok())
    return raw.status();
  std::vector<renderer::RenderBatch> result;
  for (const auto &batch : raw.value())
    result.push_back(renderer::RhiInterop::batch(batch, graphics.state_));
  state.stats.batches = result.size();
  return result;
}
base::Result<renderer::FrameGraph::NodeId>
Rendering::append_gui_pass(Graphics &graphics, Context &context,
                           renderer::FrameGraph &graph, const RenderingFrame &frame,
                           renderer::RenderPassDesc pass, bool use_window,
                           Surface *surface,
                           std::vector<renderer::TextureTransition> transitions) {
  if (pass.extent.width == 0 && pass.extent.height == 0)
    pass.extent = frame.pixels;
  if (pass.extent != frame.pixels)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI pass extent must match the recording target");
  auto batches = record(graphics, context, frame.slot, frame.pixels, surface);
  if (!batches.ok())
    return batches.status();
  auto added = graph.add_render_pass({.pass = std::move(pass),
                                      .batches = std::move(batches).value(),
                                      .useWindow = use_window,
                                      .transitions = std::move(transitions)});
  if (added.ok())
    graph.retain(graphics.state_);
  return added;
}

} // namespace truffle::gui
