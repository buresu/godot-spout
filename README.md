# godot-spout
Godot Spout Addon via GDExtension
Send Godot textures to Spout receivers (Resolume Arena, TouchDesigner, OBS,
...) and receive Spout senders as Godot textures, on Windows.

## Support matrix

| Renderer / driver | Send | Receive | Notes |
|---|---|---|---|
| Forward+ / Mobile, **D3D12** | ✅ | ✅ | Recommended. Always built. |
| Forward+ / Mobile, **Vulkan** | ✅ | ✅ | `GODOT_SPOUT_ENABLE_VULKAN`. Needs a driver that imports D3D11 KMT shared textures (NVIDIA: yes; Intel UHD: no). See [Known limitations](#known-limitations). |
| Compatibility, **OpenGL3** | ✅ | ❌ | `GODOT_SPOUT_ENABLE_OPENGL`. Uses SpoutGL (GL/DX interop, CPU fallback if the driver has no interop). |

Tested with Godot 4.7.2 on Windows 11 (NVIDIA Quadro T2000; Intel UHD for
D3D12), Godot to Godot in one process and across processes. Testing with
Resolume Arena and other Spout applications is in progress (checklist in
`docs/IMPLEMENTATION_PLAN.md` §5.2). The extension is built against
godot-cpp for Godot 4.6.

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

`install` copies the DLL into `demo/addons/godot-spout/bin`. Copy
`demo/addons/godot-spout` into your project.

## Usage

### Sending: `SpoutOutput` (Node)

```gdscript
var out := SpoutOutput.new()
out.channel_name = "Walls"                  # the name receivers see
out.texture = $SubViewport.get_texture()    # any Texture2D
add_child(out)                              # sends while in the tree
```

| Property | Default | |
|---|---|---|
| `channel_name` | `"godot-spout"` | Sender name. Can change at runtime: the old name disappears, the new one appears, nothing else is re-created. If the name is taken, Spout appends `_1`; see `get_active_sender_name()`. |
| `texture` | – | Source. Can be switched at runtime (viewport ↔ viewport ↔ image); receivers keep the sender. Use `SubViewport.get_texture()`, not a preloaded `ViewportTexture` resource (that one is never set up for your scene). |
| `enabled` | `true` | Off: the sender leaves the Spout list; GPU resources are kept for a quick re-enable. |
| `active_in_editor` | `true` | Also send while the scene is open in the editor. |
| `format` | `FORMAT_AUTO` | `FORMAT_AUTO` (RGBA16F for float sources such as HDR 2D viewports, else RGBA8), `FORMAT_RGBA8`, `FORMAT_RGBA16F`. |
| `flip_y` | `false` | Flip vertically. |
| `alpha_mode` | `ALPHA_KEEP` | `ALPHA_OPAQUE` forces alpha to 1. |
| `color_conversion` | `COLOR_NONE` | `COLOR_LINEAR_TO_SRGB` for linear sources (viewports with `use_hdr_2d`), `COLOR_SRGB_TO_LINEAR`. |
| `max_fps` | `0` | Cap the send rate (0 = every frame). Skips sends; never sleeps. |

Methods: `is_sending()`, `get_active_sender_name()`, `get_sender_size()`,
`get_frames_sent()`, `get_frames_skipped()` (a receiver held Spout's access
mutex), `get_last_send_cpu_usec()`, `get_adapter_name()`.

Signals: `sender_started(name)`, `sender_stopped(name)`,
`sender_resized(name, size)` (also emitted on a format change).

The source is resized, switched or re-formatted in place: the shared texture
is re-created and receivers follow (Spout `UpdateSender`); the sender never
leaves the list for that.

### Receiving: `SpoutInput` (Node) and `SpoutTexture` (Texture2D)

`SpoutTexture` is a texture you can assign anywhere (materials,
`TextureRect`, shaders); it receives as long as it exists. `SpoutInput` is a
node that writes into a `Texture2DRD` you give it and receives while it's in
the tree. Both have the same API:

```gdscript
var tex := SpoutTexture.new()
tex.channel_name = "Composition"         # "" = the active sender
tex.sender_connected.connect(func(n): print("receiving ", n))
$MeshInstance3D.material_override.albedo_texture = tex
print(SpoutTexture.get_sender_list())
```

| Property | Default | |
|---|---|---|
| `channel_name` | `""` | Sender to receive; empty = the active sender. The inspector suggests the senders that are running; a name that isn't running yet is fine. |
| `enabled` | `true` | |
| `active_in_editor` | `true` | |
| `flip_y` | `false` | |
| `clear_on_disconnect` | `false` | Keep showing the last frame while the sender is gone (default), or clear. |
| `texture` (`SpoutInput` only) | – | The `Texture2DRD` to write into. |

Methods: static `get_sender_list()`, `is_sender_connected()`,
`get_receiver_state()` (`STATE_IDLE`, `STATE_WAITING`, `STATE_CONNECTED`,
`STATE_ERROR`), `get_sender_name()`, `get_sender_size()`,
`get_sender_format()`, `get_sender_fps()`, `get_sender_frame()`,
`get_frames_received()`, `get_adapter_name()`, `get_last_error()`.

Signals: `sender_connected(name)`, `sender_disconnected(name)`,
`frame_received()`, `receive_error(message)`.

A receiver reconnects by itself when its sender comes back, and follows size
and format changes. While waiting it looks for the sender 4 times a second;
nothing is allocated in the meantime.

### Behaviour worth knowing

- **Latency.** Sending adds one Godot frame (1/fps: 16.7 ms at 60 fps).
  Godot copies the frame into a texture we own, and that copy is published
  after Godot submitted it. Receiving adds none.
- **Colours.** 8-bit viewports hold sRGB-encoded values, which is what
  Spout receivers expect. HDR 2D viewports (`use_hdr_2d`) are linear
  RGBA16F; set `color_conversion = COLOR_LINEAR_TO_SRGB`. Received 8-bit
  textures look the same in 2D and in 3D materials (`source_color`).
- **Alpha.** A transparent viewport stores *premultiplied* colour where
  semi-transparent things were drawn over the clear colour. Receivers that
  expect straight alpha will show darker edges there; use `ALPHA_OPAQUE` if
  you don't need transparency.
- **Formats.** Senders publish RGBA8 (`DXGI_FORMAT_R8G8B8A8_UNORM`) or
  RGBA16F. Receivers accept BGRA8, RGBA8, RGB10A2, RGBA16, RGBA16F and
  RGBA32F senders. Spout's memory-sharing (CPU) mode is not supported.
- **Threading.** All GPU work runs on Godot's render thread, also with the
  multi-threaded renderer (tested mostly with the default thread model).

### Multiple GPUs

Spout textures can only be shared between applications on the **same GPU**.
On laptops with integrated + dedicated graphics, or desktops with two
cards, put Godot and the receiver/sender on the same GPU:
- Windows Settings → System → Display → Graphics: pick the same GPU for
  Godot (and your exported game) and for Resolume;
- Godot: `--gpu-index N` on the command line (Godot prints the GPU it uses
  at startup);
- Resolume: its GPU preference.

godot-spout prints the GPU it shares on at startup
(`[godot-spout] Sharing textures through ... on <GPU>`), and every node has
`get_adapter_name()`. A receiver that finds its sender on another GPU warns
once ("...probably running on a different GPU...") and stays in
`STATE_ERROR` until the sender changes; there is no CPU fallback.

## Performance

CPU time per frame for one sender (`tests/send_cost.gd`, Quadro T2000,
p50 / p99, the same for 1920×1080 and 7680×1080, RGBA8 and RGBA16F):

| Backend | CPU per send |
|---|---|
| D3D12 | 0.22–0.28 ms / 0.46–0.63 ms |
| Vulkan | ~0.05 ms / 0.10–0.19 ms (was ~4.8 ms per frame before this version) |

GPU work per send is one copy (Vulkan) or two (D3D12: into our texture, then
into the shared texture) of the frame; roughly 66 MB of memory traffic per
copy at 7680×1080 RGBA8, 17 MB at 1920×1080. Measure it with a GPU profiler
on your machine if it matters.

## Demo and tests

Scenes in `demo/scenes` (overrides after `--`: `--resolution=7680x1080`,
`--channel=NAME`, `--seconds=N`):

| Scene | |
|---|---|
| `send_animated.tscn` | Animated Forward+ 3D sender with a frame counter and a moving bar; stats overlay. |
| `receive_material.tscn` | Receives into a 3D material and a TextureRect; sender picker. |
| `stress_switch_resize.tscn` | Two senders + two receivers; resizes, switches, renames and toggles every few seconds. |
| `soak.tscn` | Long-running sender + receiver; logs memory and counters to `user://soak.csv`. |
| `output_demo.tscn`, `input_demo.tscn`, `texture_demo.tscn` | The original minimal demos. |

Automated checks in `demo/tests` (run with
`godot --path demo --rendering-driver d3d12|vulkan -s res://tests/<name>.gd`):
`api_test.gd` (21 API checks), `loopback_test.gd`, `cross_process_test.gd`
(run a `--role=send` and a `--role=recv` process), `srgb_material_test.gd`,
`lifecycle_test.gd`, `send_cost.gd`, `send_benchmark.gd`,
`viewport_texture_check.gd`.

The manual checklist for Resolume Arena and the Spout demo apps is in
`docs/IMPLEMENTATION_PLAN.md` §5.2.

## Diagnostics

Environment variables:
- `GODOT_SPOUT_VERBOSE=1`: print godot-spout's messages without `--verbose`.
- `GODOT_SPOUT_LOG=1`: Spout's own log, written to `godot-spout.log` in
  `%APPDATA%\Spout`.
- `GODOT_SPOUT_D3D12_DEBUG=1` (with `--gpu-validation`): print D3D12 and
  D3D11 debug-layer messages. Godot itself doesn't print them.

## Known limitations

- **Vulkan interop is outside the Vulkan spec.** Godot doesn't enable
  `VK_KHR_external_memory_win32` (or `VK_KHR_win32_keyed_mutex`), so
  importing Spout's D3D11 textures relies on the driver not enforcing it
  (NVIDIA doesn't). The validation layer reports one `memoryTypeIndex`
  message per imported texture for that reason. There is also no GPU-side
  sync with the other process. **Use D3D12 for shows.**
- **Vulkan on Intel iGPUs doesn't work:** the Intel driver only imports
  D3D11 NT handles, Spout uses the older KMT handles.
- **OpenGL3 receive** is not implemented. A route exists (an own GL texture
  + `RenderingServer.texture_create_from_native_handle`).
- **Older D3D12 drivers without enhanced barriers** use a different code
  path (tracked legacy resource states) that hasn't been tested on such a
  machine yet.
- With the D3D12 debug layer, D3D11On12 itself reports
  `ReflectSharedProperties: Resource provided was not shared by D3D11` once
  per texture it wraps. It is harmless.
- At quit, the last Vulkan images are left to the OS (Godot still flushes
  its last frame, which uses them, after the extension shuts down); the
  validation layer reports them as not destroyed.

## License
MIT License
