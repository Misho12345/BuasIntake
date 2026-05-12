#pragma once

#include "pch.hpp"

#include "terrain/TerrainField.hpp"
#include "water/WaterInteraction.hpp"

namespace game::water
{
    class TerrainWaterSystem final
    {
    public:
        using WaterPlan = water::WaterPlan;

        std::vector<ivec2> collect_water_component(
            const terrain::TerrainField& field,
            vec2                         world_center,
            ivec2                        start_coord,
            bool                         include_diagonals = false) const;

        std::optional<WaterPlan> build_targeted_water_plan(
            const terrain::TerrainField& field,
            vec2                         world_center,
            vec2                         world_position,
            std::uint32_t                volume_cap,
            bool                         pickup) const;

        bool apply_water_plan(
            terrain::TerrainField&          field,
            const WaterPlan&                plan,
            const std::function<void(ivec2)>& mark_dirty,
            std::vector<ivec2>&             changed_coords) const;

        std::uint32_t total_water_sample_count(const terrain::TerrainField& field) const;
        bool          contains_water_volume(const terrain::TerrainField& field, vec2 world_position) const;
        bool          has_water_neighbor(const terrain::TerrainField& field, ivec2 coord) const;
        bool          has_protective_water_neighbor(const terrain::TerrainField& field, vec2 world_center, ivec2 coord) const;

    private:
        static GridView make_grid_view(const terrain::TerrainField& field, vec2 world_center);
    };
}
