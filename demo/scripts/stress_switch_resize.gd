extends Control
## Stress scene: every `interval` seconds it does the next action in a cycle:
## resize the source viewport (incl. 7680x1080 and odd sizes), switch the
## sender's source (viewport A, viewport B, a still image), rename the
## sender, toggle enabled, toggle the format, toggle flip. Two senders and
## two receivers run at the same time. Receiver 1 follows sender 1 by name
## (also across the renames), receiver 2 watches sender 2.
##
## Leave it running next to Resolume and check that Resolume keeps showing
## "stress-a" / "stress-b" through every step. The log and the stats show
## what happened.
##
## Overrides: -- --seconds=600 (quit after that, print a summary)

@export var interval := 3.0

const SIZES := [Vector2i(1920, 1080), Vector2i(7680, 1080), Vector2i(3840, 1080),
	Vector2i(1001, 333), Vector2i(1280, 720)]

var _vp_a: SubViewport
var _vp_b: SubViewport
var _still: ImageTexture
var _out1: SpoutOutput
var _out2: SpoutOutput
var _in1: SpoutInput
var _in2: SpoutInput
var _log: Label
var _lines := PackedStringArray()
var _step := 0
var _time := 0.0
var _quit_after := 0.0
var _events := {}
var _anim := 0


func _ready() -> void:
	_quit_after = float(SpoutDemoArgs.get_value("seconds", "0"))
	_vp_a = _make_viewport(Color(0.8, 0.2, 0.1))
	_vp_b = _make_viewport(Color(0.1, 0.3, 0.9))
	var img := Image.create(640, 360, false, Image.FORMAT_RGBA8)
	img.fill(Color(0.9, 0.9, 0.2))
	_still = ImageTexture.create_from_image(img)

	_out1 = _make_output("stress-a", _vp_a.get_texture())
	_out2 = _make_output("stress-b", _vp_b.get_texture())
	_in1 = _make_input("stress-a")
	_in2 = _make_input("stress-b")

	var grid := GridContainer.new()
	grid.columns = 2
	grid.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(grid)
	for t in [_in1.texture, _in2.texture]:
		var r := TextureRect.new()
		r.texture = t
		r.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		r.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		r.size_flags_vertical = Control.SIZE_EXPAND_FILL
		grid.add_child(r)

	_log = Label.new()
	_log.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_log.position = Vector2(8, -220)
	_log.add_theme_font_size_override("font_size", 13)
	add_child(_log)

	var stats := SpoutStatsOverlay.new()
	stats.outputs.append_array([_out1, _out2])
	stats.inputs.append_array([_in1, _in2])
	add_child(stats)


func _make_viewport(color: Color) -> SubViewport:
	var vp := SubViewport.new()
	vp.size = SIZES[0]
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var bg := ColorRect.new()
	bg.color = color
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	vp.add_child(bg)
	var bar := ColorRect.new()
	bar.name = "Bar"
	bar.color = Color.WHITE
	bar.size = Vector2(32, 4096)
	vp.add_child(bar)
	add_child(vp)
	return vp


func _make_output(name: String, tex: Texture2D) -> SpoutOutput:
	var o := SpoutOutput.new()
	o.channel_name = name
	o.texture = tex
	for s in ["sender_started", "sender_stopped"]:
		o.connect(s, func(n): _count(s, n))
	o.sender_resized.connect(func(n, size): _count("sender_resized", "%s %s" % [n, size]))
	add_child(o)
	return o


func _make_input(name: String) -> SpoutInput:
	var i := SpoutInput.new()
	i.channel_name = name
	i.texture = Texture2DRD.new()
	for s in ["sender_connected", "sender_disconnected", "receive_error"]:
		i.connect(s, func(n): _count(s, n))
	add_child(i)
	return i


func _count(kind: String, detail: String) -> void:
	_events[kind] = _events.get(kind, 0) + 1
	_say("%s %s" % [kind, detail])


func _say(text: String) -> void:
	_lines.append("[%6.1f] %s" % [Time.get_ticks_msec() / 1000.0, text])
	if _lines.size() > 12:
		_lines.remove_at(0)
	_log.text = "\n".join(_lines)


func _process(delta: float) -> void:
	_anim += 1
	for vp in [_vp_a, _vp_b]:
		vp.get_node("Bar").position.x = float((_anim * 8) % vp.size.x)
	_time += delta
	if _time >= interval:
		_time = 0.0
		_do_step()
	if _quit_after > 0.0 and Time.get_ticks_msec() / 1000.0 > _quit_after:
		print("STRESS_SUMMARY ", JSON.stringify({
			"steps": _step, "events": _events,
			"in1_connected": _in1.is_sender_connected(),
			"in2_connected": _in2.is_sender_connected(),
			"in1_frames": _in1.get_frames_received(),
			"in2_frames": _in2.get_frames_received(),
		}))
		get_tree().quit()


func _do_step() -> void:
	match _step % 7:
		0:
			var s: Vector2i = SIZES[(_step / 7) % SIZES.size()]
			_vp_a.size = s
			_say("resize A -> %dx%d" % [s.x, s.y])
		1:
			var sources := [_vp_a.get_texture(), _vp_b.get_texture(), _still]
			var names := ["viewport A", "viewport B", "still image"]
			var k := (_step / 7) % 3
			_out1.texture = sources[k]
			_say("sender 1 source -> " + names[k])
		2:
			var n := "stress-a" if _out1.channel_name != "stress-a" else "stress-a-renamed"
			_out1.channel_name = n
			_in1.channel_name = n
			_say("rename sender 1 -> " + n)
		3:
			_out2.enabled = not _out2.enabled
			_say("sender 2 enabled = %s" % _out2.enabled)
		4:
			_out1.format = SpoutOutput.FORMAT_RGBA16F if _out1.format != SpoutOutput.FORMAT_RGBA16F else SpoutOutput.FORMAT_AUTO
			_say("sender 1 format -> %s" % ["Auto", "RGBA8", "RGBA16F"][_out1.format])
		5:
			_out1.flip_y = not _out1.flip_y
			_say("sender 1 flip_y = %s" % _out1.flip_y)
		6:
			var s: Vector2i = SIZES[(_step / 7 + 2) % SIZES.size()]
			_vp_b.size = s
			_say("resize B -> %dx%d" % [s.x, s.y])
	_step += 1
