#pragma once

#include <Godot.hpp>
#include <Viewport.hpp>

class SpoutSender;

namespace godot {

class SpoutViewport : public Viewport {
  GODOT_CLASS(SpoutViewport, Viewport)
public:
  void _init();
  static void _register_methods();

  SpoutViewport();
  virtual ~SpoutViewport();

  String get_channel_name() const;
  void set_channel_name(const String &name);

protected:
  bool _is_initialized() const;
  bool _create_sender(const String &name);
  void _release_sender();
  void _send_texture();

private:
  // Spout
  SpoutSender *_sender = nullptr;
  String _channel_name;
};

} // namespace godot
