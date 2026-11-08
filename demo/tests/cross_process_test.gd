extends SceneTree
## Two-process Spout test. Start one process with --role=send and one with
## --role=recv on the same --channel. The sender fills its viewport with a
## colour whose red channel counts frames (mod 250, values 3..252). The
## receiver checks that what it gets keeps advancing: new frames arrive, no
## garbage, no long freezes.
##
## Usage:
##   godot --path demo [--rendering-driver d3d12|vulkan] [--gpu-index N] \
##       -s res://tests/cross_process_test.gd -- --role=send|recv \
##       --channel=NAME [--seconds=20] [--size=1920x1080] [--json=path]
## Prints "CROSS_RESULT <json>". Receiver exit code 0 = pass.

var role := "send"
var channel := "gs-cross"
var seconds := 20.0
var size := Vector2i(640, 360)
var json_path := ""

var vp: SubViewport
var rect: ColorRect
var node: Node
var start_msec := 0
var frame := 0
var last := -1
var stats := {
	"frames": 0, "received": 0, "advanced": 0, "repeated": 0, "garbage": 0,
	"longest_freeze_frames": 0, "connected_after_ms": -1, "state_at_end": -1,
	"errors": [],
}
var freeze := 0


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--role="):
			role = arg.get_slice("=", 1)
		elif arg.begins_with("--channel="):
			channel = arg.get_slice("=", 1)
		elif arg.begins_with("--seconds="):
			seconds = float(arg.get_slice("=", 1))
		elif arg.begins_with("--size="):
			var p := arg.get_slice("=", 1).split("x")
			size = Vector2i(int(p[0]), int(p[1]))
		elif arg.begins_with("--json="):
			json_path = arg.get_slice("=", 1)
	start_msec = Time.get_ticks_msec()
	if role == "send":
		vp = SubViewport.new()
		vp.size = size
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		rect = ColorRect.new()
		rect.size = size
		vp.add_child(rect)
		root.add_child(vp)
		node = ClassDB.instantiate("SpoutOutput")
		node.set("channel_name", channel)
		node.set("texture", vp.get_texture())
	else:
		node = ClassDB.instantiate("SpoutInput")
		node.set("channel_name", channel)
		node.set("texture", Texture2DRD.new())
		node.connect("receive_error", func(m): stats["errors"].append(m))
	root.add_child(node)


func _process(_delta: float) -> bool:
	frame += 1
	var elapsed := (Time.get_ticks_msec() - start_msec) / 1000.0
	if role == "send":
		var v := frame % 250 + 3
		rect.color = Color8(v, 0, 255 - v, 255)
	else:
		_sample()
	if elapsed >= seconds:
		return _finish()
	return false


func _sample() -> void:
	stats["frames"] += 1
	if not node.call("is_sender_connected"):
		return
	if stats["connected_after_ms"] < 0:
		stats["connected_after_ms"] = Time.get_ticks_msec() - start_msec
	var tex: Texture2D = node.get("texture")
	if tex == null or tex.get_width() == 0:
		return
	var img := tex.get_image()
	if img == null or img.is_empty():
		return
	var c := img.get_pixel(img.get_width() / 2, img.get_height() / 2)
	var r := c.r8
	stats["received"] += 1
	# The blue channel is 255 - red in every frame the sender drew; anything
	# else is a torn, half-written or foreign frame.
	if absi(c.b8 - (255 - r)) > 3 or r < 3 or r > 252:
		stats["garbage"] += 1
		return
	if r == last:
		stats["repeated"] += 1
		freeze += 1
		stats["longest_freeze_frames"] = max(stats["longest_freeze_frames"], freeze)
	else:
		stats["advanced"] += 1
		freeze = 0
	last = r


func _finish() -> bool:
	stats["role"] = role
	stats["driver"] = RenderingServer.get_current_rendering_driver_name()
	stats["adapter"] = RenderingServer.get_video_adapter_name()
	stats["size"] = "%dx%d" % [size.x, size.y]
	var ok := true
	if role == "recv":
		stats["state_at_end"] = node.call("get_receiver_state")
		stats["sender_size"] = str(node.call("get_sender_size"))
		ok = stats["received"] > 0 and stats["garbage"] == 0 and stats["advanced"] > stats["received"] * 0.5
	else:
		stats["frames_sent"] = node.call("get_frames_sent")
		stats["frames_skipped"] = node.call("get_frames_skipped")
		stats["sender_name"] = node.call("get_active_sender_name")
	stats["pass"] = ok
	var line := JSON.stringify(stats)
	print("CROSS_RESULT ", line)
	if not json_path.is_empty():
		var f := FileAccess.open(json_path, FileAccess.WRITE)
		if f:
			f.store_string(line)
	quit(0 if ok else 1)
	return true
