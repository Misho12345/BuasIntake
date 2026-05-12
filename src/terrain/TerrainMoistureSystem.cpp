#include "pch.hpp"

#include "terrain/TerrainMoistureSystem.hpp"

#include "terrain/TerrainGreennessSystem.hpp"
#include "terrain/TerrainWetnessSystem.hpp"
#include "vegetation/VegetationSystem.hpp"

namespace game::terrain
{
    void TerrainMoistureSystem::recompute_wetness_around(
        TerrainField&                       field,
        const std::vector<ivec2>&           changed_coords,
        const CollectWaterComponent&        collect_water_component,
        const MarkDirty&                    mark_dirty,
        const vegetation::VegetationSystem* vegetation,
        const bool                          recompute_greenness) const
    {
        terrain_wetness::recompute_around(
            field.sample_span(),
            field.size(),
            field.cell_size(),
            changed_coords,
            mark_dirty,
            collect_water_component);

        if (recompute_greenness) recompute_ground_greenness(field, vegetation, mark_dirty);
    }

    void TerrainMoistureSystem::recompute_ground_greenness(
        TerrainField&                       field,
        const vegetation::VegetationSystem* vegetation,
        const MarkDirty&                    mark_dirty) const
    {
        if (vegetation == nullptr) return;
        const auto plant_samples = vegetation->plant_samples();
        if (field.empty() || plant_samples.empty()) return;

        terrain_greenness::recompute(
            field.sample_span(),
            field.size(),
            field.cell_size(),
            plant_samples,
            vegetation->active_plant_indices(),
            mark_dirty,
            [&field](const ivec2 coord) { return field.sample_world_position(coord); });
    }
}
