float surface_radius(vec2 direction)
{
    // sample in direction space so the shell wraps evenly around the center
    vec2 seed_offset = vec2(0.0137, 0.0211) * uSeed;

    float macro = fbm(direction * 1.85 + seed_offset + vec2(3.1, -7.4));
    float medium = fbm(direction * 6.20 - seed_offset.yx + vec2(-11.2, 4.6));
    float ridges = ridged_fbm(direction * 11.50 + seed_offset * 1.3 + vec2(8.4, -5.6));
    float micro = fbm(direction * 23.0 - seed_offset * 0.75 + vec2(-4.2, 12.8));

    return uPlanetRadius
        + (macro - 0.5) * uPlanetRadius * 0.19
        + (medium - 0.5) * uPlanetRadius * 0.07
        + (ridges - 0.45) * uPlanetRadius * 0.045
        + (micro - 0.5) * uPlanetRadius * 0.02;
}
