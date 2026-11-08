class_name SpoutStatsOverlay
extends CanvasLayer
## On-screen stats for the demo scenes: fps, frame time, and for each
## SpoutOutput / SpoutInput / SpoutTexture it is given: name, size, format,
## frame counters, CPU time per send and the GPU in use.

var outputs: Array[SpoutOutput] = []
var inputs: Array[Object] = [] # SpoutInput or SpoutTexture

var _label: Label
var _cpu_samples: Dictionary = {}
var _frame_times := PackedFloat32Array()
var _last_usec := 0


func _ready() -> void:
	layer = 100
	var panel := PanelContainer.new()
	panel.position = Vector2(8, 8)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0.6)
	style.set_content_margin_all(8)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	_label = Label.new()
	_label.add_theme_font_size_override("font_size", 14)
	panel.add_child(_label)


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	if _last_usec != 0:
		_frame_times.append((now - _last_usec) / 1000.0)
		if _frame_times.size() > 120:
			_frame_times.remove_at(0)
	_last_usec = now
	for o in outputs:
		var a: Array = _cpu_samples.get(o, [])
		a.append(o.get_last_send_cpu_usec())
		if a.size() > 60:
			a.pop_front()
		_cpu_samples[o] = a
	if Engine.get_process_frames() % 10 == 0:
		_label.text = _text()


func _text() -> String:
	var lines := PackedStringArray()
	var worst := 0.0
	for t in _frame_times:
		worst = max(worst, t)
	lines.append("%s | %d fps | worst frame %.1f ms (last 2 s)" % [
		RenderingServer.get_current_rendering_driver_name(),
		Engine.get_frames_per_second(), worst])
	lines.append("GPU: %s" % RenderingServer.get_video_adapter_name())
	for o in outputs:
		var cpu: Array = _cpu_samples.get(o, [])
		var avg := 0.0
		for v in cpu:
			avg += v
		avg = avg / max(cpu.size(), 1)
		var size: Vector2i = o.get_sender_size()
		lines.append("OUT \"%s\" %s  %dx%d  sent %d  skipped %d  cpu %.0f us" % [
			o.get_active_sender_name(), "sending" if o.is_sending() else "idle",
			size.x, size.y, o.get_frames_sent(), o.get_frames_skipped(), avg])
	for i in inputs:
		var state: String = ["idle", "waiting", "connected", "error"][i.get_receiver_state()]
		var size: Vector2i = i.get_sender_size()
		lines.append("IN  \"%s\" %s  %dx%d %s  sender %.0f fps  received %d" % [
			i.get_sender_name() if i.is_sender_connected() else i.channel_name,
			state, size.x, size.y, i.get_sender_format(), i.get_sender_fps(),
			i.get_frames_received()])
		if i.get_receiver_state() == 3:
			lines.append("    " + i.get_last_error())
	return "\n".join(lines)
