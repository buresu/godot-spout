extends SceneTree
## Functional test of the SpoutOutput / SpoutInput API in one process:
## signals, sender list, flip, alpha, colour conversion, RGBA16F, renaming,
## enable/disable, two sender/receiver pairs and a receiver without sender.
##
## Usage: godot --path demo --rendering-driver d3d12 -s res://tests/api_test.gd
## Prints "API_TEST <json>" (one entry per check) and exits 0 if all pass.

const SIZE := Vector2i(64, 64)
const STEP_TIMEOUT := 180 # frames

var tag := ""
var src_vp: SubViewport
var top_rect: ColorRect
var bottom_rect: ColorRect
var src2_vp: SubViewport
var sender: Node
var sender2: Node
var recv: Node
var recv2: Node
var recv_missing: Node
var events: Array[String] = []

var steps: Array[Callable] = []
var step := -1
var step_frame := 0
var step_state := {}
var results := {}


func _initialize() -> void:
	tag = str(OS.get_process_id())
	src_vp = _make_viewport(Color8(200, 10, 10, 255), Color8(10, 200, 10, 128))
	src_vp.transparent_bg = true
	top_rect = src_vp.get_child(0)
	bottom_rect = src_vp.get_child(1)
	src2_vp = _make_viewport(Color8(20, 20, 220, 255), Color8(20, 20, 220, 255))

	sender = _make_sender("gs-api-A-" + tag, src_vp)
	recv = _make_receiver("gs-api-A-" + tag)
	for s in ["sender_started", "sender_stopped"]:
		sender.connect(s, _on_event.bind(s))
	sender.connect("sender_resized", func(n, size): events.append("sender_resized:%s:%s" % [n, size]))
	for s in ["sender_connected", "sender_disconnected", "receive_error"]:
		recv.connect(s, _on_event.bind(s))

	steps = [
		_step_connect, _step_receiver_flip, _step_sender_flip, _step_alpha,
		_step_color_conversion, _step_rgba16f, _step_rename, _step_enable,
		_step_two_pairs, _step_missing_sender,
	]
	_next_step()


func _make_viewport(top: Color, bottom: Color) -> SubViewport:
	var vp := SubViewport.new()
	vp.size = SIZE
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var a := ColorRect.new()
	a.color = top
	a.size = Vector2(SIZE.x, SIZE.y / 2)
	vp.add_child(a)
	var b := ColorRect.new()
	b.color = bottom
	b.position = Vector2(0, SIZE.y / 2)
	b.size = Vector2(SIZE.x, SIZE.y / 2)
	vp.add_child(b)
	root.add_child(vp)
	return vp


func _make_sender(name: String, vp: SubViewport) -> Node:
	var s: Node = ClassDB.instantiate("SpoutOutput")
	s.set("channel_name", name)
	s.set("texture", vp.get_texture())
	root.add_child(s)
	return s


func _make_receiver(name: String) -> Node:
	var r: Node = ClassDB.instantiate("SpoutInput")
	r.set("channel_name", name)
	r.set("texture", Texture2DRD.new())
	root.add_child(r)
	return r


func _on_event(arg, kind: String) -> void:
	events.append("%s:%s" % [kind, str(arg)])


func _pixel(r: Node, y: int) -> Color:
	var tex: Texture2D = r.get("texture")
	if tex == null or tex.get_width() == 0:
		return Color(-1, -1, -1, -1)
	var img := tex.get_image()
	if img == null or img.is_empty():
		return Color(-1, -1, -1, -1)
	return img.get_pixel(img.get_width() / 2, clampi(y, 0, img.get_height() - 1))


func _near(c: Color, r8: int, g8: int, b8: int, tol := 4) -> bool:
	return absi(c.r8 - r8) <= tol and absi(c.g8 - g8) <= tol and absi(c.b8 - b8) <= tol


func _check(name: String, ok: bool, detail := "") -> void:
	results[name] = {"pass": ok, "detail": detail}


func _next_step() -> void:
	step += 1
	step_frame = 0
	step_state = {}


func _process(_delta: float) -> bool:
	if step >= steps.size():
		return _finish()
	step_frame += 1
	var done: bool = steps[step].call()
	if done:
		_next_step()
	elif step_frame > STEP_TIMEOUT:
		_check("step_%d_timeout" % step, false, "events=" + str(events))
		_next_step()
	return false


# Waits a few frames after an action so the 1-frame pipeline settles.
func _settled(frames := 8) -> bool:
	return step_frame >= frames


func _step_connect() -> bool:
	if not recv.call("is_sender_connected") or recv.call("get_frames_received") < 3:
		return false
	var name := "gs-api-A-" + tag
	_check("sender_started_signal", events.has("sender_started:" + name), str(events))
	_check("sender_connected_signal", events.has("sender_connected:" + name), str(events))
	var list: PackedStringArray = recv.call("get_sender_list")
	_check("sender_list", list.has(name), str(list))
	_check("active_sender_name", sender.call("get_active_sender_name") == name)
	_check("receiver_info", recv.call("get_sender_size") == SIZE and recv.call("get_sender_format") == "RGBA8",
		"%s %s" % [recv.call("get_sender_size"), recv.call("get_sender_format")])
	var top := _pixel(recv, 2)
	_check("pixels_top", _near(top, 200, 10, 10), str(top))
	_check("adapter_name", not str(sender.call("get_adapter_name")).is_empty(), sender.call("get_adapter_name"))
	return true


