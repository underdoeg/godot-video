//
// Created by phwhitfield on 18.08.25.
//
#include <format>

#include "shaders.h"

#include <map>
#include <ranges>

using namespace godot;

struct ShaderCacheEntry {
	RenderingDevice *rd = nullptr;
	RID shader;
};

static std::map<String, ShaderCacheEntry> shader_cache;

RID compile_shader(RenderingDevice *rd, const String &glsl, const String &name) {
	if (shader_cache.contains(name)) {
		auto &entry = shader_cache.at(name);
		if (entry.rd == rd) {
			return entry.shader;
		}
	}

	auto src = memnew(RDShaderSource);
	src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, glsl);

	auto spirv = rd->shader_compile_spirv_from_source(src);
	auto shader = rd->shader_create_from_spirv(spirv, name);
	if (!shader.is_valid()) {
		UtilityFunctions::printerr(name, " shader compile error: ",
				spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE));
		return {};
	}
	UtilityFunctions::print("created shader ", name);

	// Store both the RenderingDevice pointer and shader RID
	shader_cache[name] = ShaderCacheEntry{ rd, shader };
	return shader;
}

void cleanup_shaders() {
	for (auto val : shader_cache | std::views::values) {
		val.rd->free_rid(val.shader);
	}
}

RID yuv420(RenderingDevice *rd, Vector2i size) {
	// static RID cache = {};
	// if (cache.is_valid()) {
	// 	UtilityFunctions::print("returning cached yuv420 shader");
	// 	return cache;
	// }

	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

		// A binding to the buffer we create in our script
		//layout(set = 0, binding = 0, std430) restrict buffer Y {
		//	float data[];
		//} y;

		layout (set=0, binding=0, rgba8) uniform image2D result;
		layout (set=0, binding=1, r8) readonly uniform image2D Y;
		layout (set=0, binding=2, r8) readonly uniform image2D U;
		layout (set=0, binding=3, r8) readonly uniform image2D V;

		// The code we want to execute in each invocation
		void main() {
			// gl_LocalInvocationID.xy
			ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

			ivec2 texel_half = texel / 2;

			float y = imageLoad(Y, texel).r;
			float u = imageLoad(U, texel_half).r;// - .5;
			float v = imageLoad(V, texel_half).r;// - .5;

			//float y = texture(Y, uv).r;
			//float u = texture(U, uv).g;
			//float v = texture(V, uv).b;

			mat3 color_matrix = mat3(
                1,   0,       1.402,
                1,  -0.344,  -0.714,
                1,   1.772,   0);

			vec3 rgb = vec3(y,u-.5,v-.5) * color_matrix;
			//vec3 rgb = vec3(y,u,v);

			imageStore(result, texel, vec4(rgb, 1));

			//y = 1.1643*(y-0.0625);

			//float r = y + 1.403 * v;
		    //float g = y - 0.344 * u - 0.714 * v;
			//float b = y + 1.770 * u;

			//float r = y + 1.596 * v;
			//float g = y - 0.813 * u - 0.391 * v;
			//float b = y + 2.018 * u;

			//float r = y + 1.13983 * v;
			//float g = y - 0.39465 * u - 0.58060 * v;
			//float b = y + 2.03211  * u;
			//imageStore(result, texel, vec4(r,g,b,1));
		}
	)";

	// Dictionary replace;
	// // replace.set("width", size.x);
	// // replace.set("height", size.y);
	// s = s.format(replace);
	return compile_shader(rd, s, "yuv420");
	// return cache;
}

RID yuv420p10le(RenderingDevice *rd, Vector2i size) {
	// static RID cache = {};
	// if (cache.is_valid()) {
	// 	UtilityFunctions::print("returning cached yuv420 shader");
	// 	return cache;
	// }

	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

		layout (set=0, binding=0, rgba8) uniform image2D result;
		layout (set=0, binding=1, r8) readonly uniform image2D Y;
		layout (set=0, binding=2, r8) readonly uniform image2D U;
		layout (set=0, binding=3, r8) readonly uniform image2D V;

		// The code we want to execute in each invocation
		void main() {
			// gl_LocalInvocationID.xy
			ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

			ivec2 texel_half = texel / 2;
			ivec2 texel_u = texel_half;
			ivec2 texel_v = texel_half;

			float y = imageLoad(Y, texel).r;
			float u = imageLoad(U, texel_u).r;
			float v = imageLoad(V, texel_v).r;


			mat3 color_matrix = mat3(
				1,   0,       1.402,
				1,  -0.344,  -0.714,
				1,   1.772,   0
			);

			vec3 rgb = vec3(y,u-.5,v-.5) * color_matrix;
			//vec3 rgb = vec3(u, u, u);
			imageStore(result, texel, vec4(rgb, 1));
		}
	)";

	// Dictionary replace;
	// // replace.set("width", size.x);
	// // replace.set("height", size.y);
	// s = s.format(replace);
	return compile_shader(rd, s, "yuv420p10le");
	// return cache;
}

