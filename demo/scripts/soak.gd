extends Control
## Soak test: an animated sender plus a receiver (by default of the same
## sender, so it works without Resolume; pass --channel=<Resolume output name>
## to receive from Resolume instead). Every `log_every` seconds a line goes to
## user://soak.csv:
##   time_s, fps, frame_ms_p99, video_mem_mb, static_mem_mb, frames_sent,
##   frames_skipped, frames_received, connects, disconnects, errors
## Memory must stay flat and frames_received must keep growing.
##
## Overrides: -- --resolution=7680x1080 --seconds=28800 --log_every=300

@export var resolution := Vector2i(1920, 1080)
@export var log_every := 300.0

var _viewport: SubViewport
var _bar: ColorRect
var _output: SpoutOutput
var _input: SpoutInput
var _file: FileAccess
var _since_log := 0.0
var _frame_ms := PackedFloat32Array()
var _last_usec := 0
var _counts := {"connects": 0, "disconnects": 0, "errors": 0}
var _quit_after := 0.0
var _anim := 0


func _ready() -> void:
	resolution = SpoutDemoArgs.get_resolution(resolution)
	log_every = float(SpoutDemoArgs.get_value("log_every", str(log_every)))
	_quit_after = float(SpoutDemoArgs.get_value("seconds", "0"))
	var channel := SpoutDemoArgs.get_value("channel", "soak-" + str(OS.get_process_id()))

	_viewport = SubViewport.new()
	_viewport.size = resolution
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var bg := ColorRect.new()
	bg.color = Color(0.1, 0.1, 0.2)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(bg)
	_bar = ColorRect.new()
	_bar.size = Vector2(32, resolution.y)
	_viewport.add_child(_bar)
	add_child(_viewport)

	_output = SpoutOutput.new()
	_output.channel_name = "soak-" + str(OS.get_process_id())
	_output.texture = _viewport.get_texture()
	add_child(_output)

	_input = SpoutInput.new()
	_input.channel_name = channel
	_input.texture = Texture2DRD.new()
	_input.sender_connected.connect(func(_n): _counts["connects"] += 1)
	_input.sender_disconnected.connect(func(_n): _counts["disconnects"] += 1)
	_input.receive_error.connect(func(_m): _counts["errors"] += 1)
	add_child(_input)

	var preview := TextureRect.new()
	preview.texture = _input.texture
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	preview.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(preview)
	var stats := SpoutStatsOverlay.new()
	stats.outputs.append(_output)
	stats.inputs.append(_input)
	add_child(stats)

	var new_file := not FileAccess.file_exists("user://soak.csv")
	_file = FileAccess.open("user://soak.csv", FileAccess.READ_WRITE if not new_file else FileAccess.WRITE)
	_file.seek_end()
	if new_file:
		_file.store_line("time_s,fps,frame_ms_p99,video_mem_mb,static_mem_mb,frames_sent,frames_skipped,frames_received,connects,disconnects,errors")
	print("Soak log: ", ProjectSettings.globalize_path("user://soak.csv"))


func _process(delta: float) -> void:
	_anim += 1
	_bar.position.x = float((_anim * 8) % resolution.x)
	var now := Time.get_ticks_usec()
	if _last_usec != 0:
		_frame_ms.append((now - _last_usec) / 1000.0)
	_last_usec = now
	_since_log += delta
	if _since_log >= log_every:
		_since_log = 0.0
		_write_line()
	if _quit_after > 0.0 and Time.get_ticks_msec() / 1000.0 > _quit_after:
		_write_line()
		get_tree().quit()


func _write_line() -> void:
	var sorted := _frame_ms.duplicate()
	sorted.sort()
	var p99 := sorted[int(sorted.size() * 0.99)] if sorted.size() > 0 else 0.0
	_frame_ms.clear()
	var vmem := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0
	var smem := OS.get_static_memory_usage() / 1048576.0
	var line := "%.0f,%d,%.2f,%.1f,%.1f,%d,%d,%d,%d,%d,%d" % [
		Time.get_ticks_msec() / 1000.0, Engine.get_frames_per_second(), p99, vmem, smem,
		_output.get_frames_sent(), _output.get_frames_skipped(), _input.get_frames_received(),
		_counts["connects"], _counts["disconnects"], _counts["errors"]]
	_file.store_line(line)
	_file.flush()
	print("SOAK ", line)
