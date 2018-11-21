tool
extends EditorPlugin

func _enter_tree():
	#add_custom_type("TestTexture", "ViewportTexture", preload("res://addons/SpoutPlugin/SpoutTexture.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	#add_custom_type("SpoutViewport", "Viewport", preload("res://addons/SpoutPlugin/SpoutViewport.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	pass

func _exit_tree():
	#remove_custom_type("TestTexture")
	#remove_custom_type("SpoutViewport")
	pass
