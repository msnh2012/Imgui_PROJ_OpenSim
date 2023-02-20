#version 330 core

uniform mat4 uViewProjMat;
uniform mat4 uModelMat;
uniform mat3 uNormalMat;
uniform vec3 uLightWorldPos;
uniform vec3 uViewWorldPos;

layout (location = 0) in vec3 aPos;
layout (location = 2) in vec3 aNormal;
layout (location = 1) in vec2 aTexCoord;
layout (location = 4) in vec4 aTangent;

out vec3 FragWorldPos;
out vec2 TexCoord;
out vec3 NormalTangentDir;  // used in non-normal-mapped scenario
out vec3 LightTangentPos;
out vec3 ViewTangentPos;
out vec3 FragTangentPos;

void main()
{
    // compute change-of-basis matrix for world-to-tangent space
    vec3 normal = normalize(uNormalMat * aNormal);
    vec3 T = normalize(uNormalMat * vec3(aTangent));
    vec3 B = cross(normal, T) * aTangent.w;  // `w` is computed CPU-side to figure out direction
    mat3 TBN = transpose(mat3(T, B, normal));

    vec4 vertWorldPos = uModelMat * vec4(aPos, 1.0);

    FragWorldPos = vec3(vertWorldPos);
    TexCoord = aTexCoord;
    NormalTangentDir = TBN * normal;
    LightTangentPos = TBN * uLightWorldPos;
    ViewTangentPos = TBN * uViewWorldPos;
    FragTangentPos = TBN * FragWorldPos;
    gl_Position = uViewProjMat * vertWorldPos;
}