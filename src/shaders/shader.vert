#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(push_constant, scalar) uniform DrawConstants
{
    uint64_t vertexAddress;
} drawConsts;

struct Vertex
{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 uv;
};

layout(buffer_reference, scalar) readonly buffer VertexPtr
{
    Vertex vertices[];
};

layout (location = 0) out vec3 outColor;

void main() {
    VertexPtr vBuffer = VertexPtr(drawConsts.vertexAddress);
    Vertex v = vBuffer.vertices[gl_VertexIndex];
    gl_Position = vec4(v.position, 1.0);
    outColor = v.position;
}
