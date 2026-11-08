extends Control
## Receives a Spout sender into a SpoutTexture used by a 3D material and a
## TextureRect. Pick the sender from the list (refreshed every second); the
## status line follows the receiver's signals.

var _texture: SpoutTexture
var _picker: OptionButton
var _status: Label
var _cube: MeshInstance3D


func _ready() -> void:
	_texture = SpoutTexture.new()
	_texture.channel_name = SpoutDemoArgs.get_value("channel", "")
	_texture.sender_connected.connect(func(n): _status.text = "connected: " + n)
	_texture.sender_disconnected.connect(func(n): _status.text = "sender gone: %s (waiting)" % n)
	_texture.receive_error.connect(func(m): _status.text = "error: " + m)

	var split := HBoxContainer.new()
	split.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(split)

	# Left: 3D material.
	var container := SubViewportContainer.new()
	container.stretch = true
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(container)
	var vp := SubViewport.new()
	vp.own_world_3d = true
	container.add_child(vp)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, 3)
	vp.add_child(cam)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-30, 30, 0)
	vp.add_child(light)
	_cube = MeshInstance3D.new()
	_cube.mesh = BoxMesh.new()
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = _texture # albedo uses the sRGB view (source_color)
	_cube.material_override = mat
	vp.add_child(_cube)

	# Right: the same texture in 2D.
	var rect := TextureRect.new()
	rect.texture = _texture
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(rect)

	var bar := HBoxContainer.new()
	bar.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	bar.offset_top = -40
	add_child(bar)
	_picker = OptionButton.new()
	_picker.custom_minimum_size = Vector2(320, 0)
	_picker.item_selected.connect(_on_pick)
	bar.add_child(_picker)
	_status = Label.new()
	_status.text = "waiting for a sender..."
	bar.add_child(_status)

	var stats := SpoutStatsOverlay.new()
	stats.inputs.append(_texture)
	add_child(stats)

	var timer := Timer.new()
	timer.wait_time = 1.0
	timer.autostart = true
	timer.timeout.connect(_refresh_list)
	add_child(timer)
	_refresh_list()


func _refresh_list() -> void:
	var names := SpoutTexture.get_sender_list()
	var current := _texture.channel_name
	_picker.clear()
	_picker.add_item("(active sender)")
	for n in names:
		_picker.add_item(n)
		if n == current:
			_picker.select(_picker.item_count - 1)


func _on_pick(index: int) -> void:
	_texture.channel_name = "" if index == 0 else _picker.get_item_text(index)


func _process(delta: float) -> void:
	_cube.rotate_y(delta * 0.5)
	_cube.rotate_x(delta * 0.3)
