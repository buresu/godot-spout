extends Control

@onready var color_rect: ColorRect = $SubViewportContainer/SubViewport/ColorRect

var _hue: float = 0.0

func _process(delta: float) -> void:
	_hue = fmod(_hue + delta * 0.2, 1.0)
	color_rect.color = Color.from_hsv(_hue, 0.8, 0.9)
