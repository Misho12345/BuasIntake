#version 460 core

layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoords;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoords;

void main()
{
	vColor = aColor;
	vTexCoords = aTexCoords;
	gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
