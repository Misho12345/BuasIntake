#version 460 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoords;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoords;
out vec2 vWorldPosition;

void main()
{
    vColor = aColor;
    vTexCoords = aTexCoords;

    // fragment shaders use world position for texture sampling, so that's why I keep the unprojected value too
    vWorldPosition = aPosition;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
