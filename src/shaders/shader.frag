#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 inFragW;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 4) in flat uint inTextureIndex;
layout(location = 5) in flat vec4 inMaterialBaseColor;
layout(location = 0) out vec4 fragColor;

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t lightsBufferAddress;
} frameConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];

struct Light
{
	vec3 position;
	vec3 color;
	float intensity;
	float range;
};

layout(buffer_reference, scalar) readonly buffer LightsPtr
{
    Light lights[];
};

void main()
{
    vec4 texColor = texture(textures[inTextureIndex], inUV);
    vec3 finalColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
    vec3 nNormal = normalize(inNormal);
	vec3 litColor = vec3(0);

	const int numPointLights = 3;
    LightsPtr lightsBuff = LightsPtr(frameConsts.lightsBufferAddress);

	// point lights
	for (int i = 0; i < numPointLights; ++i)
	{
		Light light = lightsBuff.lights[i];
		vec3 L = light.position - inFragW; // vector from fragment to light position
		float lightDistance = length(L);

		float inverseSquare = 1.0 / max(lightDistance * lightDistance, 0.0001);
		float window = pow(clamp(1.0 - pow(lightDistance / light.range, 4), 0, 1), 2);
		float attenuation = inverseSquare * window;

		float radiantIntensity = light.intensity / 683.0;
		L = L / lightDistance;
		float d = max(dot(nNormal, L), 0);
		litColor += finalColor * light.color * d * attenuation * radiantIntensity;
	}

    // ambient light
	vec3 ambient = vec3(0.005, 0.005, 0.005) * finalColor;

	litColor += ambient;
	fragColor = vec4(litColor, texColor.a);
}