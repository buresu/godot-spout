extends SceneTree
## Measures what sending costs per frame.
##
## Renders an animated SubViewport at the given size with vsync off, in
## alternating blocks without a SpoutOutput ("off") and with one ("on"), so GPU
## clock ramp-up and thermal drift affect both sides equally. Reports pooled
## mean / median / p99 frame time per side and the difference of the medians. If the extension exposes
## `get_last_send_cpu_usec()`, the CPU time spent in the send handler is
## reported too.
##
## Usage: godot --path demo --rendering-driver d3d12 -s res://tests/send_benchmark.gd -- \
##            [--size=1920x1080] [--frames=400] [--warmup=90] [--blocks=6] [--hdr]
## Prints "BENCHMARK_RESULT <json>".

var size := Vector2i(1920, 1080)
var frames := 400
var warmup := 90
var blocks := 6
var block := 0
var hdr := false

var vp: SubViewport
var shapes: Array[ColorRect] = []
var sender: Node
var phase := "off"
var n := 0
var last_usec := 0
var samples := {"off": [], "on": []}
var send_cpu: Array = []


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--size="):
			var p := arg.get_slice("=", 1).split("x")
			size = Vector2i(int(p[0]), int(p[1]))
		elif arg.begins_with("--frames="):
			frames = int(arg.get_slice("=", 1))
		elif arg.begins_with("--warmup="):
			warmup = int(arg.get_slice("=", 1))
		elif arg.begins_with("--blocks="):
			blocks = int(arg.get_slice("=", 1))
		elif arg == "--hdr":
			hdr = true
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	vp = SubViewport.new()
	vp.size = size
	vp.use_hdr_2d = hdr
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var bg := ColorRect.new()
	bg.color = Color(0.1, 0.1, 0.15)
	bg.size = size
	vp.add_child(bg)
	for i in 32:
		var r := ColorRect.new()
		r.size = Vector2(size.y / 6.0, size.y / 6.0)
		r.color = Color.from_hsv(i / 32.0, 0.8, 0.9)
		vp.add_child(r)
		shapes.append(r)
	root.add_child(vp)


func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	var t := now / 1_000_000.0
	for i in shapes.size():
		var s := shapes[i]
		s.position = Vector2(
			(sin(t * 0.7 + i) * 0.5 + 0.5) * (size.x - s.size.x),
			(cos(t * 1.1 + i * 1.3) * 0.5 + 0.5) * (size.y - s.size.y))
	if last_usec != 0:
		n += 1
		if n > warmup:
			samples[phase].append(now - last_usec)
			if phase == "on" and sender and sender.has_method("get_last_send_cpu_usec"):
				send_cpu.append(sender.call("get_last_send_cpu_usec"))
		if n >= warmup + frames:
			block += 1
			n = 0
			if block >= blocks:
				_report()
				quit(0)
				return true
			if phase == "off":
				phase = "on"
				sender = ClassDB.instantiate("SpoutOutput")
				sender.set("channel_name", "gs-bench-%d" % OS.get_process_id())
				sender.set("texture", vp.get_texture())
				root.add_child(sender)
			else:
				phase = "off"
				sender.queue_free()
				sender = null
	last_usec = now
	return false


func _stats(a: Array) -> Dictionary:
	if a.is_empty():
		return {}
	var s := a.duplicate()
	s.sort()
	var total := 0.0
	for v in s:
		total += v
	return {
		"mean_us": snappedf(total / s.size(), 0.1),
		"p50_us": s[s.size() / 2],
		"p99_us": s[int(s.size() * 0.99)],
		"n": s.size(),
	}


func _report() -> void:
	var off := _stats(samples["off"])
	var on := _stats(samples["on"])
	var res := {
		"driver": RenderingServer.get_current_rendering_driver_name(),
		"adapter": RenderingServer.get_video_adapter_name(),
		"size": "%dx%d" % [size.x, size.y],
		"format": "RGBA16F" if hdr else "RGBA8",
		"off": off,
		"on": on,
		"delta_p50_us": on["p50_us"] - off["p50_us"],
		"delta_mean_us": snappedf(on["mean_us"] - off["mean_us"], 0.1),
		"send_cpu": _stats(send_cpu),
	}
	print("BENCHMARK_RESULT ", JSON.stringify(res))
