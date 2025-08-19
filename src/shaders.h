//
// Created by phwhitfield on 18.08.25.
//

#pragma once

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/variant/string.hpp>

godot::RID yuv420(godot::RenderingDevice *rd, godot::Vector2i size);
godot::RID nv12(godot::RenderingDevice *rd, godot::Vector2i size);
