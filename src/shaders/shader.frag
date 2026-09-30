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
layout(location = 6) in flat float inRoughness;
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

void main()
{
    vec4 texColor = texture(textures[inTextureIndex], inUV);
    vec3 finalColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
    vec3 nNormal = normalize(inNormal);
	vec3 nViewDir = normalize(frameConsts.camPosition - inFragW);
	vec3 litColor = vec3(0);
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

		// specular highlight
		vec3 rL = reflect(-L, nNormal);
		float spec = pow(max(dot(nViewDir, rL), 0.0), 32);
		float specularStr = 1.0 - inRoughness;
		litColor += specularStr * spec * d * radiance;
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
		float cone = 1;

		if (light.type == 1) // spotlight
		{
			float cosS = dot(-L, light.direction);
			float cosU = cos(light.outerConeAngle);
			float cosP = cos(light.innerConeAngle);
			float t = clamp((cosS - cosU) / (cosP - cosU), 0, 1);
			cone = t * t * (3 - 2 * t);
		}
		vec3 radiance = light.color * attenuation * radiantIntensity * cone;
		litColor += finalColor * radiance * d;

		// specular highlight
		vec3 rL = reflect(-L, nNormal);
		float spec = pow(max(dot(nViewDir, rL), 0.0), 32);
		float specularStr = 1.0 - inRoughness;
		litColor += specularStr * spec * d * radiance;
	}

    // ambient light
	vec3 ambient = vec3(0.005, 0.005, 0.005) * finalColor;

	litColor += ambient;
	fragColor = vec4(litColor, texColor.a);
}