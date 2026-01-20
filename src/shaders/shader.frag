#version 460

// output to the first color attachment (swapchain)
layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 fragColor;

void main()
{
	float brightness = 1.0 - gl_FragCoord.z;
	fragColor = vec4(inColor * brightness, 1.0);
}