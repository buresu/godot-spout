# godot-spout
Godot Spout Addon via GDExtension  
Forward+ D3D12 and Vulkan are supported for input/output.  
Requires Godot 4.6 or later.

## Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" .. [options]
cmake --build . --config [Debug|Release] --target install
```

Options:
- `-DGODOT_SPOUT_ENABLE_VULKAN=ON`: Vulkan support (requires `scoop install vulkan`)
- `-DGODOT_SPOUT_ENABLE_OPENGL=ON`: OpenGL support

## Usage
```gdscript
# Send
var out := SpoutOutput.new()
out.channel_name = "Godot"
out.texture = $SubViewport.get_texture()  # not a preloaded ViewportTexture
add_child(out)

# Receive
var tex := SpoutTexture.new()
tex.channel_name = "Resolume"  # "" = the active sender
$TextureRect.texture = tex
print(SpoutTexture.get_sender_list())
```

`SpoutInput` is a node version of `SpoutTexture` that writes into a `Texture2DRD`.
Demos are in `demo/scenes`, tests in `demo/tests`.

## Limitations
- Compatibility/OpenGL3 is optionally supported for output only.
- Sender and receiver must be on the same GPU.
- Transparent viewports send premultiplied alpha; use
  `alpha_mode = ALPHA_OPAQUE` for receivers expecting straight alpha.

## License
MIT License
