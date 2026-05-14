#include "pch.hpp"

#include "water/WaterInteraction.hpp"

namespace game::water
{
    namespace
    {
        struct WaterCandidate final
        {
            ivec2 coord{ 0, 0 };
            float radial{ 0.0f };
            float click_dist_sq{ 0.0f };
        };

        bool sample_has_water(const FieldSample& sample) { return std::min(-sample.terrain, sample.water) > 1e-4f; }

        bool grid_ready(const GridView& grid)
        {
            return !grid.field_samples.empty() && grid.field_size.x > 0u && grid.field_size.y > 0u;
        }

        ivec2 world_to_grid_coord(const GridView& grid, const vec2 world_position)
        {
            return {
                std::clamp(static_cast<int>(std::lround((world_position.x - grid.field_origin.x) / grid.cell_size.x)),
                           0,
                           static_cast<int>(grid.field_size.x) - 1),
                std::clamp(static_cast<int>(std::lround((world_position.y - grid.field_origin.y) / grid.cell_size.y)),
                           0,
                           static_cast<int>(grid.field_size.y) - 1)
            };
        }

        bool has_inward_support(const GridView& grid, const ivec2 coord)
        {
            if (!is_valid_global_sample(grid, coord)) return false;

            // Water needs a bit of solid support toward the planet core or it will look like it is hanging outward.
            const float sample_radial = distance(global_sample_world_position(grid, coord), grid.world_center);
            const float radial_tolerance = min(grid.cell_size) * 0.25f;

            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    if (x == 0 && y == 0) continue;

                    const ivec2 neighbor = coord + ivec2{ x, y };

                    if (!is_valid_global_sample(grid, neighbor)) continue;

                    const auto& neighbor_sample = grid.field_samples[global_field_index(grid, neighbor)];
                    if (neighbor_sample.terrain < 0.0f) continue;

                    const float neighbor_radial = distance(
                        global_sample_world_position(grid, neighbor),
                        grid.world_center);

                    if (neighbor_radial + radial_tolerance < sample_radial) return true;
                }
            }

