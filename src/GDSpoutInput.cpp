#include "GDSpoutInput.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <SpoutDX12.h>

using namespace godot;

void GDSpoutInput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_receive_texture"),
                       &GDSpoutInput::_receive_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &GDSpoutInput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &GDSpoutInput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &GDSpoutInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &GDSpoutInput::set_texture);

  // Add properties
  ClassDB::add_property("GDSpoutInput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "GDSpoutInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2DRD"},
      "set_texture", "get_texture");
}

GDSpoutInput::GDSpoutInput() : Node() {
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});
}

GDSpoutInput::~GDSpoutInput() {

  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  _release_receiver();
}

String GDSpoutInput::get_channel_name() const { return _channel_name; }

void GDSpoutInput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_receiver();
}

Ref<Texture2DRD> GDSpoutInput::get_texture() const { return _texture; }

void GDSpoutInput::set_texture(Ref<Texture2DRD> p_texture) {
  if (_texture == p_texture) {
    return;
  }
  // Clear RID from old texture
  if (_texture.is_valid()) {
    _texture->set_texture_rd_rid(RID());
  }
  _texture = p_texture;
  // Apply current RID to new texture immediately if already receiving
  if (_texture.is_valid() && _rd_texture.is_valid()) {
    _texture->set_texture_rd_rid(_rd_texture);
  }
}

bool GDSpoutInput::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutInput::_create_receiver() {
  _release_receiver();

  UtilityFunctions::print("GDSpoutInput: _create_receiver() channel='", _channel_name, "'");

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    UtilityFunctions::printerr("GDSpoutInput: RenderingDevice not available");
    return false;
  }

  String driver = rs->get_current_rendering_driver_name();
  UtilityFunctions::print("GDSpoutInput: rendering driver=", driver);
  if (driver != "d3d12") {
    ERR_PRINT("GDSpoutInput: Only Direct3D 12 rendering is supported. "
              "Current driver: " +
              driver);
    return false;
  }

  auto d3d12_device = reinterpret_cast<ID3D12Device *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                              RID(), 0));
  UtilityFunctions::print("GDSpoutInput: D3D12 device=", (uint64_t)d3d12_device);
  if (!d3d12_device) {
    UtilityFunctions::printerr("GDSpoutInput: failed to get D3D12 device");
    return false;
  }

  auto command_queue = reinterpret_cast<IUnknown *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                              RID(), 0));
  UtilityFunctions::print("GDSpoutInput: command queue=", (uint64_t)command_queue);
  if (!command_queue) {
    UtilityFunctions::printerr("GDSpoutInput: failed to get command queue");
    return false;
  }

  _receiver = new spoutDX12();

  if (!_channel_name.is_empty()) {
    auto channel = _channel_name.utf8();
    _receiver->SetReceiverName(channel.get_data());
  }

  // spoutDX12's destructor calls Release() on the device even for external
  // devices, so we AddRef here to keep the balance.
  d3d12_device->AddRef();

  if (!_receiver->OpenDirectX12(d3d12_device, &command_queue)) {
    UtilityFunctions::printerr("GDSpoutInput: OpenDirectX12 failed");
    delete _receiver;
    _receiver = nullptr;
    return false;
  }

  UtilityFunctions::print("GDSpoutInput: receiver created successfully");
  return true;
}

void GDSpoutInput::_release_receiver() {

  // Release Spout receiver first (releases its internal m_pReceivedResource11
  // which wraps our Godot-owned D3D12 texture)
  if (_receiver) {
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
  }

  // _d3d12_texture is borrowed from Godot — do not Release
  _d3d12_texture = nullptr;

  // Detach Godot texture then free the RD resource
  if (_rd_texture.is_valid()) {
    if (_texture.is_valid()) {
      _texture->set_texture_rd_rid(RID());
    }
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd) {
      rd->free_rid(_rd_texture);
    }
    _rd_texture = RID();
  }

  _width = 0;
  _height = 0;
}

bool GDSpoutInput::_recreate_receive_texture(uint32_t p_width, uint32_t p_height) {

  auto rd = RenderingServer::get_singleton()->get_rendering_device();
  if (!rd) {
    return false;
  }

  // _d3d12_texture is borrowed from Godot — do not Release.
  // When called after IsUpdated(), Spout has already nulled m_pReceivedResource11
  // internally, so it is safe to free the underlying Godot texture immediately.
  _d3d12_texture = nullptr;

  // Detach and free the old Godot RD texture
  if (_rd_texture.is_valid()) {
    if (_texture.is_valid()) {
      _texture->set_texture_rd_rid(RID());
    }
    rd->free_rid(_rd_texture);
    _rd_texture = RID();
  }

  _width = p_width;
  _height = p_height;

  // Create a new Godot RD texture that Spout will copy into.
  // CAN_COPY_TO_BIT is required because Spout wraps this resource as COPY_DEST.
  Ref<RDTextureFormat> fmt;
  fmt.instantiate();
  fmt->set_format(RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM);
  fmt->set_width(p_width);
  fmt->set_height(p_height);
  fmt->set_depth(1);
  fmt->set_array_layers(1);
  fmt->set_mipmaps(1);
  fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
  fmt->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
  fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);

  Ref<RDTextureView> view;
  view.instantiate();

  _rd_texture = rd->texture_create(fmt, view);
  if (!_rd_texture.is_valid()) {
    UtilityFunctions::printerr("GDSpoutInput: texture_create failed");
    return false;
  }

  // Get the native ID3D12Resource* — borrowed from Godot, do not Release
  _d3d12_texture = reinterpret_cast<ID3D12Resource *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE,
                              _rd_texture, 0));
  if (!_d3d12_texture) {
    UtilityFunctions::printerr("GDSpoutInput: failed to get D3D12 texture handle");
    rd->free_rid(_rd_texture);
    _rd_texture = RID();
    return false;
  }

  UtilityFunctions::print("GDSpoutInput: receive texture created ",
                          p_width, "x", p_height,
                          " d3d12=", (uint64_t)_d3d12_texture);

  if (_texture.is_valid()) {
    _texture->set_texture_rd_rid(_rd_texture);
  }

  return true;
}

void GDSpoutInput::_receive_texture() {

  if (_texture.is_null()) {
    return;
  }

  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  // Pass our receive texture (null on the first call — Spout will set m_bUpdated)
  if (!_receiver->ReceiveDX12Resource(&_d3d12_texture)) {
    if (_d3d12_texture != nullptr) {
      UtilityFunctions::print("GDSpoutInput: sender disconnected");
    }
    return;
  }

  // IsUpdated() returns true when a sender connects or its size changes,
  // and MUST be called to clear m_bUpdated so data can flow next frame.
  if (_receiver->IsUpdated()) {
    auto width = _receiver->GetSenderWidth();
    auto height = _receiver->GetSenderHeight();
    UtilityFunctions::print("GDSpoutInput: sender updated '",
                            String(_receiver->GetSenderName()),
                            "' ", width, "x", height);
    _recreate_receive_texture(width, height);
  }
}
