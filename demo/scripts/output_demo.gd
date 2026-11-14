extends Control

@onready var spout_output: SpoutOutput = $SpoutOutput
@onready var color_rect: ColorRect = $SubViewport/ColorRect
@onready var preview: TextureRect = $Preview
@onready var button_viewport: Button = $ButtonBar/ButtonViewport
@onready var button_icon: Button = $ButtonBar/ButtonIcon

var _icon_tex: Texture2D = preload("res://textures/icon.svg")
# A preloaded ViewportTexture resource isn't connected to this scene's viewport.
@onready var _viewport_tex: Texture2D = $SubViewport.get_texture()

var _hue: float = 0.0

func _ready() -> void:
	button_viewport.pressed.connect(_send.bind(_viewport_tex))
	button_icon.pressed.connect(_send.bind(_icon_tex))
	_send(_viewport_tex)

func _send(texture: Texture2D) -> void:
	spout_output.texture = texture
	preview.texture = texture

func _process(delta: float) -> void:
	_hue = fmod(_hue + delta * 0.2, 1.0)
	color_rect.color = Color.from_hsv(_hue, 0.8, 0.9)
