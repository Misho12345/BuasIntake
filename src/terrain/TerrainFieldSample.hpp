#pragma once

#include "pch.hpp"

namespace game::terrain
{
    struct TerrainFieldSample final
    {
        float terrain{};
        float water{};
        float wetness{};
        float greenness{};
    };

    constexpr bool is_solid_sample(const TerrainFieldSample& sample)
    {
        return sample.terrain >= 0.0f;
    }

    constexpr bool has_water_sample(const TerrainFieldSample& sample)
    {
        return std::min(-sample.terrain, sample.water) > 1e-4f;
    }

    inline float dry_water_density(const TerrainFieldSample& sample)
    {
        return -std::abs(sample.terrain);
    }
}