func _step_receiver_flip() -> bool:
	if step_frame == 1:
		recv.set("flip_y", true)
	if not _settled():
		return false
	var top := _pixel(recv, 2)
	# Bottom rect: (10,200,10) at alpha 128 over a transparent viewport is
	# stored premultiplied by Godot: (5,100,5,128).
	_check("receiver_flip_y", _near(top, 5, 100, 5, 6), str(top))
	recv.set("flip_y", false)
	return true


func _step_sender_flip() -> bool:
	if step_frame == 1:
		sender.set("flip_y", true)
	if not _settled():
		return false
	var top := _pixel(recv, 2)
	_check("sender_flip_y", _near(top, 5, 100, 5, 6), str(top))
	sender.set("flip_y", false)
	return true


func _step_alpha() -> bool:
	# Let the previous step's flip reset reach the receiver first.
	if step_frame == 10:
		step_state["before"] = _pixel(recv, SIZE.y - 2).a8
		sender.set("alpha_mode", 1) # ALPHA_OPAQUE
	if step_frame < 20:
		return false
	var a := _pixel(recv, SIZE.y - 2).a8
	_check("alpha_opaque", step_state["before"] < 250 and a == 255,
		"before=%d after=%d" % [step_state["before"], a])
	sender.set("alpha_mode", 0)
	return true


func _step_color_conversion() -> bool:
	if step_frame == 1:
		sender.set("color_conversion", 1) # COLOR_LINEAR_TO_SRGB
	if not _settled():
		return false
	# 200/255 = 0.784 linear -> 0.900 sRGB-encoded = 230
	var top := _pixel(recv, 2)
	_check("linear_to_srgb", absi(top.r8 - 230) <= 3, str(top.r8))
	sender.set("color_conversion", 0)
	return true


func _step_rgba16f() -> bool:
	if step_frame == 1:
		sender.set("format", 2) # FORMAT_RGBA16F
	if recv.call("get_sender_format") != "RGBA16F":
		return false
	if not step_state.has("seen"):
		step_state["seen"] = step_frame
	if step_frame - step_state["seen"] < 8:
		return false
	var top := _pixel(recv, 2)
	_check("rgba16f", _near(top, 200, 10, 10), "%s %s" % [recv.call("get_sender_format"), top])
	_check("resized_signal_on_format_change", events.any(func(e): return e.begins_with("sender_resized:")), str(events))
	sender.set("format", 0)
	return true


func _step_rename() -> bool:
	var old_name := "gs-api-A-" + tag
	var new_name := "gs-api-B-" + tag
	if step_frame == 1:
		events.clear()
		sender.set("channel_name", new_name)
	if step_frame < 20:
		return false
	if step_frame == 20:
		_check("rename_stop_start", events.has("sender_stopped:" + old_name) and events.has("sender_started:" + new_name), str(events))
		_check("rename_receiver_lost_old", events.has("sender_disconnected:" + old_name), str(events))
		var list: PackedStringArray = recv.call("get_sender_list")
		_check("rename_list", list.has(new_name) and not list.has(old_name), str(list))
		recv.set("channel_name", new_name)
		return false
	if not recv.call("is_sender_connected") or recv.call("get_sender_name") != new_name:
		return false
	_check("rename_receiver_follows", true)
	return true


func _step_enable() -> bool:
	if step_frame == 1:
		events.clear()
		sender.set("enabled", false)
	if step_frame < 20:
		return false
	if step_frame == 20:
		_check("disable_stops", not sender.call("is_sending") and events.any(func(e): return e.begins_with("sender_disconnected:")), str(events))
		sender.set("enabled", true)
		return false
	if not recv.call("is_sender_connected"):
		return false
	_check("enable_reconnects", true)
	return true


func _step_two_pairs() -> bool:
	if step_frame == 1:
		sender2 = _make_sender("gs-api-C-" + tag, src2_vp)
		recv2 = _make_receiver("gs-api-C-" + tag)
	if not recv2.call("is_sender_connected") or recv2.call("get_frames_received") < 3:
		return false
	if not _settled(30):
		return false
	var a := _pixel(recv, 2)
	var c := _pixel(recv2, 2)
	_check("two_pairs", _near(a, 200, 10, 10) and _near(c, 20, 20, 220), "A=%s C=%s" % [a, c])
	return true


func _step_missing_sender() -> bool:
	if step_frame == 1:
		recv_missing = _make_receiver("gs-api-missing-" + tag)
	if not _settled(40):
		return false
	_check("missing_sender_waits", recv_missing.call("get_receiver_state") == 1 and not recv_missing.call("is_sender_connected"),
		str(recv_missing.call("get_receiver_state")))
	return true


func _finish() -> bool:
	var all_ok := true
	for k in results:
		all_ok = all_ok and results[k]["pass"]
	results["_driver"] = RenderingServer.get_current_rendering_driver_name()
	results["_pass"] = all_ok
	print("API_TEST ", JSON.stringify(results))
	quit(0 if all_ok else 1)
	return true
