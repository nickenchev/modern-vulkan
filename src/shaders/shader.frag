#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 inFragW;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout (location = 4) in flat uint inColorTexIdx;
layout (location = 5) in flat uint inRoughTexIdx;
layout (location = 6) in flat vec4 inMaterialBaseColor;
layout (location = 7) in flat float inRoughness;
layout(location = 0) out vec4 fragColor;

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t lightsBufferAddress;
    vec3 camPosition;
    vec3 camDirection;
	int numPointLights;
	int numSpotLights;
} frameConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];

struct Light
{
	uint type;
	vec3 position;
	vec3 color;
	vec3 direction;
	float intensity;
	float range;
	float innerConeAngle;
	float outerConeAngle;
};

layout(buffer_reference, scalar) readonly buffer LightsPtr
{
    Light lights[];
};

float attenuate(float lightDistance, float range)
{
	float inverseSquare = 1.0 / max(lightDistance * lightDistance, 0.0001);
	float window = pow(clamp(1.0 - pow(lightDistance / range, 4), 0, 1), 2);
	float attenuation = inverseSquare * window;
	return attenuation;
}

float specular(vec3 lightDir, vec3 viewDir, vec3 normal, float roughness, float matRoughness)
{
	vec3 halfwayDir = normalize(lightDir + viewDir);
	float rough = clamp(roughness * matRoughness, 0.05, 1.0);
	float a = rough * rough;
	float specStr = clamp(2.0 / (a * a) - 2.0, 4, 4096);
	float spec = pow(max(dot(normal, halfwayDir), 0.0), specStr) * (specStr + 8) / 8 * 0.04;
	return spec;
}

void main()
{
    vec4 texColor = texture(textures[inColorTexIdx], inUV);
    vec4 ormColor = texture(textures[inRoughTexIdx], inUV);
    vec3 finalColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
    vec3 nNormal = normalize(inNormal);
	vec3 nViewDir = normalize(frameConsts.camPosition - inFragW);
	vec3 litColor = vec3(0);
	float exposure = 0.7;
    LightsPtr lightsBuff = LightsPtr(frameConsts.lightsBufferAddress);

	// point lights
	int lightIdx = 0;
	for (; lightIdx < frameConsts.numPointLights; ++lightIdx)
	{
		Light light = lightsBuff.lights[lightIdx];
		vec3 L = light.position - inFragW; // vector from fragment to light position
		float lightDistance = length(L);
		float attenuation = attenuate(lightDistance, light.range);

		float radiantIntensity = light.intensity / 683.0;
		L = L / lightDistance;
		float d = max(dot(nNormal, L), 0);
		vec3 radiance = light.color * attenuation * radiantIntensity;
		litColor += finalColor * radiance * d;
		litColor += specular(L, nViewDir, nNormal, ormColor.g, inRoughness) * d * radiance;
	}

	int spotStartIdx = lightIdx;
	for (; lightIdx < spotStartIdx + frameConsts.numSpotLights; ++lightIdx)
	{
		Light light = lightsBuff.lights[lightIdx];
		vec3 L = light.position - inFragW; // vector from fragment to light position
		float lightDistance = length(L);
		float attenuation = attenuate(lightDistance, light.range);

		float radiantIntensity = light.intensity / 683.0;
		L = L / lightDistance;
		float d = max(dot(nNormal, L), 0);

		float cosS = dot(-L, light.direction);
		float cosU = cos(light.outerConeAngle);
		float cosP = cos(light.innerConeAngle);
		float t = clamp((cosS - cosU) / (cosP - cosU), 0, 1);
		float cone = t * t * (3 - 2 * t);

		vec3 radiance = light.color * attenuation * radiantIntensity * cone;
		litColor += finalColor * radiance * d;
		litColor += specular(L, nViewDir, nNormal, ormColor.g, inRoughness) * d * radiance;
	}

    // ambient light
	vec3 ambient = vec3(0.01, 0.01, 0.01) * finalColor;

	litColor += ambient;
	vec3 c = litColor * exposure;
	vec3 tonemapped = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
	fragColor = vec4(tonemapped, texColor.a);
}