RID nv12(RenderingDevice *rd, Vector2i size) {
	// static RID cache = {};
	// if (cache.is_valid()) {
	// 	UtilityFunctions::print("returning cached nv12 shader");
	// 	return cache;
	// }

	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

		layout (set=0, binding=0, rgba8) uniform image2D result;
		layout (set=0, binding=1, r8) readonly uniform image2D Y;
		layout (set=0, binding=2, r8) readonly uniform image2D UV;

		// The code we want to execute in each invocation
		void main() {
			// gl_LocalInvocationID.xy
			ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

			ivec2 texel_half = texel / 2;
			ivec2 texel_u = texel_half;
			ivec2 texel_v = texel_half;

			// uvuvuvuvuv layout
			texel_u.x = texel.x - texel.x % 2;
			texel_v.x = texel.x - texel.x % 2 + 1;

			// uuuuuuuvvvvvv layout
			//texel_v.x *= 2; // += texel_half.x - texel_half.x/2;
			//texel_v.x /= 2;
			//texel_v.x += int(texel.x * .75);
			//texel_v.x /= 4;
			//texel_v.x = texel.x;
			//texel_v.x /= 4;
			//texel_v.x += texel_half.x;
			//texel_v.x += 1920;



			float y = imageLoad(Y, texel).r;
			float u = imageLoad(UV, texel_u).r;
			float v = imageLoad(UV, texel_v).r;

			//float y = texture(Y, uv).r;
			//float u = texture(U, uv).g;
			//float v = texture(V, uv).b;

			//mat3 color_matrix = mat3(
			//	1,   0,       1.403,
			//	1,  -0.344,  -0.714,
			//	1,   1.770,   0
			//);

			mat3 color_matrix = mat3(
                1,   0,       1.13983,
                1,  -0.39465,  -0.58060,
                1,   2.03211,   0);

			//float r = y + 1.13983 * v;
			//float g = y - 0.39465 * u - 0.58060 * v;
			//float b = y + 2.03211  * u;

			vec3 rgb = vec3(y,u-.5,v-.5) * color_matrix;
			//vec3 rgb = vec3(y,y,y);
			//vec3 rgb = vec3(0, 1, 0);
			imageStore(result, texel, vec4(rgb, 1));
			//imageStore(result, texel, vec4(y,u,v, 1));
		}
	)";

	return compile_shader(rd, s, "nv12");
	// return cache;
}

godot::RID p010le(godot::RenderingDevice *rd, godot::Vector2i size) {
	// static RID cache = {};
	// if (cache.is_valid()) {
	// 	UtilityFunctions::print("returning cached p010le shader");
	// 	return cache;
	// }

	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

		layout (set=0, binding=0, rgba8) uniform image2D result;
		layout (set=0, binding=1, r16) readonly uniform image2D Y;
		layout (set=0, binding=2, r16) readonly uniform image2D UV;

		// The code we want to execute in each invocation
		void main() {
			// gl_LocalInvocationID.xy
			ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

			ivec2 texel_half = texel / 2;
			ivec2 texel_u = texel_half;
			ivec2 texel_v = texel_half;

			// uvuvuvuvuv layout
			texel_u.x = texel.x - texel.x % 2;
			texel_v.x = texel.x - texel.x % 2 + 1;

			float y = imageLoad(Y, texel).r;
			float u = imageLoad(UV, texel_u).r;
			float v = imageLoad(UV, texel_v).r;


			mat3 color_matrix = mat3(
				1,   0,       1.402,
				1,  -0.344,  -0.714,
				1,   1.772,   0
			);

			vec3 rgb = vec3(y,u-.5,v-.5) * color_matrix;
			imageStore(result, texel, vec4(rgb, 1));
		}
	)";

	return compile_shader(rd, s, "p010le");
	// return cache;
}

godot::RID p016le(godot::RenderingDevice *rd, godot::Vector2i size) {
	// static RID cache = {};
	// if (cache.is_valid()) {
	// 	UtilityFunctions::print("returning cached p016le shader");
	// 	return cache;
	// }

	String s = R"(
		#version 450

		// Invocations in the (x, y, z) dimension
		layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

		layout (set=0, binding=0, rgba8) uniform image2D result;
		layout (set=0, binding=1, r16) readonly uniform image2D Y;
		layout (set=0, binding=2, r16) readonly uniform image2D UV;

		// The code we want to execute in each invocation
		void main() {
			// gl_LocalInvocationID.xy
			ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

			//if(texel.x % 2 == 0 || texel.y % 2 == 0) {
			//	return;
			//}

			ivec2 texel_y = texel; // ivec2(texel.x*2, texel.y);
			ivec2 texel_half = ivec2(texel.x, texel.y/2);
			ivec2 texel_u = texel_half;
			ivec2 texel_v = texel_half;

			// uvuvuvuvuv layout
			texel_u.x = texel.x - texel.x % 2;
			texel_v.x = texel.x - texel.x % 2 + 1;

			float y = imageLoad(Y, texel_y).r;
			//float y2 = imageLoad(Y, texel_y + ivec2(1, 0)).r;


			float u = imageLoad(UV, texel_u).r;
			float v = imageLoad(UV, texel_v).r;


			mat3 color_matrix = mat3(
				1,   0,       1.402,
				1,  -0.344,  -0.714,
				1,   1.772,   0
			);

			vec3 rgb = vec3(y,u-.5,v-.5) * color_matrix;
			//vec3 rgb = vec3(y, y, y);
			imageStore(result, texel, vec4(rgb, 1));
		}
	)";

	// String s = R"(
	//
	// )";

	return compile_shader(rd, s, "p016le");
	// return cache;
}
