extends Control

@onready var spout_output: GDSpoutOutput = $GDSpoutOutput
@onready var color_rect: ColorRect = $SubViewportContainer/SubViewport/ColorRect
@onready var button_viewport: Button = $ButtonBar/ButtonViewport
@onready var button_icon: Button = $ButtonBar/ButtonIcon

var _icon_tex: Texture2D = preload("res://textures/icon.svg")
var _viewport_tex: Texture2D = preload("res://textures/viewport_texture.tres")

var _hue: float = 0.0

func _ready() -> void:
	button_viewport.pressed.connect(func(): spout_output.texture = _viewport_tex)
	button_icon.pressed.connect(func(): spout_output.texture = _icon_tex)

func _process(delta: float) -> void:
	_hue = fmod(_hue + delta * 0.2, 1.0)
	color_rect.color = Color.from_hsv(_hue, 0.8, 0.9)
