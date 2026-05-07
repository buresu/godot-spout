extends Control

@onready var spout_input: SpoutInput = $SpoutInput
@onready var texture_rect: TextureRect = $TextureRect
@onready var label: Label = $Label

func _ready() -> void:
	texture_rect.texture = spout_input.texture

func _process(_delta: float) -> void:
	var tex := spout_input.texture
	if tex and tex.get_width() > 0:
		var channel := spout_input.channel_name
		label.text = "Receiving: %s (%dx%d)" % [
			channel if not channel.is_empty() else "auto",
			tex.get_width(),
			tex.get_height()
		]
	else:
		label.text = "Waiting for sender..."
