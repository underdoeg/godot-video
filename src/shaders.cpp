//
// Created by phwhitfield on 18.08.25.
//
#include <format>

#include "shaders.h"

using namespace godot;

RID yuv420(RenderingDevice *rd, Vector2i size) {
	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 2, local_size_y = 1, local_size_z = 1) in;

		// A binding to the buffer we create in our script
		layout(set = 0, binding = 0, std430) restrict buffer Y {
			float data[];
		} y;

		// The code we want to execute in each invocation
		void main() {
			// gl_GlobalInvocationID.x uniquely identifies this invocation across all work groups
			y.data[gl_GlobalInvocationID.x] *= 2.0;
		}
	)";

	s = s.format(size);

	auto src = memnew(RDShaderSource);
	src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, s);

	auto spirv = rd->shader_compile_spirv_from_source(src);
	auto shader = rd->shader_create_from_spirv(spirv, "avg_yuv420");
	if (!shader.is_valid()) {
		UtilityFunctions::printerr("YUV420 shader compile error: ",
				spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE));
	}
	return shader;
}