extends Control

@onready var texture_rect: TextureRect = $TextureRect
@onready var label: Label = $Label

func _process(_delta: float) -> void:
	var tex := texture_rect.texture as GDSpoutTexture
	if tex and tex.get_width() > 0:
		label.text = "Receiving: %s (%dx%d)" % [
			tex.channel_name if not tex.channel_name.is_empty() else "auto",
			tex.get_width(),
			tex.get_height()
		]
	else:
		label.text = "Waiting for sender..."
