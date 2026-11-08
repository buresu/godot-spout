#include "SpoutOutput.hpp"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutOutput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutOutput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutOutput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &SpoutOutput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &SpoutOutput::set_texture);

  // Add properties
  ClassDB::add_property("SpoutOutput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "SpoutOutput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"},
      "set_texture", "get_texture");
}

SpoutOutput::SpoutOutput() : Node() { _core.set_name(_channel_name); }

SpoutOutput::~SpoutOutput() { _stop(); }

String SpoutOutput::get_channel_name() const { return _channel_name; }

void SpoutOutput::set_channel_name(String p_name) {
  // Takes effect with the next frame; the sender is renamed in place.
  _channel_name = p_name;
  _core.set_name(p_name);
}

Ref<Texture2D> SpoutOutput::get_texture() const { return _texture; }

void SpoutOutput::set_texture(Ref<Texture2D> p_texture) {
  // Switching the source keeps the sender (and its shared texture, if the
  // size and format match), so receivers don't see it disappear.
  _texture = p_texture;
  _core.set_texture(p_texture);
}

void SpoutOutput::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    _start();
    break;
  case NOTIFICATION_EXIT_TREE:
  case NOTIFICATION_PREDELETE:
    _stop();
    break;
  default:
    break;
  }
}

void SpoutOutput::_start() {
  if (_connected || !spout_rendering_available()) {
    return;
  }
  RenderingServer::get_singleton()->connect(
      "frame_post_draw", callable_mp(this, &SpoutOutput::_on_frame_post_draw));
  _connected = true;
}

void SpoutOutput::_stop() {
  if (_connected && spout_rendering_available()) {
    RenderingServer::get_singleton()->disconnect(
        "frame_post_draw",
        callable_mp(this, &SpoutOutput::_on_frame_post_draw));
  }
  _connected = false;
  // Unregisters the sender and frees the GPU resources (scene change, node
  // freed, quit).
  if (spout_rendering_available()) {
    _core.release();
  }
}

void SpoutOutput::_on_frame_post_draw() {
  // With the multi-threaded renderer, frame_post_draw is emitted deferred on
  // the main thread; RenderingDevice must only be used on the render thread.
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_on_render_thread()) {
    rs->call_on_render_thread(callable_mp(this, &SpoutOutput::_process_frame));
    return;
  }
  _process_frame();
}

void SpoutOutput::_process_frame() {
  if (_connected) {
    _core.on_frame_post_draw();
  }
}
