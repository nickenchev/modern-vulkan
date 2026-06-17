#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in flat uint inTextureIndex;
layout(location = 0) out vec4 fragColor;

layout(push_constant, scalar) uniform DrawConstants
{
    mat4x4 wvp;
    uint64_t vertexAddress;
    uint64_t materialAddress;
    uint materialIndex;
} drawConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main()
{
    vec4 s = texture(textures[inTextureIndex], inUV);
	fragColor = s;
}