tool
extends EditorPlugin

func _enter_tree():
	#add_custom_type("TestTexture", "ViewportTexture", preload("res://addons/SpoutPlugin/SpoutTexture.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	add_custom_type("SpoutSender", "Node", preload("res://addons/SpoutPlugin/GDSpoutSender.gdns"), preload("res://addons/SpoutPlugin/icon.png"))
	pass

func _exit_tree():
	#remove_custom_type("TestTexture")
	remove_custom_type("SpoutSender")
	pass
