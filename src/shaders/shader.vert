#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
} frameConsts;

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
    uint colorTextureIndex;
};

layout(buffer_reference, scalar) readonly buffer MaterialPtr
{
    Material materials[];
};

struct RenderItem
{
    mat4x4 wvp;
    mat4x4 worldMatrix;
    uint materialIndex;
};

layout(buffer_reference, scalar) readonly buffer RenderItemPtr
{
    RenderItem renderItems[];
};


layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out flat uint outTextureIndex;
layout (location = 4) out flat vec4 outMaterialBaseColor;

void main()
{
    VertexPtr vBuffer = VertexPtr(frameConsts.vertexBufferAddress);
    Vertex v = vBuffer.vertices[gl_VertexIndex];

    RenderItemPtr riBuffer = RenderItemPtr(frameConsts.renderItemBufferAddress);
    RenderItem ri = riBuffer.renderItems[gl_InstanceIndex];

    MaterialPtr matBuff = MaterialPtr(frameConsts.materialBufferAddress);
    Material material = matBuff.materials[ri.materialIndex];

    gl_Position = ri.wvp * vec4(v.position, 1.0);
    outColor = v.color;
    outNormal = mat3x3(transpose(inverse(ri.worldMatrix))) * v.normal; 
    outUV = v.uv;
    outTextureIndex = material.colorTextureIndex;
    outMaterialBaseColor = material.baseColor;
}