            return false;
        }

        std::optional<ivec2> find_direct_water_sample(const GridView& grid, const vec2 world_position)
        {
            if (!grid_ready(grid)) return std::nullopt;

            const ivec2 coord = world_to_grid_coord(grid, world_position);

            if (!is_valid_global_sample(grid, coord) ||
                !sample_has_water(grid.field_samples[global_field_index(grid, coord)]))
                return std::nullopt;

            return coord;
        }
    }

    bool has_water(const FieldSample& sample) { return sample_has_water(sample); }

    bool is_valid_global_sample(const GridView& grid, const ivec2 coord)
    {
        return coord.x >= 0 && coord.y >= 0 &&
                coord.x < static_cast<int>(grid.field_size.x) &&
                coord.y < static_cast<int>(grid.field_size.y);
    }

    std::size_t global_field_index(const GridView& grid, const ivec2 coord)
    {
        return static_cast<std::size_t>(coord.y) *
                static_cast<std::size_t>(grid.field_size.x) +
                static_cast<std::size_t>(coord.x);
    }

    vec2 global_sample_world_position(const GridView& grid, const ivec2 coord)
    {
        return grid.field_origin + grid.cell_size * coord;
    }

    // for pickup we care more about what the player is obviously pointing at than the mathematically deepest part of the blob
    // so this is a small local search around the click with nearest visible wet sample winning first
    static std::optional<ivec2> find_water_sample(const GridView& grid, const vec2 world_position)
    {
        if (!grid_ready(grid)) return std::nullopt;

        // When picking up water, prefer the closest visible wet sample instead of jumping to a deeper connected blob.
        const ivec2          center        = world_to_grid_coord(grid, world_position);
        static constexpr int search_radius = 3;

        std::optional<WaterCandidate> best_candidate;
        for (int y = center.y - search_radius; y <= center.y + search_radius; ++y)
        {
            for (int x = center.x - search_radius; x <= center.x + search_radius; ++x)
            {
                const ivec2 coord{ x, y };
                if (!is_valid_global_sample(grid, coord)) continue;

                const auto& sample = grid.field_samples[global_field_index(grid, coord)];
                if (!has_water(sample)) continue;

                const vec2  sample_world  = global_sample_world_position(grid, coord);
                const vec2  click_delta   = sample_world - world_position;
                const float click_dist_sq = click_delta.lengthSquared();

                const WaterCandidate candidate{
                    coord, distance(sample_world, grid.world_center), click_dist_sq
                };

                if (!best_candidate.has_value() ||
                    candidate.click_dist_sq < best_candidate->click_dist_sq ||
                    (std::abs(candidate.click_dist_sq - best_candidate->click_dist_sq) <= 1e-5f &&
                        candidate.radial < best_candidate->radial))
                    best_candidate = candidate;
            }
        }

        if (!best_candidate.has_value()) return std::nullopt;
        return best_candidate->coord;
    }

    // placement wants the visible cavity edge not some random sample behind it
    // so we probe outward from the click first and only fall back to the hit sample when that simple guess fails
    static std::optional<ivec2> find_water_anchor(const GridView& grid, const vec2 world_position)
    {
        if (!grid_ready(grid)) return std::nullopt;

        // For placement, probe just beyond the click first so water snaps to the visible cavity edge before settling.
        const ivec2 hit_coord = world_to_grid_coord(grid, world_position);

        const vec2                    up          = normalize(world_position - grid.world_center, { 0.0f, 1.0f });
        const float                   cell_extent = min(grid.cell_size);
        std::optional<WaterCandidate> best_candidate;
        for (int step = 1; step <= 24; ++step)
        {
            const vec2  probe_world = world_position + up * (cell_extent * 0.35f * static_cast<float>(step));
            const ivec2 probe_coord = world_to_grid_coord(grid, probe_world);

            if (!is_valid_global_sample(grid, probe_coord)) break;
            if (grid.field_samples[global_field_index(grid, probe_coord)].terrain >= 0.0f) break;
            if (!has_inward_support(grid, probe_coord)) continue;

            const vec2  probe_sample_world = global_sample_world_position(grid, probe_coord);
            const vec2  delta              = probe_sample_world - world_position;
            const float click_dist_sq      = delta.lengthSquared();

            const WaterCandidate candidate{
                .coord         = probe_coord,
                .radial        = distance(probe_sample_world, grid.world_center),
                .click_dist_sq = click_dist_sq
            };

            if (!best_candidate.has_value() ||
                candidate.click_dist_sq < best_candidate->click_dist_sq ||
                (std::abs(candidate.click_dist_sq - best_candidate->click_dist_sq) <= 1e-5f &&
                    candidate.radial < best_candidate->radial))
                best_candidate = candidate;
        }

        if (best_candidate.has_value()) return best_candidate->coord;

        if (!is_valid_global_sample(grid, hit_coord)) return std::nullopt;

        if (grid.field_samples[global_field_index(grid, hit_coord)].terrain < 0.0f &&
            has_inward_support(grid, hit_coord))
            return hit_coord;

        return std::nullopt;
    }

    // this is just a straight bfs over wet cells but a lot of later code depends on having a stable connected component first
    std::vector<ivec2> collect_water_component(
        const GridView& grid,
        const ivec2     start_coord,
        const bool      include_diagonals)
    {
        if (!is_valid_global_sample(grid, start_coord)) return {};
        if (!has_water(grid.field_samples[global_field_index(grid, start_coord)])) return {};

        static constexpr std::array orthogonal_neighbors{
            ivec2{ 1, 0 }, ivec2{ -1, 0 },
            ivec2{ 0, 1 }, ivec2{ 0, -1 }
        };

        static constexpr std::array diagonal_neighbors{
            ivec2{ 1, 1 }, ivec2{ 1, -1 },
            ivec2{ -1, 1 }, ivec2{ -1, -1 }
        };

        std::queue<ivec2>                 frontier;
        std::unordered_set<std::uint64_t> visited;
        std::vector<ivec2>                component;

        frontier.push(start_coord);
        visited.insert(sample_key(start_coord));

        while (!frontier.empty())
        {
            const auto coord = frontier.front();
            frontier.pop();
            component.push_back(coord);

            for (const auto& offset : orthogonal_neighbors)
            {
                const ivec2 neighbor = coord + offset;
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (!has_water(grid.field_samples[global_field_index(grid, neighbor)])) continue;

                const auto key = sample_key(neighbor);
                if (!visited.insert(key).second) continue;

                frontier.push(neighbor);
            }

            if (!include_diagonals) continue;

            for (const auto& offset : diagonal_neighbors)
            {
                const ivec2 neighbor = coord + offset;
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (!has_water(grid.field_samples[global_field_index(grid, neighbor)])) continue;

                const auto key = sample_key(neighbor);
                if (!visited.insert(key).second) continue;

                frontier.push(neighbor);
            }
        }

        return component;
    }

    // this is the actual fill planner
    // the frontier is ordered by spill level first because we want something closer to how a basin really fills instead of a dumb radius flood
    // once we know which cells make the cut we derive one shared surface level and turn that back into per cell water values
    static std::optional<WaterPlan> build_settled_water_plan(
        const GridView&     grid,
        const ivec2         start_coord,
        std::vector<ivec2>  previous_water,
        const std::uint32_t desired_wet_sample_count)
    {
        if (!is_valid_global_sample(grid, start_coord)) return std::nullopt;

        const auto& start_sample = grid.field_samples[global_field_index(grid, start_coord)];
        if (start_sample.terrain >= 0.0f) return std::nullopt;

        WaterPlan plan{};
        plan.dried_component = std::move(previous_water);
        if (desired_wet_sample_count == 0u) return plan;

        const float cell_extent    = min(grid.cell_size);
        const float water_strength = 8.0f;
        const float surface_bias   = cell_extent * 0.5f;
        const vec2  start_world    = global_sample_world_position(grid, start_coord);

        struct FillCandidate final
        {
            ivec2 coord{ 0, 0 };
            float radial{ 0.0f };
            float spill_level{ 0.0f };
            float start_dist_sq{ 0.0f };
        };

        struct FrontierNode final
        {
            ivec2 coord{ 0, 0 };
            float radial{ 0.0f };
            float spill_level{ 0.0f };
            float start_dist_sq{ 0.0f };
        };

        struct FrontierCompare final
        {
            bool operator()(const FrontierNode& lhs, const FrontierNode& rhs) const
            {
                if (std::abs(lhs.spill_level - rhs.spill_level) > 1e-5f) return lhs.spill_level > rhs.spill_level;
                if (std::abs(lhs.radial - rhs.radial) > 1e-5f) return lhs.radial > rhs.radial;
                return lhs.start_dist_sq > rhs.start_dist_sq;
            }
        };

        static constexpr std::array neighbors{ ivec2{ 1, 0 }, ivec2{ -1, 0 }, ivec2{ 0, 1 }, ivec2{ 0, -1 } };

        std::priority_queue<FrontierNode, std::vector<FrontierNode>, FrontierCompare> frontier;
        std::unordered_map<std::uint64_t, float>                                      best_spill_levels;
        best_spill_levels.reserve(static_cast<std::size_t>(desired_wet_sample_count) * 6u + 32u);

        auto push_neighbor = [&](const ivec2 coord, const float incoming_spill_level)
        {
            if (!is_valid_global_sample(grid, coord)) return;

            const auto& sample = grid.field_samples[global_field_index(grid, coord)];
            if (sample.terrain >= 0.0f) return;

            const vec2  sample_world = global_sample_world_position(grid, coord);
            const float radial       = distance(sample_world, grid.world_center);
            const float spill_level  = std::max(incoming_spill_level, radial);
            const auto  key          = sample_key(coord);

            if (const auto it = best_spill_levels.find(key);
                it != best_spill_levels.end() && spill_level >= it->second - 1e-5f)
                return;

            best_spill_levels[key] = spill_level;
            const vec2 start_delta = sample_world - start_world;

            frontier.push({
                .coord         = coord,
                .radial        = radial,
                .spill_level   = spill_level,
                .start_dist_sq = start_delta.lengthSquared()
            });
        };

        push_neighbor(start_coord, distance(start_world, grid.world_center));

        std::vector<FillCandidate> selected;
        selected.reserve(desired_wet_sample_count);
        std::optional<FillCandidate> next_unselected;

        while (!frontier.empty())
        {
            const auto& node = frontier.top();
            const ivec2 node_coord = node.coord;
            const float node_radial = node.radial;
            const float node_spill_level = node.spill_level;
            const float node_start_dist_sq = node.start_dist_sq;
            frontier.pop();

            const auto key     = sample_key(node_coord);
            const auto best_it = best_spill_levels.find(key);
            if (best_it == best_spill_levels.end() || node_spill_level > best_it->second + 1e-5f) continue;

            const FillCandidate candidate{
                .coord         = node_coord,
                .radial        = node_radial,
                .spill_level   = node_spill_level,
                .start_dist_sq = node_start_dist_sq
            };

            if (selected.size() < desired_wet_sample_count) selected.push_back(candidate);
            else
            {
                next_unselected = candidate;
                break;
            }

            for (const auto& offset : neighbors)
            {
                push_neighbor(node_coord + offset, node_spill_level);
            }
        }

        const auto selected_count = selected.size();
        if (selected_count == 0u) return plan;

        const float highest_selected = selected.back().spill_level;
        const float next_level       = next_unselected.has_value()
                                           ? next_unselected->spill_level
                                           : highest_selected + cell_extent;

        const float surface_level = 0.5f * (highest_selected + next_level);

        for (std::size_t i = 0; i < selected_count; ++i)
        {
            const auto& candidate     = selected[i];
            const float planned_water = std::max((surface_level - candidate.radial + surface_bias) * water_strength, 0.25f);

            plan.affected_samples.push_back({
                .coord = candidate.coord,
                .water = planned_water
            });
        }

        plan.wet_sample_count = static_cast<std::uint32_t>(selected_count);
        return plan;
    }

    // this is the click facing planner used by the bucket tool
    // resolve a sensible anchor first then either shrink or grow the connected volume around it depending on pickup vs placement
    std::optional<WaterPlan> build_targeted_water_plan(
        const GridView&     grid,
        const vec2          world_position,
        const std::uint32_t volume_cap,
        const bool          pickup)
    {
        if (!grid_ready(grid) || volume_cap == 0u) return std::nullopt;

        std::optional<ivec2> anchor;
        if (pickup) anchor = find_water_sample(grid, world_position);
        else
        {
            anchor = find_direct_water_sample(grid, world_position);
            if (!anchor.has_value()) anchor = find_water_anchor(grid, world_position);
        }

        if (!anchor.has_value()) return std::nullopt;

        ivec2         plan_start     = *anchor;
        std::uint32_t current_volume = 0u;
        if (pickup)
        {
            auto component = collect_water_component(grid, *anchor, false);
            if (component.empty()) return std::nullopt;

            current_volume = static_cast<std::uint32_t>(component.size());

            const auto desired_total = current_volume > volume_cap ? current_volume - volume_cap : 0u;
            return build_settled_water_plan(grid, plan_start, std::move(component), desired_total);
        }

        const auto& anchor_sample = grid.field_samples[global_field_index(grid, *anchor)];
        if (has_water(anchor_sample))
        {
            auto component = collect_water_component(grid, *anchor, false);
            current_volume = static_cast<std::uint32_t>(component.size());
            return build_settled_water_plan(grid, plan_start, std::move(component), current_volume + volume_cap);
        }

        return build_settled_water_plan(grid, plan_start, {}, volume_cap);
    }
}
