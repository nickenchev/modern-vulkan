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
	uint numDirLights;
	uint numPointLights;
	uint numSpotLights;
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

struct ShadeInfo
{
	vec3 surfaceColor;
	vec3 normal;
	vec3 viewDir;
	vec3 lightDir;
	float roughness;
};

layout(buffer_reference, scalar) readonly buffer LightsPtr
{
    Light lights[];
};

float attenuate(float lightDistance, float range)
{
	const float sourceRadius = 0.03; // 3cm light radius
	float inverseSquare = 1.0 / max(pow(lightDistance, 2), pow(sourceRadius, 2));
	float window = pow(clamp(1.0 - pow(lightDistance / range, 4), 0, 1), 2);
	float attenuation = inverseSquare * window;
	return attenuation;
}

float specular(ShadeInfo shadeInfo)
{
	vec3 halfwayDir = normalize(shadeInfo.lightDir + shadeInfo.viewDir);
	float a = pow(shadeInfo.roughness, 2);
	float specStr = clamp(2.0 / (a * a) - 2.0, 4, 4096);
	float spec = pow(max(dot(shadeInfo.normal, halfwayDir), 0.0), specStr) * (specStr + 8) / 8 * 0.04;
	return spec;
}

void lightFragment(Light light, inout vec3 litFragment, ShadeInfo shadeInfo, float attenuation)
{
	float d = max(dot(shadeInfo.normal, shadeInfo.lightDir), 0);
	float radiantIntensity = light.intensity / 683.0;
	vec3 radiance = light.color * attenuation * radiantIntensity;
	litFragment += shadeInfo.surfaceColor * radiance * d;
	litFragment += specular(shadeInfo) * d * radiance;
}

vec3 pbrNeutralToneMapping(vec3 color)
{
    const float ks = 0.8 - 0.04;
    const float kd = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < ks) return color;

    float d = 1.0 - ks;
    float newPeak = 1.0 - d * d / (peak + d - ks);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (kd * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

void main()
{
    vec4 texColor = texture(textures[inColorTexIdx], inUV);
    vec4 ormColor = texture(textures[inRoughTexIdx], inUV);
	float exposure = 0.8;
	vec3 litColor = vec3(0);

	ShadeInfo shadeInfo;
	shadeInfo.surfaceColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
	shadeInfo.normal = normalize(inNormal);
	shadeInfo.viewDir = normalize(frameConsts.camPosition - inFragW);
	shadeInfo.roughness = clamp(ormColor.g * inRoughness, 0.01, 1.0);

    LightsPtr lightsBuff = LightsPtr(frameConsts.lightsBufferAddress);

	// directional lights
	uint lightIdx = 0;
	for (uint i = 0; i < frameConsts.numDirLights; ++i)
	{
		Light light = lightsBuff.lights[lightIdx + i];
		shadeInfo.lightDir = -light.direction;
		lightFragment(light, litColor, shadeInfo, 1);
	}
	lightIdx += frameConsts.numDirLights;

	// point lights
	for (uint i = 0; i < frameConsts.numPointLights; ++i)
	{
		Light light = lightsBuff.lights[lightIdx + i];
		vec3 L = light.position - inFragW; // vector from fragment to light position
		float lightDistance = length(L);
		L = L / lightDistance;
		shadeInfo.lightDir = L;
		float attenuation = attenuate(lightDistance, light.range);
		lightFragment(light, litColor, shadeInfo, attenuation);
	}
	lightIdx += frameConsts.numPointLights;

	for (uint i = 0; i < frameConsts.numSpotLights; ++i)
	{
		Light light = lightsBuff.lights[lightIdx + i];
		vec3 L = light.position - inFragW; // vector from fragment to light position
		float lightDistance = length(L);
		L = L / lightDistance;
		shadeInfo.lightDir = L;

		float attenuation = attenuate(lightDistance, light.range);
		float cosS = dot(-L, light.direction);
		float cosU = cos(light.outerConeAngle);
		float cosP = cos(light.innerConeAngle);
		float t = clamp((cosS - cosU) / (cosP - cosU), 0, 1);
		float cone = t * t * (3 - 2 * t);
		lightFragment(light, litColor, shadeInfo, attenuation * cone);
	}
	lightIdx += frameConsts.numSpotLights;

    // ambient light
	vec3 ambient = vec3(0.01, 0.01, 0.01) * shadeInfo.surfaceColor;
	litColor += ambient;

	// tone mapping
	vec3 c = litColor * exposure;
	vec3 tonemapped = pbrNeutralToneMapping(c);

	fragColor = vec4(tonemapped, texColor.a);
}