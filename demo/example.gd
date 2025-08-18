extends Control
#@onready var av_video:AvVideo = $video

@onready var dynamic_video:GAVStream = GAVStream.new()

func _ready() -> void:
	dynamic_video.file = "res://input.mp4"
#
	#%video_dynamic.stream = dynamic_video
	#%video_dynamic.play()
	
	%video_tex.texture = %video.get_video_texture()
	%video_tex2.texture = %video.get_video_texture()
	
	_on_timer_timeout()

func _on_timer_timeout():
	$Timer.wait_time = randf_range(3, 12)
	var random_rect = Rect2(randf_range(-size.x*.5, size.x*.5), randf_range(-size.y*.5, size.y*.5), randf_range(0, size.x), randf_range(0, size.y))
	create_tween().tween_property(%video_tex2, "position", random_rect.position, $Timer.wait_time)
	create_tween().tween_property(%video_tex2, "size", random_rect.size, $Timer.wait_time)
