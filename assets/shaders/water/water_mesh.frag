#version 460 core

// simple water material for the generated pond mesh
// it uses world-space waves instead of textures so small ponds still animate without needing uv unwraps
// I used chatgpt 5.4 to make the effects of this shader because the shader is just playing with math

in vec4 vColor;
in vec2 vWorldPosition;

uniform float uTime;

out vec4 FragColor;

const float wave_a_scale = 0.42;
const float wave_b_scale = 0.78;
const float wave_c_scale = 0.36;
const float large_flow_x_scale = 0.09;
const float large_flow_y_scale = 0.07;
const float caustic_x_scale = 1.25;
const float caustic_y_scale = 1.05;

void main()
{
    float wave_a = sin(vWorldPosition.x * wave_a_scale + uTime * 1.55 + vWorldPosition.y * 0.15);
    float wave_b = sin(vWorldPosition.y * wave_b_scale - uTime * 1.18 + vWorldPosition.x * 0.27);
    float wave_c = sin((vWorldPosition.x + vWorldPosition.y) * wave_c_scale - uTime * 1.95);
    float wave_mix = 0.5 + 0.5 * (wave_a * 0.45 + wave_b * 0.35 + wave_c * 0.20);

    float large_flow = 0.5 + 0.5 * sin(vWorldPosition.x * large_flow_x_scale + vWorldPosition.y * large_flow_y_scale - uTime * 0.42);
    float caustics = 0.5 + 0.5 * sin(vWorldPosition.x * caustic_x_scale - uTime * 2.4) * sin(vWorldPosition.y * caustic_y_scale + uTime * 1.7);

    vec3 shallow = vec3(0.22, 0.76, 0.95);
    vec3 deep = vec3(0.04, 0.23, 0.63);
    vec3 color = mix(deep, shallow, 0.35 + large_flow * 0.25 + caustics * 0.20);
    color += vec3(0.06, 0.10, 0.14) * wave_mix;

    float foam = smoothstep(0.73, 0.98, wave_mix + caustics * 0.18);
    color = mix(color, vec3(0.90, 0.97, 1.00), foam * 0.24);

    float alpha = 0.56 + foam * 0.06;
    FragColor = vec4(color * vColor.rgb, alpha * vColor.a);
}
