extends Control
@onready var player = $player

func _ready():
	var stream:GAVStream = player.stream
	stream.finished.connect(func():print("done"))
	pass
	
func _process(_delta):
	pass
	# print(player.loop)
	# f not OS.has_environment("DESKTOP_SESSION"):
		#print("no desktop session set, will go fullscren")
		#DisplayServer.window_set_size(Vector2i(2160, 3840))
		#DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN)
		
func _input(event):
	if event is InputEventKey:
		if not event.pressed:
			if event.keycode == KEY_SPACE:
				player.set_paused(true)
			if event.keycode == KEY_P:
				player.play()
			if event.keycode == KEY_S:
				if player.is_playing():
					player.stop()
				else:
					player.play()
