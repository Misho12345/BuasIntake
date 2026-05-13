#pragma once

#include "pch.hpp"


namespace game::vegetation
{
    inline constexpr float seed_plantable_wetness_threshold{ 0.35f };
    inline constexpr float seed_to_sprout_time{ 22.0f };
    inline constexpr float sprout_growth_duration{ 53.0f };

    enum class PlantStage : std::uint8_t
    {
        Empty,
        Seeded,
        Sprout,
        Mature
    };

    enum class PlantFamily : std::uint8_t
    {
        Grass,
        Flowers,
        Bush,
        Tree
    };

    struct Plant final
    {
        // stored per terrain sample; empty entries mean no plant, active entries keep just enough data for growth and rendering
        PlantStage   stage{ PlantStage::Empty };
        PlantFamily  family{ PlantFamily::Grass };
        float        age{ 0.0f };
        float        spread_age{ 0.0f };
        std::uint8_t variant{ 0u };
        vec2         anchor_world{ 0.0f, 0.0f };
    };
}
