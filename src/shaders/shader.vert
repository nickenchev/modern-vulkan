#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#define M_PI 3.1415926535897932384626433832795
#define M_2PI M_PI * 2

layout (location = 0) out vec3 outColor;

struct Vertex
{
    vec3 position;
};

layout(buffer_reference, scalar) readonly buffer VertexPtr
{
    Vertex vertices[];
};

layout(push_constant, scalar) uniform DrawConstants
{
    uint64_t vertexAddress;
    float globalTime;
    float padding;
    mat4 mvp;
} drawConsts;

void main()
{
    VertexPtr vBuffer = VertexPtr(drawConsts.vertexAddress);
    vec3 pos = vBuffer.vertices[gl_VertexIndex].position;
    gl_Position = drawConsts.mvp * vec4(pos, 1.0);

    outColor = vec3(1, 1, 1);
}