#include "SpoutBlit.hpp"

#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace {

// %FORMAT% is replaced by the image format qualifier of the destination.
const char *SHADER_SOURCE = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_tex;
layout(set = 0, binding = 1, %FORMAT%) uniform restrict writeonly image2D dest_image;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	uint flags;
	uint pad;
} params;

const uint FLAG_FLIP_Y = 1u;
const uint FLAG_OPAQUE = 2u;
const uint FLAG_LINEAR_TO_SRGB = 4u;
const uint FLAG_SRGB_TO_LINEAR = 8u;

vec3 linear_to_srgb(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, c * 12.92, lessThan(c, vec3(0.0031308)));
}

vec3 srgb_to_linear(vec3 c) {
	return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThan(c, vec3(0.04045)));
}

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	// Sizes that aren't multiples of 8 (e.g. 1001x333) dispatch extra threads.
	if (p.x >= params.size.x || p.y >= params.size.y) {
		return;
	}
	ivec2 s = p;
	if ((params.flags & FLAG_FLIP_Y) != 0u) {
		s.y = params.size.y - 1 - p.y;
	}
	vec4 c = texelFetch(source_tex, s, 0);
	if ((params.flags & FLAG_LINEAR_TO_SRGB) != 0u) {
		c.rgb = linear_to_srgb(c.rgb);
	} else if ((params.flags & FLAG_SRGB_TO_LINEAR) != 0u) {
		c.rgb = srgb_to_linear(c.rgb);
	}
	if ((params.flags & FLAG_OPAQUE) != 0u) {
		c.a = 1.0;
	}
	imageStore(dest_image, p, c);
}
)";

const char *format_qualifier(RenderingDevice::DataFormat p_format) {
  switch (p_format) {
  case RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM:
    return "rgba8";
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT:
    return "rgba16f";
  case RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT:
    return "rgba32f";
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_UNORM:
    return "rgba16";
  case RenderingDevice::DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
    return "rgb10_a2";
  default:
    // BGRA8 has no image format qualifier and isn't reliably storable.
    return nullptr;
  }
}

} // namespace

SpoutBlit::SpoutBlit(RenderingDevice *p_rd) : _rd(p_rd) {}

SpoutBlit::~SpoutBlit() { release(); }

bool SpoutBlit::can_store(RenderingDevice::DataFormat p_format) {
  return format_qualifier(p_format) != nullptr;
}

void SpoutBlit::release() {
  if (!_rd) {
    return;
  }
  if (_uniform_set.is_valid() && _rd->uniform_set_is_valid(_uniform_set)) {
    _rd->free_rid(_uniform_set);
  }
  _uniform_set = RID();
  for (auto &it : _pipelines) {
    // Freeing the shader also frees pipelines created from it.
    if (it.second.shader.is_valid()) {
      _rd->free_rid(it.second.shader);
    }
  }
  _pipelines.clear();
  if (_sampler.is_valid()) {
    _rd->free_rid(_sampler);
    _sampler = RID();
  }
}

const SpoutBlit::Pipeline *
SpoutBlit::get_pipeline(RenderingDevice::DataFormat p_format) {
  auto it = _pipelines.find(p_format);
  if (it != _pipelines.end()) {
    return it->second.pipeline.is_valid() ? &it->second : nullptr;
  }
  Pipeline &p = _pipelines[p_format]; // cached even on failure: no retry spam
  const char *qualifier = format_qualifier(p_format);
  if (!qualifier) {
    return nullptr;
  }
  Ref<RDShaderSource> source;
  source.instantiate();
  source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
  source->set_stage_source(
      RenderingDevice::SHADER_STAGE_COMPUTE,
      String(SHADER_SOURCE).replace("%FORMAT%", qualifier));
  Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(source);
  if (spirv.is_null()) {
    spout_error("Could not compile the processing shader.");
    return nullptr;
  }
  const String error =
      spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
  if (!error.is_empty()) {
    spout_error("Processing shader: " + error);
    return nullptr;
  }
  p.shader = _rd->shader_create_from_spirv(spirv, "godot-spout blit");
  if (!p.shader.is_valid()) {
    return nullptr;
  }
  p.pipeline = _rd->compute_pipeline_create(p.shader);
  if (!p.pipeline.is_valid()) {
    _rd->free_rid(p.shader);
    p.shader = RID();
    return nullptr;
  }
  return &p;
}

