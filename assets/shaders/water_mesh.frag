#version 460 core

in vec4 vColor;
in vec2 vTexCoords;

out vec4 FragColor;

void main()
{
	const vec3 shallow_tint = vec3(0.196, 0.525, 0.780);
	const vec3 deep_tint = vec3(0.043, 0.203, 0.384);
	const float depth_blend = clamp(vTexCoords.y, 0.0, 1.0);
	const vec3 tint = mix(shallow_tint, deep_tint, depth_blend);
	FragColor = vec4(vColor.rgb * tint, vColor.a);
}
