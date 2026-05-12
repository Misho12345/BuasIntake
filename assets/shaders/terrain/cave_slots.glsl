const float tau = 6.28318530718;

const int cave_slot_count = 16;
const float cave_activation_threshold = 0.14;
const float min_cave_depth = 0.22;
const float max_cave_depth = 0.43;
const float min_cave_radius_tangent = 5.8;
const float max_cave_radius_tangent = 10.8;
const float min_cave_radius_up = 4.2;
const float max_cave_radius_up = 7.0;

// shared cave layout used by both cave carving and pond placement
// this is why water basins line up with the same cave spaces instead of being a separate random pass

float smooth_profile(float value)
{
    float t = clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct CaveSlotProfile
{
    vec2 cave_up;
    vec2 tangent;
    vec2 cave_center;
    float size_roll;
    float base_radius_tangent;
    float base_radius_up;
    int lobe_count;
    float lobe_span;
};

bool cave_slot_active(float slot_id)
{
    return hash(vec2(slot_id + 11.3, 7.9)) >= cave_activation_threshold;
}

CaveSlotProfile cave_slot_profile(int slot)
{
    float slot_id = float(slot);
    float angle = tau * ((slot_id + 0.5) / float(cave_slot_count));
    angle += (hash(vec2(slot_id + 3.7, 19.1)) - 0.5) * (tau / float(cave_slot_count)) * 0.74;

    vec2 cave_up = vec2(cos(angle), sin(angle));
    vec2 tangent = vec2(cave_up.y, -cave_up.x);
    float cave_depth = mix(min_cave_depth, max_cave_depth, hash(vec2(slot_id + 5.1, 13.7)));
    float cave_radius = uPlanetRadius * (1.0 - cave_depth);
    vec2 cave_center = uWorldCenter + cave_up * cave_radius;

    float size_roll = hash(vec2(slot_id + 17.1, 2.6));
    float height_roll = hash(vec2(slot_id + 23.4, 9.2));
    float base_radius_tangent = mix(min_cave_radius_tangent, max_cave_radius_tangent, size_roll);
    float base_radius_up = mix(min_cave_radius_up, max_cave_radius_up, height_roll) * 0.75;
    int lobe_count = 2 + int(floor(hash(vec2(slot_id + 31.4, 4.8)) * 4.0));
    float lobe_spacing = mix(5.6, 10.8, hash(vec2(slot_id + 27.2, 12.3)));
    float lobe_span = lobe_spacing * 0.5 * float(max(lobe_count - 1, 0));

    CaveSlotProfile profile;
    profile.cave_up = cave_up;
    profile.tangent = tangent;
    profile.cave_center = cave_center;
    profile.size_roll = size_roll;
    profile.base_radius_tangent = base_radius_tangent;
    profile.base_radius_up = base_radius_up;
    profile.lobe_count = lobe_count;
    profile.lobe_span = lobe_span;
    return profile;
}
