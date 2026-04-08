#version 460 core

in vec4 vColor;
in vec2 vTexCoords;
in vec2 vWorldPosition;

uniform sampler2D uDirtTexture;
uniform float uTextureScale;

out vec4 FragColor;

void main()
{
	vec2 base_uv = vWorldPosition * uTextureScale;
	vec3 dirt_a = texture(uDirtTexture, base_uv).rgb;
	vec3 dirt_b = texture(uDirtTexture, base_uv * 0.53 + vec2(0.19, -0.11)).rgb;
	float grain = texture(uDirtTexture, base_uv * 1.7).r;

	vec3 dirt = mix(dirt_a, dirt_b, 0.35);
	vec3 albedo = mix(vColor.rgb, dirt, 0.72);
	float lighting = mix(0.82, 1.08, grain);
	float warm_boost = 0.94 + 0.06 * smoothstep(-0.25, 0.85, vTexCoords.y);
	FragColor = vec4(albedo * lighting * warm_boost, vColor.a);
}
