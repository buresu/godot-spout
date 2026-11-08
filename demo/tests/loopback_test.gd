extends SceneTree
## Spout loopback test: sends a SubViewport through SpoutOutput, receives it with
## SpoutInput in the same process, and checks that the received pixels follow
## what was sent.
##
## Every frame the source viewport is filled with a colour whose red channel
## encodes the frame number (mod 250). The received texture is drawn into a
## probe viewport and read back, so the test detects frozen, stale or blank
## frames and measures the lag in frames.
##
## Usage (from the repo root):
##   godot --path demo --rendering-driver d3d12 -s res://tests/loopback_test.gd -- \
##       [--frames=600] [--size=256x128] [--switch] [--resize] [--trace] [--json=path]
##
## Prints one line starting with "LOOPBACK_RESULT " followed by JSON, then quits.
## Exit code 0 = pass, 1 = fail.

const MAX_LAG := 8

var frames := 600
var size := Vector2i(256, 128)
var do_switch := false
var do_resize := false
var json_path := ""
var trace := false

var channel := ""
var src_vp: SubViewport
var src_rect: ColorRect
var probe_vp: SubViewport
var probe_rect: TextureRect
var sender: Node
var receiver: Node
var still_tex: ImageTexture

var frame := 0
var sent: Array[int] = []
var last_received := -1
var stats := {
	"frames": 0,
	"received_frames": 0,
	"matched": 0,
	"stale": 0,
	"blank": 0,
	"unexpected": 0,
	"first_match_frame": -1,
	"lag_histogram": {},
	"switch_ok": null,
	"resize_ok": null,
	"errors": [],
}
var phase := "run"
var phase_frame := 0


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--frames="):
			frames = int(arg.get_slice("=", 1))
		elif arg.begins_with("--size="):
			var parts := arg.get_slice("=", 1).split("x")
			size = Vector2i(int(parts[0]), int(parts[1]))
		elif arg == "--switch":
			do_switch = true
		elif arg == "--resize":
			do_resize = true
		elif arg == "--trace":
			trace = true
		elif arg.begins_with("--json="):
			json_path = arg.get_slice("=", 1)

	if not ClassDB.class_exists("SpoutOutput") or not ClassDB.class_exists("SpoutInput"):
		_finish_with_error("godot-spout extension not loaded")
		return

	channel = "gs-loopback-%d" % OS.get_process_id()

	src_vp = SubViewport.new()
	src_vp.size = size
	src_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	src_vp.transparent_bg = false
	src_rect = ColorRect.new()
	src_rect.size = size
	src_vp.add_child(src_rect)
	root.add_child(src_vp)

	var img := Image.create(size.x, size.y, false, Image.FORMAT_RGBA8)
	img.fill(Color8(200, 200, 200, 255))
	still_tex = ImageTexture.create_from_image(img)

	sender = ClassDB.instantiate("SpoutOutput")
	sender.set("channel_name", channel)
	sender.set("texture", src_vp.get_texture())
	root.add_child(sender)

	receiver = ClassDB.instantiate("SpoutInput")
	receiver.set("channel_name", channel)
	receiver.set("texture", Texture2DRD.new())
	root.add_child(receiver)

	probe_vp = SubViewport.new()
	probe_vp.size = size
	probe_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	probe_rect = TextureRect.new()
	probe_rect.texture = receiver.get("texture")
	probe_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	probe_rect.stretch_mode = TextureRect.STRETCH_SCALE
	probe_rect.size = size
	probe_vp.add_child(probe_rect)
	root.add_child(probe_vp)


