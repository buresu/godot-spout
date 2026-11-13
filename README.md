# godot-spout
Godot Spout Addon via GDExtension  
Forward+ D3D12 and Vulkan are supported for input/output.  
Requires Godot 4.6 or later.

## Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" ..
cmake --build . --config [Debug|Release] --target install
```

## Build with Vulkan
```
scoop install vulkan
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" .. -DGODOT_SPOUT_ENABLE_VULKAN=ON
cmake --build . --config [Debug|Release] --target install
```

## Build with OpenGL
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" .. -DGODOT_SPOUT_ENABLE_OPENGL=ON
cmake --build . --config [Debug|Release] --target install
```

## Usage
```gdscript
# Send
var out := SpoutOutput.new()
out.channel_name = "Godot"
out.texture = $SubViewport.get_texture()
add_child(out)

# Receive
var tex := SpoutTexture.new()
tex.channel_name = "Resolume"   # "" = the active sender
$TextureRect.texture = tex
print(SpoutTexture.get_sender_list())
```

`SpoutOutput`: `channel_name`, `texture`, `enabled`, `format` (RGBA8 /
RGBA16F), `flip_y`, `alpha_mode`, `color_conversion`, `max_fps`. All can be
changed at runtime; the sender stays visible to receivers.

`SpoutTexture` (a Texture2D) and `SpoutInput` (a node writing into a
`Texture2DRD`): `channel_name`, `enabled`, `flip_y`, `clear_on_disconnect`.
They reconnect automatically and emit `sender_connected`,
`sender_disconnected`, `frame_received` and `receive_error`.

Use `SubViewport.get_texture()` rather than a preloaded `ViewportTexture`
resource, which is not connected to the viewport in your scene.

Demo scenes are in `demo/scenes`, automated tests in `demo/tests`.

## License
MIT License

## Limitations
- Compatibility/OpenGL3 is optionally supported for output only.
- Sender and receiver must be on the same GPU.
- Transparent viewports send premultiplied alpha; use
  `alpha_mode = ALPHA_OPAQUE` for receivers expecting straight alpha.
