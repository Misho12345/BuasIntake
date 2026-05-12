#pragma once

#include "pch.hpp"


#include "terrain/TerrainGenerator.hpp"

namespace game::water
{
    using FieldSample = terrain::TerrainGenerator::FieldSample;

    struct WaterPlanSample final
    {
        ivec2 coord{ 0, 0 };
        float water{ 0.0f };
    };

    struct WaterPlan final
    {
        std::vector<ivec2>           dried_component{};
        std::vector<WaterPlanSample> affected_samples{};
        std::uint32_t                wet_sample_count{ 0u };
    };

    struct GridView final
    {
        vec2  world_center{ 0.0f, 0.0f };
        vec2  field_origin{ 0.0f, 0.0f };
        vec2  cell_size{ 1.0f, 1.0f };
        uvec2 field_size{ 0u, 0u };

        std::span<const FieldSample> field_samples{};
    };

    bool has_water(const FieldSample& sample);

    float combined_water_field(const FieldSample& sample);
    bool is_valid_global_sample(const GridView& grid, ivec2 coord);
    std::size_t global_field_index(const GridView& grid, ivec2 coord);
    vec2 global_sample_world_position(const GridView& grid, ivec2 coord);

    int solid_neighbor_count(const GridView& grid, ivec2 coord);
    bool has_water_neighbor(const GridView& grid, ivec2 coord);
    ivec2 settle_water_anchor(const GridView& grid, ivec2 anchor);

    std::optional<ivec2> find_water_anchor(const GridView& grid, vec2 world_position);
    std::optional<ivec2> find_water_sample(const GridView& grid, vec2 world_position);

    std::vector<ivec2> collect_water_component(const GridView& grid, ivec2 start_coord, bool include_diagonals = false);

    std::uint32_t water_volume_at_anchor(const GridView& grid, ivec2 anchor, ivec2* plan_start = nullptr);

    std::optional<WaterPlan> build_targeted_water_plan(
        const GridView& grid,
        vec2            world_position,
        std::uint32_t   volume_cap,
        bool            pickup,
        std::uint32_t*  existing_volume = nullptr);

    std::optional<WaterPlan> build_water_plan(
        const GridView& grid,
        ivec2           start_coord,
        std::uint32_t   desired_wet_sample_count,
        bool            preserve_existing_water);
}
