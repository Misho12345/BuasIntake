#pragma once

#include "pch.hpp"

#include "terrain/TerrainField.hpp"

namespace game::vegetation
{
    class VegetationSystem;
}

namespace game::terrain
{
    class TerrainMoistureSystem final
    {
    public:
        using MarkDirty = std::function<void(ivec2)>;
        using CollectWaterComponent = std::function<std::vector<ivec2>(ivec2, bool)>;

        void recompute_wetness_around(
            TerrainField&                       field,
            const std::vector<ivec2>&           changed_coords,
            const CollectWaterComponent&        collect_water_component,
            const MarkDirty&                    mark_dirty,
            const vegetation::VegetationSystem* vegetation,
            bool                                recompute_greenness = true,
            bool                                mark_affected_visuals_dirty = false) const;

        void recompute_ground_greenness(
            TerrainField&                       field,
            const vegetation::VegetationSystem* vegetation,
            const MarkDirty&                    mark_dirty) const;
    };
}
