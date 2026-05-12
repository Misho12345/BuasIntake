#pragma once

#include "pch.hpp"


#include "terrain/TerrainColliderManager.hpp"
#include "water/WaterInteraction.hpp"

namespace game::terrain
{
    class TerrainWaterColliderBuilder final
    {
    public:
        static void rebuild(
            TerrainColliderManager& collider_manager,
            const water::GridView&  grid);
    };
}