SpoutGodotAccess SpoutBlit::run(const RID &p_source, const RID &p_dest,
                                const SpoutBlitOptions &p_options) {
  Ref<RDTextureFormat> src = _rd->texture_get_format(p_source);
  Ref<RDTextureFormat> dst = _rd->texture_get_format(p_dest);
  if (src.is_null() || dst.is_null()) {
    return SPOUT_ACCESS_NONE;
  }
  const uint32_t width = dst->get_width();
  const uint32_t height = dst->get_height();
  const bool same_size =
      src->get_width() == width && src->get_height() == height;
  const auto src_usage = src->get_usage_bits();
  const auto dst_usage = dst->get_usage_bits();

  // A plain copy is cheapest and keeps the D3D12 states simple (see
  // SpoutD3D12Gpu.cpp), so use it whenever nothing has to change.
  const bool can_copy =
      !p_options.needs_shader() && same_size &&
      src->get_samples() == RenderingDevice::TEXTURE_SAMPLES_1 &&
      spout_rd_formats_copy_compatible(src->get_format(), dst->get_format()) &&
      (src_usage & RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT) &&
      (dst_usage & RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
  if (can_copy) {
    const Error err = _rd->texture_copy(p_source, p_dest, Vector3(), Vector3(),
                                        Vector3(width, height, 1), 0, 0, 0, 0);
    return err == OK ? SPOUT_ACCESS_COPY : SPOUT_ACCESS_NONE;
  }

  // Compute pass: needs a sampled source of the same size and a storable
  // destination.
  const Pipeline *pipeline = nullptr;
  if (same_size && (src_usage & RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT) &&
      (dst_usage & RenderingDevice::TEXTURE_USAGE_STORAGE_BIT)) {
    pipeline = get_pipeline(dst->get_format());
  }
  if (!pipeline) {
    if (!_warned) {
      spout_error("Can't transfer this texture (format " +
                  String::num_int64(src->get_format()) + " -> " +
                  String::num_int64(dst->get_format()) + ", " +
                  String::num_int64(src->get_width()) + "x" +
                  String::num_int64(src->get_height()) + " -> " +
                  String::num_int64(width) + "x" + String::num_int64(height) +
                  ").");
      _warned = true;
    }
    return SPOUT_ACCESS_NONE;
  }

  if (!_sampler.is_valid()) {
    Ref<RDSamplerState> state;
    state.instantiate(); // nearest filtering; texelFetch ignores it anyway
    _sampler = _rd->sampler_create(state);
  }

  // Uniform sets are freed by RD automatically when a texture they reference
  // is freed, so check validity rather than tracking frees ourselves.
  if (!_uniform_set.is_valid() || !_rd->uniform_set_is_valid(_uniform_set) ||
      _set_source != p_source || _set_dest != p_dest) {
    if (_uniform_set.is_valid() && _rd->uniform_set_is_valid(_uniform_set)) {
      _rd->free_rid(_uniform_set);
    }
    Ref<RDUniform> u_src;
    u_src.instantiate();
    u_src->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u_src->set_binding(0);
    u_src->add_id(_sampler);
    u_src->add_id(p_source);
    Ref<RDUniform> u_dst;
    u_dst.instantiate();
    u_dst->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    u_dst->set_binding(1);
    u_dst->add_id(p_dest);
    TypedArray<RDUniform> uniforms;
    uniforms.push_back(u_src);
    uniforms.push_back(u_dst);
    _uniform_set = _rd->uniform_set_create(uniforms, pipeline->shader, 0);
    _set_source = p_source;
    _set_dest = p_dest;
    if (!_uniform_set.is_valid()) {
      return SPOUT_ACCESS_NONE;
    }
  }

  uint32_t flags = 0;
  if (p_options.flip_y) {
    flags |= 1u;
  }
  if (p_options.force_opaque) {
    flags |= 2u;
  }
  if (p_options.color == SPOUT_COLOR_LINEAR_TO_SRGB) {
    flags |= 4u;
  } else if (p_options.color == SPOUT_COLOR_SRGB_TO_LINEAR) {
    flags |= 8u;
  }
  PackedByteArray push;
  push.resize(16);
  push.encode_s32(0, static_cast<int32_t>(width));
  push.encode_s32(4, static_cast<int32_t>(height));
  push.encode_u32(8, flags);
  push.encode_u32(12, 0);

  const int64_t list = _rd->compute_list_begin();
  _rd->compute_list_bind_compute_pipeline(list, pipeline->pipeline);
  _rd->compute_list_bind_uniform_set(list, _uniform_set, 0);
  _rd->compute_list_set_push_constant(list, push, push.size());
  _rd->compute_list_dispatch(list, (width + 7) / 8, (height + 7) / 8, 1);
  _rd->compute_list_end();
  return SPOUT_ACCESS_SHADER;
}
