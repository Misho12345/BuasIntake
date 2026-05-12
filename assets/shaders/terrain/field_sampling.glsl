float sample_field(ivec2 p)
{
    vec4 field = imageLoad(uField, p);

    // channel 1 is water, clipped by the terrain cavity
    if (uChannelIndex == 0)
    {
        return field.r;
    }

    return min(-field.r, field.g);
}

vec2 grid_to_world(vec2 g)
{
    return uFieldOrigin + g * uCellSize;
}
