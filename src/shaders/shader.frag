#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 inFragW;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout (location = 4) in flat uint inMaterialIndex;
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

struct Material
{
    vec4 baseColor;
	float roughness;
    uint colorTextureIndex;
    uint metallicRoughnessTextureIndex;
};

layout(buffer_reference, scalar) readonly buffer MaterialPtr
{
    Material materials[];
};


struct Light
{
	uint type;
	vec3 position;
	vec3 color;
	vec3 direction;
	float intensity;
	float intensityFactor;
	float range;
	float innerConeAngle;
	float outerConeAngle;
};

struct ShadeInfo
{
	vec3 surfaceColor;
	vec4 texColor;
	float shinyness;
	float specularStrength;
	vec3 normal;
	vec3 viewDir;
	vec3 lightDir;
};

layout(buffer_reference, scalar) readonly buffer LightsPtr
{
    Light lights[];
};

float attenuate(float lightDistance, float range)
{
	float inverseSquare = 1.0 / max(pow(lightDistance, 2), 0.001);
	float window = pow(clamp(1.0 - pow(lightDistance / range, 4), 0, 1), 2);
	float attenuation = inverseSquare * window;
	return attenuation;
}

float specular(ShadeInfo shadeInfo)
{
	vec3 halfwayDir = normalize(shadeInfo.lightDir + shadeInfo.viewDir);
	float m = mix(2.0, 128.0, shadeInfo.shinyness);
	float spec = pow(max(dot(shadeInfo.normal, halfwayDir), 0), m);
	return spec;
}

void lightFragment(Light light, inout vec3 litFragment, ShadeInfo shadeInfo, Material material, float attenuation)
{
	float d = max(dot(shadeInfo.normal, shadeInfo.lightDir), 0);
	float intensity = light.intensity * light.intensityFactor / 683.0; // convert from physical units
	vec3 radiance = light.color * attenuation * intensity;
	float specularFactor = specular(shadeInfo);
	vec3 diffuse = shadeInfo.surfaceColor * radiance * d;
	vec3 specularColor = specularFactor * radiance * shadeInfo.specularStrength;
	litFragment += diffuse + specularColor;
}

vec3 PBRNeutralToneMapping( vec3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

void main()
{
    MaterialPtr matBuff = MaterialPtr(frameConsts.materialBufferAddress);
    Material material = matBuff.materials[inMaterialIndex];

	ShadeInfo shadeInfo;
    shadeInfo.texColor = texture(textures[material.colorTextureIndex], inUV);
	shadeInfo.surfaceColor = inColor * shadeInfo.texColor.rgb * material.baseColor.rgb;
	shadeInfo.normal = normalize(inNormal);
	shadeInfo.viewDir = normalize(frameConsts.camPosition - inFragW);
	vec3 ormColor = texture(textures[material.metallicRoughnessTextureIndex], inUV).rgb;
	float roughness = material.roughness * ormColor.g;
	shadeInfo.shinyness = 1.0 - roughness;
	shadeInfo.specularStrength = 2.0 * pow(shadeInfo.shinyness, 2.0);

	vec3 litColor = vec3(0);
    LightsPtr lightsBuff = LightsPtr(frameConsts.lightsBufferAddress);

	// directional lights
	uint lightIdx = 0;
	for (uint i = 0; i < frameConsts.numDirLights; ++i)
	{
		Light light = lightsBuff.lights[lightIdx + i];
		shadeInfo.lightDir = -light.direction;
		float directionalAtten = 1.0;
		lightFragment(light, litColor, shadeInfo, material, directionalAtten);
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
		lightFragment(light, litColor, shadeInfo, material, attenuation);
	}
	lightIdx += frameConsts.numPointLights;

	// spot lights
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
		lightFragment(light, litColor, shadeInfo, material, attenuation * cone);
	}
	lightIdx += frameConsts.numSpotLights;

    // ambient light
	litColor += vec3(0.01) * shadeInfo.surfaceColor;

	// exposure and tonemapping
	float exposure = 0.4;
	vec3 c = litColor * exposure;
	//vec3 tonemapped = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
	//vec3 tonemapped = c / (1.0 + c);
	vec3 tonemapped = PBRNeutralToneMapping(c);

	fragColor = vec4(tonemapped, shadeInfo.texColor.a);
}