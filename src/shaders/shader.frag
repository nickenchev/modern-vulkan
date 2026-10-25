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
	float metallic;
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
	float range;
	float innerConeAngle;
	float outerConeAngle;
};

struct ShadeInfo
{
	vec3 surfaceColor;
	vec4 texColor;
	float roughness;
	float metallic;
	float shinyness;
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
	float inverseSquare = 1.0 / max(pow(lightDistance, 2), 0.01);
	float window = pow(clamp(1.0 - pow(lightDistance / range, 4), 0, 1), 2);
	float attenuation = inverseSquare * window;
	return attenuation;
}

vec3 specular(ShadeInfo shadeInfo)
{
	vec3 halfwayDir = normalize(shadeInfo.lightDir + shadeInfo.viewDir);
	float m = 256.0 * shadeInfo.shinyness;
	float spec = (m + 8.0) * pow(max(dot(shadeInfo.normal, halfwayDir), 0.0), m) / 8.0;

	vec3 r0 = mix(vec3(0.04), shadeInfo.surfaceColor, shadeInfo.metallic);
	float vdoth = max(dot(shadeInfo.viewDir, halfwayDir), 0.0);
	vec3 fresnel = r0 + (1.0 - r0) * pow(1.0 - vdoth, 5.0);
	return spec * fresnel * shadeInfo.shinyness;
}

void lightFragment(Light light, inout vec3 litFragment, ShadeInfo shadeInfo, Material material, float attenuation)
{
	float d = max(dot(shadeInfo.normal, shadeInfo.lightDir), 0);
	float intensity = light.intensity / 683.0;
	vec3 radiance = light.color * attenuation * intensity;
	litFragment += shadeInfo.surfaceColor * radiance * d * (1.0 - shadeInfo.metallic);
	litFragment += specular(shadeInfo) * d * radiance;
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
	shadeInfo.roughness = ormColor.g * material.roughness;
	shadeInfo.shinyness = 1.0 - shadeInfo.roughness;
	shadeInfo.metallic = ormColor.b * material.metallic;

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
	litColor += vec3(0.02, 0.02, 0.02) * shadeInfo.surfaceColor * (1.0 - shadeInfo.metallic);

	// exposure and tonemapping
	float exposure = 0.2;
	vec3 c = litColor * exposure;
	vec3 tonemapped = (c * (2.51 * c + 0.03)) / (c * (2.43*c + 0.59) + 0.14);

	fragColor = vec4(tonemapped, shadeInfo.texColor.a);
}