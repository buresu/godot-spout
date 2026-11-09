extends SceneTree
## Lifecycle test: changes scenes repeatedly between the Spout demo scenes,
## then quits while a SpoutTexture is still referenced from a static variable
## (it outlives the scene tree). Checks video memory growth and
## that the process exits cleanly (exit code 0, no crash).
##
## Usage: godot --path demo --rendering-driver d3d12 -s res://tests/lifecycle_test.gd -- [--cycles=30]
## Prints "LIFECYCLE_RESULT <json>".

const SCENES := ["res://scenes/send_animated.tscn", "res://scenes/receive_material.tscn",
	"res://scenes/stress_switch_resize.tscn"]
const FRAMES_PER_SCENE := 40

static var held_texture: Texture2D

var cycles := 30
var cycle := 0
var frame := 0
var mem_samples: Array[float] = []


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--cycles="):
			cycles = int(arg.get_slice("=", 1))
	var t: Texture2D = ClassDB.instantiate("SpoutTexture")
	t.set("channel_name", "lifecycle-held")
	held_texture = t
	change_scene_to_file(SCENES[0])


func _process(_delta: float) -> bool:
	frame += 1
	if frame < FRAMES_PER_SCENE:
		return false
	frame = 0
	cycle += 1
	mem_samples.append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0)
	if cycle >= cycles:
		# Compare the same scene at the start and at the end of the run.
		var first := mem_samples[0]
		var last := mem_samples[mem_samples.size() - SCENES.size()] if mem_samples.size() > SCENES.size() else first
		var res := {
			"driver": RenderingServer.get_current_rendering_driver_name(),
			"cycles": cycle,
			"video_mem_mb_first": snappedf(first, 0.1),
			"video_mem_mb_same_scene_last": snappedf(last, 0.1),
			"video_mem_samples": mem_samples.map(func(v): return snappedf(v, 0.1)),
		}
		print("LIFECYCLE_RESULT ", JSON.stringify(res))
		quit(0)
		return true
	change_scene_to_file(SCENES[cycle % SCENES.size()])
	return false
