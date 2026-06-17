#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(push_constant, scalar) uniform DrawConstants
{
    mat4x4 wvp;
    uint64_t vertexAddress;
    uint64_t materialAddress;
    uint materialIndex;
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

struct Material
{
    vec4 baseColor;
    uint textureId;
};

layout(buffer_reference, scalar) readonly buffer MaterialPtr
{
    Material materials[];
};

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 outUV;
layout (location = 2) out flat uint outTextureIndex;

void main()
{
    VertexPtr vBuffer = VertexPtr(drawConsts.vertexAddress);
    Vertex v = vBuffer.vertices[gl_VertexIndex];

    MaterialPtr matBuff = MaterialPtr(drawConsts.materialAddress);
    outTextureIndex = matBuff.materials[drawConsts.materialIndex].textureId - 1;

    gl_Position = drawConsts.wvp * vec4(v.position, 1.0);
    outColor = v.color;
    outUV = v.uv;
}
