#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inFragW;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 4) in flat uint inTextureIndex;
layout(location = 5) in flat vec4 inMaterialBaseColor;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main()
{
    vec4 texColor = texture(textures[inTextureIndex], inUV);
    vec3 finalColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
    vec3 nNormal = normalize(inNormal);

	// point lights
	vec3 lightPos = vec3(-3.3, 2.5, 5);
	vec3 lightCol = vec3(0.8, 0.4, 0.6);
	float lightIntensity = 1;
	float falloff = 7.0;
	float falloffStart = falloff - 4;
	vec3 L = lightPos - inFragW; // vector from fragment to light position
	float lightDistance = length(L);
	vec3 pointLight = vec3(0);
	if (lightDistance < falloff)
	{
		float inverseSquare = 1.0 / max(lightDistance * lightDistance, 0.0001);
		float window = clamp(1.0 - pow(lightDistance / falloff, 4), 0, 1);
		float attenuation = inverseSquare * window;
		L = L / lightDistance;
		float d = max(dot(nNormal, L), 0);
		pointLight += finalColor * lightCol * lightIntensity * d * attenuation;
		//pointLight += finalColor * lightCol * lightIntensity * d * attenuation;
	}

    // ambient light
	vec3 ambient = vec3(0.01, 0.01, 0.01) * finalColor;

	vec3 litColor = pointLight + ambient;
	fragColor = vec4(litColor, texColor.a);
}