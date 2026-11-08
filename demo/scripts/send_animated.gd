extends Control
## Sends an animated Forward+ 3D scene (default 1920x1080; 7680x1080 for three
## projection walls) through Spout, with a 2D overlay that makes stale frames
## and tearing easy to spot: a frame counter and a bar that moves 8 px per
## frame. The window shows a scaled preview and stats.
##
## Overrides: godot --path demo res://scenes/send_animated.tscn -- \
##                --resolution=7680x1080 --channel=Walls

@export var resolution := Vector2i(1920, 1080)
@export var channel_name := "godot-spout"
@export var hdr := false

var _viewport: SubViewport
var _pivot: Node3D
var _frame_label: Label
var _bar: ColorRect
var _output: SpoutOutput
var _frame := 0


func _ready() -> void:
	resolution = SpoutDemoArgs.get_resolution(resolution)
	channel_name = SpoutDemoArgs.get_value("channel", channel_name)

	_viewport = SubViewport.new()
	_viewport.size = resolution
	_viewport.use_hdr_2d = hdr
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.own_world_3d = true
	add_child(_viewport)
	_build_3d()
	_build_overlay()

	_output = SpoutOutput.new()
	_output.channel_name = channel_name
	_output.texture = _viewport.get_texture()
	if hdr:
		# HDR 2D viewports are linear RGBA16F; receivers expect sRGB-encoded.
		_output.color_conversion = SpoutOutput.COLOR_LINEAR_TO_SRGB
	add_child(_output)

	var preview := TextureRect.new()
	preview.texture = _viewport.get_texture()
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	preview.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(preview)

	var stats := SpoutStatsOverlay.new()
	stats.outputs.append(_output)
	add_child(stats)


func _build_3d() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.02, 0.05)
	env.glow_enabled = true
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	_viewport.add_child(we)

	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.5, 9)
	cam.fov = 40
	_viewport.add_child(cam)
	cam.look_at(Vector3.ZERO)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	_viewport.add_child(sun)

	_pivot = Node3D.new()
	_viewport.add_child(_pivot)
	# A ring of meshes wide enough to fill a 32:9 or 64:9 frame.
	for i in 24:
		var m := MeshInstance3D.new()
		m.mesh = [BoxMesh.new(), SphereMesh.new(), TorusMesh.new()][i % 3]
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color.from_hsv(i / 24.0, 0.7, 1.0)
		mat.emission_enabled = i % 4 == 0
		mat.emission = mat.albedo_color
		mat.emission_energy_multiplier = 2.0
		m.material_override = mat
		var a := TAU * i / 24.0
		m.position = Vector3(cos(a) * 6.0, sin(a * 3.0) * 0.8, sin(a) * 2.0)
		_pivot.add_child(m)
		if i % 6 == 0:
			var light := OmniLight3D.new()
			light.light_color = mat.albedo_color
			light.omni_range = 3.0
			light.position = m.position + Vector3(0, 1, 0)
			_pivot.add_child(light)


func _build_overlay() -> void:
	var scale := resolution.y / 1080.0
	_bar = ColorRect.new()
	_bar.color = Color.WHITE
	_bar.size = Vector2(24 * scale, resolution.y)
	_viewport.add_child(_bar)
	_frame_label = Label.new()
	_frame_label.position = Vector2(40, 40) * scale
	_frame_label.add_theme_font_size_override("font_size", int(64 * scale))
	_viewport.add_child(_frame_label)


func _process(delta: float) -> void:
	_frame += 1
	_pivot.rotate_y(delta * 0.4)
	for i in _pivot.get_child_count():
		_pivot.get_child(i).rotate_x(delta * (0.5 + i * 0.05))
	# 8 px per frame: a stale or repeated frame shows as the bar not moving.
	_bar.position.x = float((_frame * 8) % resolution.x)
	_frame_label.text = "%s  frame %d  %dx%d" % [channel_name, _frame, resolution.x, resolution.y]
