#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in flat uint inTextureIndex;
layout(location = 4) in flat vec4 inMaterialBaseColor;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main()
{
    vec3 nNormal = normalize(inNormal);
    vec3 lightDirection = normalize(vec3(-0.75, -1, 0));
    float d = max(dot(nNormal, -lightDirection), 0);
    vec4 texColor = texture(textures[inTextureIndex], inUV);

    // two-tone ambient light
	vec3 skyColor = vec3(0.15, 0.18, 0.25);
	vec3 groundColor = vec3(0.05, 0.03, 0.02);
	float t = nNormal.y * 0.5 + 0.5;
	vec3 hemiAmbient = mix(groundColor, skyColor, t);

    vec3 finalColor = inColor * texColor.rgb * inMaterialBaseColor.rgb;
	vec3 litColor = finalColor * d + finalColor * hemiAmbient;
	fragColor = vec4(litColor, texColor.a);
}