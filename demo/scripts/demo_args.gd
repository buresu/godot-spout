class_name SpoutDemoArgs
## Command-line overrides for the demo scenes, after "--":
##   --resolution=7680x1080   --channel=Walls   --seconds=60


static func get_value(key: String, default: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--%s=" % key):
			return arg.get_slice("=", 1)
	return default


static func get_resolution(default: Vector2i) -> Vector2i:
	var v := get_value("resolution", "")
	if v.is_empty() or not "x" in v:
		return default
	return Vector2i(int(v.get_slice("x", 0)), int(v.get_slice("x", 1)))
