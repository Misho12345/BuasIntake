#version 460 core

in vec4 vColor;
in vec2 vTexCoords;
in vec2 vWorldPosition;

uniform sampler2D uDirtTexture;
uniform sampler2D uRockTexture;
uniform sampler2D uHardRockTexture;
uniform float uTextureScale;
uniform float uRockBlendStartDepth;
uniform float uRockBlendEndDepth;
uniform float uHardRockStartDepth;

out vec4 FragColor;

void main()
{
	vec2 base_uv = vWorldPosition * uTextureScale;
	float wetness = clamp(vTexCoords.x, 0.0, 1.0);
	float depth = clamp(1.0 - vTexCoords.y, 0.0, 1.0);
	float rock_mix = smoothstep(uRockBlendStartDepth, uRockBlendEndDepth, depth);
	float hard_rock_mix = step(uHardRockStartDepth, depth);

	vec3 dirt_a = texture(uDirtTexture, base_uv).rgb;
	vec3 dirt_b = texture(uDirtTexture, base_uv * 0.53 + vec2(0.19, -0.11)).rgb;
	vec3 rock_a = texture(uRockTexture, base_uv).rgb;
	vec3 rock_b = texture(uRockTexture, base_uv * 0.61 + vec2(-0.23, 0.17)).rgb;
	vec3 hard_rock_a = texture(uHardRockTexture, base_uv).rgb;
	vec3 hard_rock_b = texture(uHardRockTexture, base_uv * 0.67 + vec2(0.27, 0.08)).rgb;
	float dirt_grain = texture(uDirtTexture, base_uv * 1.7).r;
	float rock_grain = texture(uRockTexture, base_uv * 1.45).r;
	float hard_rock_grain = texture(uHardRockTexture, base_uv * 1.25).r;

	vec3 dirt = mix(dirt_a, dirt_b, 0.35);
	vec3 rock = mix(rock_a, rock_b, 0.45);
	vec3 hard_rock = mix(hard_rock_a, hard_rock_b, 0.5);
	vec3 material = mix(dirt, rock, rock_mix);
	material = mix(material, hard_rock, hard_rock_mix);

	float grain = mix(dirt_grain, rock_grain, rock_mix);
	grain = mix(grain, hard_rock_grain, hard_rock_mix);

	float material_mix = mix(0.72, 0.56, rock_mix);
	material_mix = mix(material_mix, 0.82, hard_rock_mix);
	vec3 albedo = mix(vColor.rgb, material, material_mix);

	vec3 wet_tint = mix(vec3(0.52, 0.45, 0.38), vec3(0.63, 0.67, 0.72), rock_mix);
	wet_tint = mix(wet_tint, vec3(0.48, 0.52, 0.58), hard_rock_mix);
	vec3 wet_albedo = albedo * wet_tint;
	float lighting = mix(0.82, 1.08, grain);
	lighting *= mix(1.0, 0.84, wetness);
	float warm_boost = mix(0.94 + 0.06 * smoothstep(-0.25, 0.85, vTexCoords.y), 0.99, rock_mix);
	float sheen = wetness * (0.03 + grain * 0.08);
	vec3 final_color = mix(albedo, wet_albedo, wetness) * lighting * warm_boost + vec3(sheen);
	FragColor = vec4(final_color, vColor.a);
}
