#include "GDSpoutInput.hpp"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

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

  // Add properties
  ClassDB::add_property("GDSpoutInput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "GDSpoutInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2DRD"},
      "", "get_texture");
}

GDSpoutInput::GDSpoutInput() : Node() {
  _texture = Ref<Texture2DRD>(memnew(Texture2DRD));

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

bool GDSpoutInput::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutInput::_create_receiver() {
  _release_receiver();

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    return false;
  }

  String driver = rs->get_current_rendering_driver_name();
  if (driver != "d3d12") {
    ERR_PRINT("GDSpoutInput: Only Direct3D 12 rendering is supported. "
              "Current driver: " +
              driver);
    return false;
  }

  auto d3d12_device = reinterpret_cast<ID3D12Device *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                              RID(), 0));
  if (!d3d12_device) {
    return false;
  }

  auto command_queue = reinterpret_cast<IUnknown *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                              RID(), 0));
  if (!command_queue) {
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
    delete _receiver;
    _receiver = nullptr;
    return false;
  }

  return true;
}

void GDSpoutInput::_release_receiver() {

  // Free Godot RD texture before releasing the Spout-owned D3D12 resource
  if (_rd_texture.is_valid()) {
    _texture->set_texture_rd_rid(RID());
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd) {
      rd->free_rid(_rd_texture);
    }
    _rd_texture = RID();
  }

  // _d3d12_texture is borrowed from Spout — do not Release
  _d3d12_texture = nullptr;
  _width = 0;
  _height = 0;

  if (_receiver) {
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
  }
}

void GDSpoutInput::_receive_texture() {

  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  ID3D12Resource *d3d12_tex = nullptr;
  if (!_receiver->ReceiveDX12Resource(&d3d12_tex)) {
    return;
  }

  if (!d3d12_tex) {
    return;
  }

  auto width = _receiver->GetSenderWidth();
  auto height = _receiver->GetSenderHeight();

  // Recreate Godot texture if the D3D12 resource pointer or dimensions changed
  if (d3d12_tex != _d3d12_texture || width != _width || height != _height) {
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) {
      return;
    }

    if (_rd_texture.is_valid()) {
      _texture->set_texture_rd_rid(RID());
      rd->free_rid(_rd_texture);
      _rd_texture = RID();
    }

    _rd_texture = rd->texture_create_from_extension(
        RenderingDevice::TEXTURE_TYPE_2D,
        RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM,
        RenderingDevice::TEXTURE_SAMPLES_1,
        RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT,
        reinterpret_cast<uint64_t>(d3d12_tex), width, height, 1, 1);

    _d3d12_texture = d3d12_tex;
    _width = width;
    _height = height;
    _texture->set_texture_rd_rid(_rd_texture);
  }
}
