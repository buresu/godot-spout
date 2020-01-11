tool
extends EditorPlugin

func _enter_tree():
	#add_custom_type("SpoutTexture", "Texture", preload("res://addons/SpoutPlugin/GDSpoutTexture.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	add_custom_type("SpoutSender", "Node", preload("res://addons/SpoutPlugin/GDSpoutSender.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	pass

func _exit_tree():
	#remove_custom_type("SpoutTexture")
	remove_custom_type("SpoutSender")
	pass
