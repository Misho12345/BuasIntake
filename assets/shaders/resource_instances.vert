#version 460 core

layout (location = 0) in vec2 aLocalPosition;
layout (location = 1) in vec2 aLocalUv;
layout (location = 2) in vec2 aInstanceCenter;
layout (location = 3) in vec2 aInstanceUp;
layout (location = 4) in vec4 aInstanceParams0;
layout (location = 5) in float aInstanceTileRow;
layout (location = 6) in float aInstanceAngleOffset;

uniform mat4 uProjection;

out vec2 vLocalUv;
flat out vec3 vTileData;

void main()
{
	float world_height = aInstanceParams0.x;
	float radial_offset = aInstanceParams0.y;
	vec2 up = normalize(aInstanceUp);
	vec2 tangent = vec2(up.y, -up.x);
	vec2 anchor = aInstanceCenter + up * radial_offset;
	float angle = aInstanceAngleOffset;
	float c = cos(angle);
	float s = sin(angle);
	vec2 rotated_position = vec2(
		aLocalPosition.x * c - aLocalPosition.y * s,
		aLocalPosition.x * s + aLocalPosition.y * c);
	vec2 world_position = anchor + tangent * (rotated_position.x * world_height) + up * (rotated_position.y * world_height);

	vLocalUv = aLocalUv;
	vTileData = vec3(aInstanceParams0.z, aInstanceParams0.w, aInstanceTileRow);
	gl_Position = uProjection * vec4(world_position, 0.0, 1.0);
}
