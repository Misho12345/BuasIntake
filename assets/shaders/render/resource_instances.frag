#version 460 core

in vec2 vLocalUv;
flat in vec3 vTileData;

uniform sampler2DArray uSpriteTextureArray;
uniform int uTileSizePixels;

out vec4 FragColor;

void main()
{
    vec2 atlas_size = vec2(textureSize(uSpriteTextureArray, 0).xy);
    vec2 tile_size = vec2(float(uTileSizePixels));

    // each layer is a sprite sheet, so the instance selects a layer and tile while the quad supplies local uv inside that tile
    vec2 atlas_uv = (vec2(vTileData.y, vTileData.z) * tile_size + vLocalUv * tile_size) / atlas_size;
    vec4 sample_color = texture(uSpriteTextureArray, vec3(atlas_uv, vTileData.x));

    if (sample_color.a < 0.05) discard;

    FragColor = sample_color;
}
