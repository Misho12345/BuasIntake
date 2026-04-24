#version 460 core

in vec4 vColor;
in vec2 vTexCoords;
in vec2 vWorldPosition;

uniform float uTime;

out vec4 FragColor;

void main()
{
	float wave_a = sin(vWorldPosition.x * 0.42 + uTime * 1.55 + vWorldPosition.y * 0.15);
	float wave_b = sin(vWorldPosition.y * 0.78 - uTime * 1.18 + vWorldPosition.x * 0.27);
	float wave_c = sin((vWorldPosition.x + vWorldPosition.y) * 0.36 - uTime * 1.95);
	float wave_mix = 0.5 + 0.5 * (wave_a * 0.45 + wave_b * 0.35 + wave_c * 0.20);

	float large_flow = 0.5 + 0.5 * sin(vWorldPosition.x * 0.09 + vWorldPosition.y * 0.07 - uTime * 0.42);
	float caustics = 0.5 + 0.5 * sin(vWorldPosition.x * 1.25 - uTime * 2.4) * sin(vWorldPosition.y * 1.05 + uTime * 1.7);

	vec3 shallow = vec3(0.22, 0.76, 0.95);
	vec3 deep = vec3(0.04, 0.23, 0.63);
	vec3 color = mix(deep, shallow, 0.35 + large_flow * 0.25 + caustics * 0.20);
	color += vec3(0.06, 0.10, 0.14) * wave_mix;

	float foam = smoothstep(0.73, 0.98, wave_mix + caustics * 0.18);
	color = mix(color, vec3(0.90, 0.97, 1.00), foam * 0.24);

	float alpha = 0.56 + foam * 0.06;
	FragColor = vec4(color * vColor.rgb, alpha * vColor.a);
}
