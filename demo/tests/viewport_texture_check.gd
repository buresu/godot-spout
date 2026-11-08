extends SceneTree
## Checks the cause of known issue 1 (plan bug B12): the ViewportTexture that
## output_demo.gd preloads is not the scene's own (local-to-scene) copy, so it
## never resolves to the SubViewport and keeps pointing at a placeholder.
##
## Usage: godot --path demo -s res://tests/viewport_texture_check.gd
## Prints "VIEWPORT_TEXTURE_CHECK <json>" and quits.

var scene: Node
var frames := 0


func _initialize() -> void:
	scene = load("res://scenes/output_demo.tscn").instantiate()
	root.add_child(scene)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 5:
		return false
	var preloaded: Texture2D = load("res://textures/viewport_texture.tres")
	var in_scene: Texture2D = scene.get_node("SpoutOutput").get("texture")
	var vp_tex: Texture2D = scene.get_node("SubViewportContainer/SubViewport").get_texture()
	var result := {
		"same_resource": preloaded == in_scene,
		"preloaded": _describe(preloaded),
		"in_scene": _describe(in_scene),
		"subviewport_get_texture": _describe(vp_tex),
	}
	print("VIEWPORT_TEXTURE_CHECK ", JSON.stringify(result))
	quit(0)
	return true


func _describe(t: Texture2D) -> Dictionary:
	var rs_rid := t.get_rid()
	var rd_rid := RenderingServer.texture_get_rd_texture(rs_rid)
	var rd := RenderingServer.get_rendering_device()
	var d := {"size": str(t.get_size()), "rd_valid": rd_rid.is_valid()}
	if rd and rd_rid.is_valid():
		var fmt := rd.texture_get_format(rd_rid)
		d["rd_size"] = "%dx%d" % [fmt.width, fmt.height]
		d["rd_id"] = rd_rid.get_id()
	return d
