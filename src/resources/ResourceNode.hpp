#pragma once

#include "pch.hpp"

namespace game::resources
{
    enum class ResourceNodeKind : std::uint8_t
    {
        Rock,
        IronOre,
        CopperOre,
        GoldOre,
        DiamondOre,
        DeadPlant
    };

    using ResourceKind = ResourceNodeKind;

    struct ResourceNode final
    {
        ResourceNodeKind kind{ ResourceNodeKind::Rock };

        ivec2        coord{ 0, 0 };
        std::uint8_t variant{ 0u };

        bool surface_attached{ false };

        vec2 anchor_world{ 0.0f, 0.0f };
        vec2 surface_up{ 0.0f, 0.0f };
    };
}
