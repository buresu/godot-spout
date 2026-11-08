extends SceneTree
## Direct measurement of the CPU time one SpoutOutput costs per frame: the
## time spent in its frame_post_draw handler (get_last_send_cpu_usec), which
## includes recording the copy and, on D3D12, submitting the D3D11On12 copy.
##
## GPU time is not measured here. RenderingDevice timestamps captured around
## the handler don't bracket the copy: Godot's render graph reorders commands
## (tried: D3D12 reported a constant ~5 ms at every size, Vulkan 0). Use a GPU
## profiler (PIX, Nsight) on the show machine for that.
##
## Usage: godot --path demo --rendering-driver d3d12 -s res://tests/send_cost.gd -- \
##            [--size=1920x1080] [--frames=600] [--hdr] [--flip]
## Prints "SEND_COST <json>".

var size := Vector2i(1920, 1080)
var frames := 600
var hdr := false
var flip := false

var vp: SubViewport
var sender: SpoutOutput
var n := 0
var cpu: Array[float] = []


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--size="):
			var p := arg.get_slice("=", 1).split("x")
			size = Vector2i(int(p[0]), int(p[1]))
		elif arg.begins_with("--frames="):
			frames = int(arg.get_slice("=", 1))
		elif arg == "--hdr":
			hdr = true
		elif arg == "--flip":
			flip = true
	vp = SubViewport.new()
	vp.size = size
	vp.use_hdr_2d = hdr
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var bg := ColorRect.new()
	bg.color = Color(0.3, 0.5, 0.7)
	bg.size = size
	vp.add_child(bg)
	root.add_child(vp)

	sender = SpoutOutput.new()
	sender.channel_name = "gs-cost-%d" % OS.get_process_id()
	sender.texture = vp.get_texture()
	sender.flip_y = flip
	root.add_child(sender)


func _process(_delta: float) -> bool:
	n += 1
	if n > 60:
		cpu.append(sender.get_last_send_cpu_usec())
	if n >= frames + 60:
		print("SEND_COST ", JSON.stringify({
			"driver": RenderingServer.get_current_rendering_driver_name(),
			"adapter": RenderingServer.get_video_adapter_name(),
			"size": "%dx%d" % [size.x, size.y],
			"format": "RGBA16F" if hdr else "RGBA8",
			"pass": "compute (flip)" if flip else "copy",
			"cpu_us": _stats(cpu),
			"frames_sent": sender.get_frames_sent(),
			"frames_skipped": sender.get_frames_skipped(),
		}))
		quit(0)
		return true
	return false


func _stats(a: Array) -> Dictionary:
	if a.is_empty():
		return {}
	var s := a.duplicate()
	s.sort()
	var total := 0.0
	for v in s:
		total += v
	return {"mean": snappedf(total / s.size(), 0.1), "p50": s[s.size() / 2],
		"p99": s[int(s.size() * 0.99)], "n": s.size()}
