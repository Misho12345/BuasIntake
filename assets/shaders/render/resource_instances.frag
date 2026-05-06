#version 460 core

in vec2 vLocalUv;
flat in vec3 vTileData;

uniform sampler2DArray uOreTextureArray;
uniform int uTileSizePixels;

out vec4 FragColor;

void main()
{
    vec2 atlas_size = vec2(textureSize(uOreTextureArray, 0).xy);
    vec2 tile_size = vec2(float(uTileSizePixels));

    // Decode the packed atlas location from the instance data.
    vec2 atlas_uv = (vec2(vTileData.y, vTileData.z) * tile_size + vLocalUv * tile_size) / atlas_size;
    vec4 sample_color = texture(uOreTextureArray, vec3(atlas_uv, vTileData.x));
    if (sample_color.a < 0.05) discard;

    FragColor = sample_color;
}
