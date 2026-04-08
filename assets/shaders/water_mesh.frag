#version 460 core

in vec4 vColor;
in vec2 vTexCoords;
in vec2 vWorldPosition;

uniform float uTime;

out vec4 FragColor;

void main()
{
	float depth = clamp(vTexCoords.x, 0.0, 1.0);
	float wave_a = sin(vWorldPosition.x * 0.42 + uTime * 1.55 + vWorldPosition.y * 0.15);
	float wave_b = sin(vWorldPosition.y * 0.78 - uTime * 1.18 + vWorldPosition.x * 0.27);
	float wave_c = sin((vWorldPosition.x + vWorldPosition.y) * 0.36 - uTime * 1.95);
	float wave_mix = 0.5 + 0.5 * (wave_a * 0.45 + wave_b * 0.35 + wave_c * 0.20);

	vec3 shallow = vec3(0.24, 0.78, 0.94);
	vec3 deep = vec3(0.03, 0.20, 0.58);
	vec3 color = mix(shallow, deep, depth);

	float caustics = 0.5 + 0.5 * sin(vWorldPosition.x * 1.25 - uTime * 2.4) * sin(vWorldPosition.y * 1.05 + uTime * 1.7);
	color += vec3(0.08, 0.12, 0.16) * caustics * (1.0 - depth);
	color += vec3(0.10, 0.16, 0.22) * wave_mix;

	float foam = smoothstep(0.68, 0.98, wave_mix + (1.0 - depth) * 0.42);
	color = mix(color, vec3(0.90, 0.97, 1.00), foam * 0.32);

	float alpha = mix(0.76, 0.48, depth) + foam * 0.10;
	FragColor = vec4(color * vColor.rgb, alpha * vColor.a);
}
