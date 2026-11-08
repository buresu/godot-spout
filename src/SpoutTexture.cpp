#include "SpoutTexture.hpp"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutTexture::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutTexture::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutTexture::set_channel_name);

  // Add properties
  ClassDB::add_property("SpoutTexture", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
}

SpoutTexture::SpoutTexture() : Texture2DRD() {
  _core.set_texture_handler([this](const RID &p_rid) { _show(p_rid); });
  if (spout_rendering_available()) {
    RenderingServer::get_singleton()->connect(
        "frame_pre_draw", callable_mp(this, &SpoutTexture::_on_frame_pre_draw));
    _connected = true;
  }
}

SpoutTexture::~SpoutTexture() {
  // A resource can outlive the RenderingServer (e.g. held by a script
  // constant until the very end); then there is nothing left to release.
  if (!spout_rendering_available()) {
    return;
  }
  if (_connected) {
    RenderingServer::get_singleton()->disconnect(
        "frame_pre_draw", callable_mp(this, &SpoutTexture::_on_frame_pre_draw));
  }
  _core.release();
}

String SpoutTexture::get_channel_name() const { return _channel_name; }

void SpoutTexture::set_channel_name(String p_name) {
  _channel_name = p_name;
  _core.set_name(p_name);
}

void SpoutTexture::_show(const RID &p_rid) {
  if (spout_rendering_available()) {
    set_texture_rd_rid(p_rid);
  }
}

void SpoutTexture::_on_frame_pre_draw() {
  // RenderingDevice must only be used on the render thread. frame_pre_draw is
  // normally emitted there; hop over if it ever isn't.
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_on_render_thread()) {
    rs->call_on_render_thread(callable_mp(this, &SpoutTexture::_process_frame));
    return;
  }
  _process_frame();
}

void SpoutTexture::_process_frame() {
  if (_connected) {
    _core.on_frame_pre_draw();
  }
}
