extends Node
#@onready var av_video:AvVideo = $video

@onready var dynamic_video:GAVStream = GAVStream.new()

func _ready() -> void:
	dynamic_video.file = "res://input.mp4"
#
	$video_dynamic.stream = dynamic_video
	$video_dynamic.play()
	
	$video_tex.texture = $video.get_video_texture()
	$video_tex2.texture = $video.get_video_texture()
