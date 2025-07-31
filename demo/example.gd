extends Node
@onready var av_video:AvVideo = $video


func _ready() -> void:
	av_video.play()
	$texture_rect.texture = av_video.get_texture()
