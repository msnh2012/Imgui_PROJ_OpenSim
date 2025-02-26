#version 330 core

uniform mat4 uModelMat;
uniform mat3 uNormalMat;
uniform mat4 uViewProjMat;

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

out vec3 FragWorldPos;
out vec3 NormalWorld;
out vec2 TexCoord;

void main()
{
    vec4 affineWorldPos = uModelMat * vec4(aPos, 1.0);

    FragWorldPos = vec3(affineWorldPos);
    NormalWorld = uNormalMat * aNormal;
    TexCoord = aTexCoord;
    gl_Position = uViewProjMat * affineWorldPos;
}