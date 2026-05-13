#include "pch.hpp"

#include "water/WaterSystem.hpp"

#include "terrain/PlanetTerrain.hpp"

namespace game::water
{
    using terrain::PlanetTerrain;

    namespace
    {
        Result<WaterActionResult> apply_water_action(
            PlanetTerrain&      terrain,
            const vec2          world_position,
            const std::uint32_t volume_cap,
            const bool          pickup)
        {
            if (volume_cap == 0u) return WaterActionResult{ .status = WaterActionStatus::EmptyAmount };

            const auto previous_total = terrain.total_water_sample_count();
            const auto plan           = terrain.build_targeted_water_plan(world_position, volume_cap, pickup);
            if (!plan.has_value()) return WaterActionResult{ .status = WaterActionStatus::NoValidTarget };

            const auto applied = terrain.apply_water_plan_and_rebuild(*plan);
            TRY(applied);

            if (!*applied) return WaterActionResult{ .status = WaterActionStatus::NoChange };

            const auto next_total = terrain.total_water_sample_count();
            return WaterActionResult{
                .status        = WaterActionStatus::Applied,
                .changed_units = pickup
                                    ? (previous_total > next_total ? previous_total - next_total : 0u)
                                    : (next_total > previous_total ? next_total - previous_total : 0u)
            };
        }
    }

    // the public water api stays tiny on purpose
    // all the ugly basin finding and fill planning lives lower down and this just turns that into a simple place action result
    Result<WaterActionResult> WaterSystem::place_water(
        PlanetTerrain&      terrain,
        const vec2          world_position,
        const std::uint32_t volume_cap) const
    {
        return apply_water_action(terrain, world_position, volume_cap, false);
    }

    // pickup is the same story in reverse
    // plan first then commit then report how much actually changed because clicks do not always map to a useful amount of water
    Result<WaterActionResult> WaterSystem::pickup_water(
        PlanetTerrain&      terrain,
        const vec2          world_position,
        const std::uint32_t volume_cap) const
    {
        return apply_water_action(terrain, world_position, volume_cap, true);
    }

    // the preview keeps both meshes because the renderer wants to show what is there now and what would be there after the commit
    Result<std::optional<WaterPreviewMesh>> WaterSystem::build_preview(
        const PlanetTerrain& terrain,
        const vec2           world_position,
        const std::uint32_t  volume_cap) const
    {
        const auto plan = terrain.build_targeted_water_plan(world_position, volume_cap, false);
        if (!plan.has_value()) return std::nullopt;

        WaterPreviewMesh preview{};
        auto append_sample_quad = [&terrain](std::vector<vec2>& vertices, std::vector<std::uint32_t>& indices,
                                             const ivec2 coord)
        {
            const vec2 center    = terrain.global_sample_world_position(coord);
            const vec2 cell_size = terrain.terrain_cell_size();
            const vec2 half_cell{ cell_size.x * 0.5f, cell_size.y * 0.5f };
            const auto base_index = static_cast<std::uint32_t>(vertices.size());

            vertices.emplace_back(center.x - half_cell.x, center.y - half_cell.y);
            vertices.emplace_back(center.x + half_cell.x, center.y - half_cell.y);
            vertices.emplace_back(center.x + half_cell.x, center.y + half_cell.y);
            vertices.emplace_back(center.x - half_cell.x, center.y + half_cell.y);

            indices.insert(
                indices.end(), {
                    base_index,
                    base_index + 1u,
                    base_index + 2u,
                    base_index,
                    base_index + 2u,
                    base_index + 3u
                });
        };

        std::unordered_set<std::uint64_t> future_keys;
        future_keys.reserve(plan->affected_samples.size());
        for (const auto& entry : plan->affected_samples) { future_keys.insert(sample_key(entry.coord)); }

        // Keep both meshes so the preview can show what water is already there and what the placement would change.
        for (const auto coord : plan->dried_component)
        {
            if (!terrain.is_valid_global_sample(coord)) continue;
            if (future_keys.contains(sample_key(coord))) continue;

            const auto& current_sample = terrain.global_sample(coord);
            if (has_water(current_sample)) append_sample_quad(preview.current_vertices, preview.current_indices, coord);
        }

        for (const auto& [coord, water] : plan->affected_samples)
        {
            if (!terrain.is_valid_global_sample(coord)) continue;

            const auto& current_sample = terrain.global_sample(coord);
            if (has_water(current_sample)) append_sample_quad(preview.current_vertices, preview.current_indices, coord);
            const bool future_has_water = std::min(-current_sample.terrain, water) > 1e-4f;
            if (future_has_water) append_sample_quad(preview.future_vertices, preview.future_indices, coord);
        }

        if (preview.future_vertices.empty()) return std::nullopt;
        return preview;
    }

}
