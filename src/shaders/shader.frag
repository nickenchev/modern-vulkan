#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in flat uint inTextureIndex;
layout(location = 0) out vec4 fragColor;

layout(push_constant, scalar) uniform DrawConstants
{
    mat4x4 wvp;
    mat4x4 worldMatrix;
    uint64_t vertexAddress;
    uint64_t materialAddress;
    uint materialIndex;
} drawConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main()
{
    vec3 lightDirection = normalize(vec3(-1, -1, 0));
    float d = max(dot(normalize(inNormal), -lightDirection), 0);
    vec4 texColor = texture(textures[inTextureIndex], inUV) * vec4(inColor, 1);

    // two-tone ambient light
	vec3 skyColor = vec3(0.15, 0.18, 0.25);
	vec3 groundColor = vec3(0.05, 0.03, 0.02);
	float t = normalize(inNormal).y * 0.5 + 0.5;
	vec3 hemiAmbient = mix(groundColor, skyColor, t);

	vec3 litColor = texColor.rgb * d + texColor.rgb * hemiAmbient;
	fragColor = vec4(litColor, texColor.a);
}