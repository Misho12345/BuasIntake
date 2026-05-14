#include "pch.hpp"

#include "water/TerrainWaterSystem.hpp"

#include "terrain/TerrainGridMath.hpp"

namespace game::water
{
    GridView TerrainWaterSystem::make_grid_view(const terrain::TerrainField& field, const vec2 world_center)
    {
        return GridView{
            .world_center  = world_center,
            .field_origin  = field.origin(),
            .cell_size     = field.cell_size(),
            .field_size    = field.size(),
            .field_samples = field.sample_span()
        };
    }

    std::vector<ivec2> TerrainWaterSystem::collect_water_component(
        const terrain::TerrainField& field,
        const vec2                   world_center,
        const ivec2                  start_coord,
        const bool                   include_diagonals) const
    {
        if (field.empty()) return {};

        return water::collect_water_component(
            make_grid_view(field, world_center),
            start_coord,
            include_diagonals);
    }

    std::optional<TerrainWaterSystem::WaterPlan> TerrainWaterSystem::build_targeted_water_plan(
        const terrain::TerrainField& field,
        const vec2                   world_center,
        const vec2                   world_position,
        const std::uint32_t          volume_cap,
        const bool                   pickup) const
    {
        if (field.empty()) return std::nullopt;

        return water::build_targeted_water_plan(
            make_grid_view(field, world_center),
            world_position,
            volume_cap,
            pickup);
    }

    bool TerrainWaterSystem::apply_water_plan(
        terrain::TerrainField&             field,
        const WaterPlan&                   plan,
        const std::function<void(ivec2)>&  mark_dirty,
        std::vector<ivec2>&                changed_coords) const
    {
        bool changed = false;
        for (const auto& coord : plan.dried_component)
        {
            auto&       sample     = field.sample(coord);
            const float next_water = terrain::dry_water_density(sample);
            if (std::abs(sample.water - next_water) <= 1e-6f) continue;

            sample.water = next_water;
            changed      = true;
            changed_coords.push_back(coord);
            if (mark_dirty) mark_dirty(coord);
        }

        for (const auto& [coord, water] : plan.affected_samples)
        {
            auto& sample = field.sample(coord);

            const float next_water = sample.terrain < 0.0f && water > 0.0f
                                         ? water
                                          : terrain::dry_water_density(sample);

            if (std::abs(sample.water - next_water) <= 1e-6f) continue;

            sample.water = next_water;
            changed      = true;
            changed_coords.push_back(coord);
            if (mark_dirty) mark_dirty(coord);
        }

        return changed;
    }

    std::uint32_t TerrainWaterSystem::total_water_sample_count(const terrain::TerrainField& field) const
    {
        return static_cast<std::uint32_t>(
            std::ranges::count_if(
                field.samples(),
                [](const terrain::TerrainField::FieldSample& sample) { return has_water(sample); }));
    }

    bool TerrainWaterSystem::contains_water_volume(const terrain::TerrainField& field, const vec2 world_position) const
    {
        if (field.empty()) return false;

        const float value = terrain::sample_field_channel_bilinear(
            field.samples(),
            field.size(),
            field.origin(),
            field.cell_size(),
            world_position,
            [](const terrain::TerrainField::FieldSample& sample)
            {
                return std::min(-sample.terrain, sample.water);
            });

        return value > 0.0f;
    }

    bool TerrainWaterSystem::has_water_neighbor(const terrain::TerrainField& field, const ivec2 coord) const
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;

                const ivec2 neighbor = coord + ivec2{ x, y };
                if (!field.is_valid_sample(neighbor)) continue;
                if (has_water(field.sample(neighbor))) return true;
            }
        }

        return false;
    }

    bool TerrainWaterSystem::has_protective_water_neighbor(
        const terrain::TerrainField& field,
        const vec2                   world_center,
        const ivec2                  coord) const
    {
        if (!field.is_valid_sample(coord)) return false;

        const vec2  sample_world      = field.sample_world_position(coord);
        const vec2  up                = normalize(sample_world - world_center, { 0.0f, 1.0f });
        const vec2  tangent{ up.y, -up.x };
        const auto  cell_size         = field.cell_size();
        const float cell_extent       = min(cell_size);
        const float tangential_limit  = cell_extent * 2.35f;
        const float outward_limit     = cell_extent * 2.35f;
        const float inward_allowance  = cell_extent * 0.60f;

        static constexpr int search_radius = 4;

        for (int y = -search_radius; y <= search_radius; ++y)
        {
            for (int x = -search_radius; x <= search_radius; ++x)
            {
                if (x == 0 && y == 0) continue;

                const ivec2 neighbor = coord + ivec2{ x, y };
                if (!field.is_valid_sample(neighbor)) continue;

                const auto& neighbor_sample = field.sample(neighbor);
                if (!has_water(neighbor_sample)) continue;

                const vec2  delta          = field.sample_world_position(neighbor) - sample_world;
                const float tangent_offset = std::abs(delta.dot(tangent));
                const float up_offset      = delta.dot(up);
                
                if (tangent_offset > tangential_limit) continue;
                if (up_offset < -inward_allowance || up_offset > outward_limit) continue;

                return true;
            }
        }

        return false;
    }
}