func _process(_delta: float) -> bool:
	if phase == "done":
		return true
	frame += 1
	phase_frame += 1
	var v := frame % 250 + 3
	src_rect.color = Color8(v, 255 - v, 128, 255)
	sent.append(v)

	var r := _read_probe()
	if trace:
		print("TRACE frame=%d sent=%d recv=%d phase=%s" % [frame, v, r, phase])
	_score(r)

	# Optional phases after the main run.
	if phase == "run" and frame >= frames:
		if do_switch:
			_enter_phase("switch_still")
			sender.set("texture", still_tex)
		elif do_resize:
			_start_resize()
		else:
			return _finish()
	elif phase == "switch_still" and phase_frame >= 30:
		stats["switch_ok"] = (r == 200)
		_enter_phase("switch_back")
		sender.set("texture", src_vp.get_texture())
	elif phase == "switch_back" and phase_frame >= 30:
		stats["switch_ok"] = stats["switch_ok"] and _recent_match()
		if do_resize:
			_start_resize()
		else:
			return _finish()
	elif phase == "resize" and phase_frame >= 30:
		var tex: Texture2D = receiver.get("texture")
		stats["resize_ok"] = tex != null and tex.get_size() == Vector2(size) and _recent_match()
		return _finish()
	return false


func _start_resize() -> void:
	_enter_phase("resize")
	size = Vector2i(size.x * 2 + 2, size.y + 6)
	src_vp.size = size
	src_rect.size = size
	probe_vp.size = size
	probe_rect.size = size


func _enter_phase(p: String) -> void:
	phase = p
	phase_frame = 0
	last_received = -1


var _recent := []


func _recent_match() -> bool:
	# True if most of the last 10 frames matched a recently sent value.
	var ok := 0
	for m in _recent.slice(-10):
		if m:
			ok += 1
	return ok >= 8


func _read_probe() -> int:
	var img := probe_vp.get_texture().get_image()
	if img == null or img.is_empty():
		return -1
	var c := img.get_pixel(img.get_width() / 2, img.get_height() / 2)
	return c.r8


func _score(r: int) -> void:
	stats["frames"] += 1
	if r <= 0:
		stats["blank"] += 1
		_recent.append(false)
		return
	stats["received_frames"] += 1
	if phase == "switch_still":
		_recent.append(r == 200)
		return
	var lag := -1
	for k in range(0, MAX_LAG + 1):
		var idx := sent.size() - 1 - k
		if idx < 0:
			break
		if sent[idx] == r:
			lag = k
			break
	if lag < 0:
		# Before the first match the receiver is still connecting (it polls for
		# the sender every 250 ms); only count mismatches after that.
		if stats["first_match_frame"] >= 0 and phase == "run":
			stats["unexpected"] += 1
		_recent.append(false)
	else:
		if r == last_received:
			stats["stale"] += 1
		else:
			stats["matched"] += 1
		if stats["first_match_frame"] < 0:
			stats["first_match_frame"] = frame
		var key := str(lag)
		stats["lag_histogram"][key] = stats["lag_histogram"].get(key, 0) + 1
		_recent.append(true)
	last_received = r


func _finish() -> bool:
	var pass_ok: bool = (
		stats["matched"] > (stats["frames"] - max(stats["first_match_frame"], 0)) * 0.8
		and stats["unexpected"] == 0
		and (stats["switch_ok"] == null or stats["switch_ok"])
		and (stats["resize_ok"] == null or stats["resize_ok"])
	)
	stats["pass"] = pass_ok
	stats["driver"] = RenderingServer.get_current_rendering_driver_name()
	stats["adapter"] = RenderingServer.get_video_adapter_name()
	stats["size"] = "%dx%d" % [size.x, size.y]
	var line := JSON.stringify(stats)
	print("LOOPBACK_RESULT ", line)
	if not json_path.is_empty():
		var f := FileAccess.open(json_path, FileAccess.WRITE)
		if f:
			f.store_string(line)
	phase = "done"
	_cleanup()
	quit(0 if pass_ok else 1)
	return true


func _finish_with_error(msg: String) -> void:
	stats["errors"].append(msg)
	stats["pass"] = false
	print("LOOPBACK_RESULT ", JSON.stringify(stats))
	phase = "done"
	quit(1)


func _cleanup() -> void:
	for n in [probe_vp, receiver, sender, src_vp]:
		if is_instance_valid(n):
			n.queue_free()
