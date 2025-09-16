extends Control
#@onready var av_video:AvVideo = $video

@onready var dynamic_video:GAVStream = GAVStream.new()
@onready var video = %video

func _ready() -> void:
	dynamic_video.file = "res://Andrea_MASTER.mp4"
	%video_dynamic.stream = dynamic_video
	%video_dynamic.loop = true
	%video_dynamic.play()
	
	dynamic_video.finished.connect(func():print("done"))
	
	%video_tex.texture = %video.get_video_texture()
	%video_tex2.texture = %video.get_video_texture()
		#
	%video_dynamic2.stream = dynamic_video
	
	%video_dynamic2.play()
	
	_on_timer_timeout()
	pass

func _process(delta):
	if video:
		$seconds.text = str(video.stream_position) + "s"

func load_many():
	for child in $container.get_children():
		child.queue_free()
		
	#await get_tree().process_frame
	#await get_tree().process_frame
	#await get_tree().process_frame
	
	$container.columns = 10
	for i in range(30):
		var player = VideoStreamPlayer.new()
		player.size = Vector2(20, 20)
		$container.add_child(player)
		player.stream = load("res://Adrian_Loop_360.mov")
		player.play()

func _on_timer_timeout():
	$Timer.wait_time = randf_range(3, 12)
	var random_rect = Rect2(randf_range(-size.x*.5, size.x*.5), randf_range(-size.y*.5, size.y*.5), randf_range(0, size.x), randf_range(0, size.y))
	create_tween().tween_property(%video_tex2, "position", random_rect.position, $Timer.wait_time)
	create_tween().tween_property(%video_tex2, "size", random_rect.size, $Timer.wait_time)

func _input(event):
	if event is InputEventKey:
		if not event.pressed:
			if event.keycode == KEY_R:
				%video.stream = load("res://Adrian_Loop_360.mov")
				%video.play()
				%video_tex.texture = %video.get_video_texture()
				%video_tex2.texture = %video.get_video_texture()
			if event.keycode == KEY_C:
				%video.stream.file = "res://8k_h265.mp4"
				%video.play()

				# loading a new video, creates a new texture
				%video_tex.texture = %video.get_video_texture()
				%video_tex2.texture = %video.get_video_texture()
			if event.keycode == KEY_SPACE:
				%video.play()
			if event.keycode == KEY_P:
				print(%video.stream_position)
			if event.keycode == KEY_F:
				load_many()
			if event.keycode == KEY_S:
				if %video.is_playing():
					%video.stop()
				else:
					%video.play()
