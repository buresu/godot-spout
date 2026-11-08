extends SceneTree
## Checks that a received texture shows the same colour in a 3D material
## (albedo uses source_color, i.e. Godot's sRGB view of the texture) as in a
## TextureRect (2D, raw values). Unshaded material, linear tonemapper, so the
## 3D path should give back the sent value.
##
## Usage: godot --path demo --rendering-driver d3d12 -s res://tests/srgb_material_test.gd
## Prints "SRGB_TEST <json>"; exit 0 = pass.

const VALUE := 200 # 8-bit sRGB-encoded red sent through Spout

var src: SubViewport
var recv: Node
var view2d: SubViewport
var view3d: SubViewport
var frames := 0


func _initialize() -> void:
	var tag := str(OS.get_process_id())
	src = SubViewport.new()
	src.size = Vector2i(32, 32)
	src.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var rect := ColorRect.new()
	rect.size = Vector2(32, 32)
	rect.color = Color8(VALUE, 60, 20, 255)
	src.add_child(rect)
	root.add_child(src)

	var sender: Node = ClassDB.instantiate("SpoutOutput")
	sender.set("channel_name", "gs-srgb-" + tag)
	sender.set("texture", src.get_texture())
	root.add_child(sender)

	recv = ClassDB.instantiate("SpoutInput")
	recv.set("channel_name", "gs-srgb-" + tag)
	recv.set("texture", Texture2DRD.new())
	root.add_child(recv)

	# 2D: TextureRect samples raw values.
	view2d = SubViewport.new()
	view2d.size = Vector2i(32, 32)
	view2d.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var tr := TextureRect.new()
	tr.texture = recv.get("texture")
	tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	tr.size = Vector2(32, 32)
	view2d.add_child(tr)
	root.add_child(view2d)

	# 3D: unshaded quad filling the view, albedo = received texture.
	view3d = SubViewport.new()
	view3d.size = Vector2i(32, 32)
	view3d.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	view3d.own_world_3d = true
	var env := Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	var we := WorldEnvironment.new()
	we.environment = env
	view3d.add_child(we)
	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 1.0
	cam.position = Vector3(0, 0, 1)
	view3d.add_child(cam)
	var mesh := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(2, 2)
	mesh.mesh = quad
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_texture = recv.get("texture")
	mesh.material_override = mat
	view3d.add_child(mesh)
	root.add_child(view3d)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 60 and not (recv.call("is_sender_connected") and recv.call("get_frames_received") > 10 and frames > 30):
		return false
	var c2 := view2d.get_texture().get_image().get_pixel(16, 16)
	var c3 := view3d.get_texture().get_image().get_pixel(16, 16)
	var ok := absi(c2.r8 - VALUE) <= 2 and absi(c3.r8 - VALUE) <= 3
	var res := {
		"driver": RenderingServer.get_current_rendering_driver_name(),
		"sent": VALUE, "texture_rect": c2.r8, "material_3d": c3.r8,
		"pass": ok,
	}
	print("SRGB_TEST ", JSON.stringify(res))
	quit(0 if ok else 1)
	return true
