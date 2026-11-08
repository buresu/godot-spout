#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include "SpoutCore.hpp"

namespace godot {

// Sends a texture (typically a SubViewport's) to Spout receivers such as
// Resolume Arena. Active while the node is inside the scene tree.
class SpoutOutput : public Node {
  GDCLASS(SpoutOutput, Node)
public:
  enum Format {
    FORMAT_AUTO = SPOUT_SEND_FORMAT_AUTO,
    FORMAT_RGBA8 = SPOUT_SEND_FORMAT_RGBA8,
    FORMAT_RGBA16F = SPOUT_SEND_FORMAT_RGBA16F,
  };
  enum AlphaMode {
    ALPHA_KEEP = 0,
    ALPHA_OPAQUE = 1,
  };
  enum ColorConversion {
    COLOR_NONE = SPOUT_COLOR_NONE,
    COLOR_LINEAR_TO_SRGB = SPOUT_COLOR_LINEAR_TO_SRGB,
    COLOR_SRGB_TO_LINEAR = SPOUT_COLOR_SRGB_TO_LINEAR,
  };

  SpoutOutput();
  ~SpoutOutput() override;

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture2D> get_texture() const;
  void set_texture(Ref<Texture2D> p_texture);

  bool is_enabled() const { return _enabled; }
  void set_enabled(bool p_enabled);
  bool is_active_in_editor() const { return _active_in_editor; }
  void set_active_in_editor(bool p_active);
  Format get_format() const { return _format; }
  void set_format(Format p_format);
  bool get_flip_y() const { return _flip_y; }
  void set_flip_y(bool p_flip);
  AlphaMode get_alpha_mode() const { return _alpha_mode; }
  void set_alpha_mode(AlphaMode p_mode);
  ColorConversion get_color_conversion() const { return _color_conversion; }
  void set_color_conversion(ColorConversion p_conversion);
  double get_max_fps() const { return _max_fps; }
  void set_max_fps(double p_fps);

  // Status
  bool is_sending() const;
  String get_active_sender_name() const;
  Vector2i get_sender_size() const;
  int64_t get_last_send_cpu_usec() const;
  int64_t get_frames_sent() const;
  int64_t get_frames_skipped() const;
  String get_adapter_name() const;

protected:
  static void _bind_methods();
  void _notification(int p_what);

private:
  bool _should_run() const;
  void _update_running();
  void _start();
  void _stop();
  void _on_frame_post_draw();
  void _process_frame();
  void _apply_options();
  void _resolve_gl_source();

  SpoutSenderCore _core;
  bool _connected = false;
  // OpenGL: the viewport whose render target backs _texture, if any.
  ObjectID _gl_viewport;
  uint64_t _gl_resolve_at_msec = 0;

  // Properties
  String _channel_name = "godot-spout";
  Ref<Texture2D> _texture;
  bool _enabled = true;
  bool _active_in_editor = true;
  Format _format = FORMAT_AUTO;
  bool _flip_y = false;
  AlphaMode _alpha_mode = ALPHA_KEEP;
  ColorConversion _color_conversion = COLOR_NONE;
  double _max_fps = 0.0;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::SpoutOutput::Format);
VARIANT_ENUM_CAST(godot::SpoutOutput::AlphaMode);
VARIANT_ENUM_CAST(godot::SpoutOutput::ColorConversion);
