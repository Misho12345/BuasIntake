#include "pch.hpp"

#include "water/WaterInteraction.hpp"

namespace game::water
{
    namespace
    {
        float radial_dist(const vec2 point, const vec2 center)
        {
            const vec2 offset = point - center;
            return std::sqrt(offset.x * offset.x + offset.y * offset.y);
        }

        struct WaterCandidate final
        {
            ivec2 coord{ 0, 0 };
            float radial{ 0.0f };
            float click_dist_sq{ 0.0f };
        };

        std::uint64_t sample_key(const ivec2 coord)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u) | static_cast<std::uint32_t>(coord.y);
        }

        bool sample_has_water(const FieldSample& sample)
        {
            return std::min(-sample.terrain, sample.water) > 1e-4f;
        }

        bool grid_ready(const GridView& grid)
        {
            return !grid.field_samples.empty() && grid.field_size.x > 0u && grid.field_size.y > 0u;
        }

        bool has_inward_support(const GridView& grid, const ivec2 coord)
        {
            if (!is_valid_global_sample(grid, coord)) return false;
            // Water needs a bit of solid support toward the planet core or it will look like it is hanging outward.
            const float sample_radial = radial_dist(global_sample_world_position(grid, coord), grid.world_center);
            const float radial_tolerance = std::min(grid.cell_size.x, grid.cell_size.y) * 0.25f;
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    if (x == 0 && y == 0) continue;

                    const ivec2 neighbor{ coord.x + x, coord.y + y };

                    if (!is_valid_global_sample(grid, neighbor)) continue;

                    const auto& neighbor_sample = grid.field_samples[global_field_index(grid, neighbor)];
                    if (neighbor_sample.terrain < 0.0f) continue;

                    const float neighbor_radial = radial_dist(global_sample_world_position(grid, neighbor), grid.world_center);
                    if (neighbor_radial + radial_tolerance < sample_radial) return true;
                }
            }

            return false;
        }

        bool can_start_water(const GridView& grid, const ivec2 coord)
        {
            if (!is_valid_global_sample(grid, coord)) return false;

            const auto& sample = grid.field_samples[global_field_index(grid, coord)];
            if (sample.terrain >= 0.0f) return false;

            return has_inward_support(grid, coord);
        }

        std::optional<ivec2> find_direct_water_sample(const GridView& grid, const vec2 world_position)
        {
            if (!grid_ready(grid)) return std::nullopt;

            const ivec2 coord{
                std::clamp(static_cast<int>(std::lround((world_position.x - grid.field_origin.x) / grid.cell_size.x)),
                           0,
                           static_cast<int>(grid.field_size.x) - 1),
                std::clamp(static_cast<int>(std::lround((world_position.y - grid.field_origin.y) / grid.cell_size.y)),
                           0,
                           static_cast<int>(grid.field_size.y) - 1)
            };

            if (!is_valid_global_sample(grid, coord) ||
                !sample_has_water(grid.field_samples[global_field_index(grid, coord)]))
                return std::nullopt;

            return coord;
        }
    }

    bool has_water(const FieldSample& sample)
    {
        return sample_has_water(sample);
    }

    float combined_water_field(const FieldSample& sample)
    {
        return std::min(-sample.terrain, sample.water);
    }

    bool is_valid_global_sample(const GridView& grid, const ivec2 coord)
    {
        return coord.x >= 0 && coord.y >= 0 && coord.x < static_cast<int>(grid.field_size.x) &&
               coord.y < static_cast<int>(grid.field_size.y);
    }

    std::size_t global_field_index(const GridView& grid, const ivec2 coord)
    {
        return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(grid.field_size.x) + static_cast<std::size_t>(coord.x);
    }

    vec2 global_sample_world_position(const GridView& grid, const ivec2 coord)
    {
        return {
            grid.field_origin.x + static_cast<float>(coord.x) * grid.cell_size.x,
            grid.field_origin.y + static_cast<float>(coord.y) * grid.cell_size.y
        };
    }

    int solid_neighbor_count(const GridView& grid, const ivec2 coord)
    {
        int count = 0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;

                const ivec2 neighbor{ coord.x + x, coord.y + y };
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (grid.field_samples[global_field_index(grid, neighbor)].terrain >= 0.0f) ++count;
            }
        }

        return count;
    }

    bool has_water_neighbor(const GridView& grid, const ivec2 coord)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;

                const ivec2 neighbor{ coord.x + x, coord.y + y };
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (has_water(grid.field_samples[global_field_index(grid, neighbor)])) return true;
            }
        }

        return false;
    }

    ivec2 settle_water_anchor(const GridView& grid, const ivec2 anchor)
    {
        if (!is_valid_global_sample(grid, anchor)) return anchor;
        if (grid.field_samples[global_field_index(grid, anchor)].terrain >= 0.0f) return anchor;

        static constexpr std::array neighbors{
            ivec2{ 1, 0 },
            ivec2{ -1, 0 },
            ivec2{ 0, 1 },
            ivec2{ 0, -1 }
        };

        auto sample_radial = [&grid](const ivec2 coord)
        { return radial_dist(global_sample_world_position(grid, coord), grid.world_center); };

        ivec2 downhill_anchor = anchor;
        // First walk toward a locally lower basin, then do a bounded flood so nearby connected pockets can win.
        for (int iteration = 0; iteration < 128; ++iteration)
        {
            ivec2 best_neighbor = downhill_anchor;
            float best_neighbor_radial = sample_radial(downhill_anchor);
            for (const auto& offset : neighbors)
            {
                const ivec2 neighbor{ downhill_anchor.x + offset.x, downhill_anchor.y + offset.y };

                if (!is_valid_global_sample(grid, neighbor) ||
                    grid.field_samples[global_field_index(grid, neighbor)].terrain >= 0.0f ||
                    !has_inward_support(grid, neighbor))
                    continue;

                const float neighbor_radial = sample_radial(neighbor);
                if (neighbor_radial < best_neighbor_radial - 1e-5f)
                {
                    best_neighbor_radial = neighbor_radial;
                    best_neighbor = neighbor;
                }
            }

            if (best_neighbor.x == downhill_anchor.x && 
                best_neighbor.y == downhill_anchor.y)
                break;

            downhill_anchor = best_neighbor;
        }

        std::queue<ivec2> frontier;
        std::unordered_set<std::uint64_t> visited;
        frontier.push(downhill_anchor);
        visited.insert(sample_key(downhill_anchor));

        ivec2 best_coord = downhill_anchor;
        float best_radial = sample_radial(downhill_anchor);

        static constexpr int max_local_search_radius = 32;

        while (!frontier.empty())
        {
            const auto coord = frontier.front();
            frontier.pop();

            const float radial = sample_radial(coord);
            if (radial < best_radial - 1e-5f)
            {
                best_radial = radial;
                best_coord = coord;
            }

            for (const auto& offset : neighbors)
            {
                const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
                if (!is_valid_global_sample(grid, neighbor)) continue;

                if (std::abs(neighbor.x - downhill_anchor.x) > max_local_search_radius ||
                    std::abs(neighbor.y - downhill_anchor.y) > max_local_search_radius)
                    continue;

                if (grid.field_samples[global_field_index(grid, neighbor)].terrain >= 0.0f) continue;
                if (!has_inward_support(grid, neighbor)) continue;

                const auto key = sample_key(neighbor);
                if (!visited.insert(key).second) continue;

                frontier.push(neighbor);
            }
        }

        return best_coord;
    }

    std::optional<ivec2> find_water_sample(const GridView& grid, const vec2 world_position)
    {
        if (!grid_ready(grid)) return std::nullopt;

        // When picking up water, prefer the closest visible wet sample instead of jumping to a deeper connected blob.
        const ivec2 center{
            std::clamp(static_cast<int>(std::lround((world_position.x - grid.field_origin.x) / grid.cell_size.x)),
                       0,
                       static_cast<int>(grid.field_size.x) - 1),
            std::clamp(static_cast<int>(std::lround((world_position.y - grid.field_origin.y) / grid.cell_size.y)),
                       0,
                       static_cast<int>(grid.field_size.y) - 1)
        };
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

                const vec2 sample_world = global_sample_world_position(grid, coord);
                const vec2 click_delta = sample_world - world_position;
                const float click_dist_sq = click_delta.x * click_delta.x + click_delta.y * click_delta.y;

                const WaterCandidate candidate{ coord, radial_dist(sample_world, grid.world_center), click_dist_sq };

                if (!best_candidate.has_value() || candidate.click_dist_sq < best_candidate->click_dist_sq ||
                    (std::abs(candidate.click_dist_sq - best_candidate->click_dist_sq) <= 1e-5f &&
                     candidate.radial < best_candidate->radial))
                {
                    best_candidate = candidate;
                }
            }
        }

        if (!best_candidate.has_value()) return std::nullopt;
        return best_candidate->coord;
    }

    std::optional<ivec2> find_water_anchor(const GridView& grid, const vec2 world_position)
    {
        if (!grid_ready(grid)) return std::nullopt;

        // For placement, probe just beyond the click first so water snaps to the visible cavity edge before settling.
        const ivec2 hit_coord{
	        std::clamp(static_cast<int>(std::lround((world_position.x - grid.field_origin.x) / grid.cell_size.x)),
	                   0,
	                   static_cast<int>(grid.field_size.x) - 1),
	        std::clamp(static_cast<int>(std::lround((world_position.y - grid.field_origin.y) / grid.cell_size.y)),
	                   0,
	                   static_cast<int>(grid.field_size.y) - 1)
        };

        const vec2 up = normalize(world_position - grid.world_center, {0.0f, 1.0f});
        const float cell_extent = std::min(grid.cell_size.x, grid.cell_size.y);
        std::optional<WaterCandidate> best_candidate;
        for (int step = 1; step <= 24; ++step)
        {
	        const vec2  probe_world = world_position + up * (cell_extent * 0.35f * static_cast<float>(step));
	        const ivec2 probe_coord{
		        std::clamp(static_cast<int>(std::lround((probe_world.x - grid.field_origin.x) / grid.cell_size.x)),
		                   0,
		                   static_cast<int>(grid.field_size.x) - 1),
		        std::clamp(static_cast<int>(std::lround((probe_world.y - grid.field_origin.y) / grid.cell_size.y)),
		                   0,
		                   static_cast<int>(grid.field_size.y) - 1)
	        };

            if (!is_valid_global_sample(grid, probe_coord)) break;
            if (grid.field_samples[global_field_index(grid, probe_coord)].terrain >= 0.0f) break;
            if (!has_inward_support(grid, probe_coord)) continue;

            const vec2 probe_sample_world = global_sample_world_position(grid, probe_coord);
            const vec2 delta = probe_sample_world - world_position;
            const float click_dist_sq = delta.x * delta.x + delta.y * delta.y;

            const WaterCandidate candidate{
                .coord = probe_coord,
                .radial = radial_dist(probe_sample_world, grid.world_center),
                .click_dist_sq = click_dist_sq
	        };

	        if (!best_candidate.has_value() ||
		        candidate.click_dist_sq < best_candidate->click_dist_sq ||
		        (std::abs(
				        candidate.click_dist_sq - best_candidate->click_dist_sq) <= 1e-5f &&
			        candidate.radial < best_candidate->radial))
	        {
		        best_candidate = candidate;
	        }
        }

        if (best_candidate.has_value()) return best_candidate->coord;

        if (!is_valid_global_sample(grid, hit_coord)) return std::nullopt;

        if (grid.field_samples[global_field_index(grid, hit_coord)].terrain < 0.0f && 
            has_inward_support(grid, hit_coord))
            return hit_coord;

        return std::nullopt;
    }

    std::vector<ivec2> collect_water_component(const GridView& grid, const ivec2 start_coord, const bool include_diagonals)
    {
        if (!is_valid_global_sample(grid, start_coord))
            return {};
        if (!has_water(grid.field_samples[global_field_index(grid, start_coord)]))
            return {};

        static constexpr std::array orthogonal_neighbors{ ivec2{ 1, 0 }, ivec2{ -1, 0 }, ivec2{ 0, 1 }, ivec2{ 0, -1 } };
        static constexpr std::array diagonal_neighbors{ ivec2{ 1, 1 }, ivec2{ 1, -1 }, ivec2{ -1, 1 }, ivec2{ -1, -1 } };

        std::queue<ivec2> frontier;
        std::unordered_set<std::uint64_t> visited;
        std::vector<ivec2> component;

        frontier.push(start_coord);
        visited.insert(sample_key(start_coord));

        while (!frontier.empty())
        {
            const auto coord = frontier.front();
            frontier.pop();
            component.push_back(coord);

            for (const auto& offset : orthogonal_neighbors)
            {
                const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (!has_water(grid.field_samples[global_field_index(grid, neighbor)])) continue;

                const auto key = sample_key(neighbor);
                if (!visited.insert(key).second) continue;

                frontier.push(neighbor);
            }

            if (!include_diagonals) continue;

            for (const auto& offset : diagonal_neighbors)
            {
                const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
                if (!is_valid_global_sample(grid, neighbor)) continue;
                if (!has_water(grid.field_samples[global_field_index(grid, neighbor)])) continue;

                const auto key = sample_key(neighbor);
                if (!visited.insert(key).second) continue;

                frontier.push(neighbor);
            }
        }

        return component;
    }

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

        const float cell_extent = std::min(grid.cell_size.x, grid.cell_size.y);
        const float water_strength = 8.0f;
        const float surface_bias = cell_extent * 0.5f;
        const vec2 start_world = global_sample_world_position(grid, start_coord);

        struct FillCandidate final
        {
            ivec2 coord{0, 0};
            float radial{0.0f};
            float spill_level{0.0f};
            float start_dist_sq{0.0f};
        };

        struct FrontierNode final
        {
            ivec2 coord{0, 0};
            float radial{0.0f};
            float spill_level{0.0f};
            float start_dist_sq{0.0f};
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
        std::unordered_map<std::uint64_t, float> best_spill_levels;
        best_spill_levels.reserve(static_cast<std::size_t>(desired_wet_sample_count) * 6u + 32u);

        auto push_neighbor = [&](const ivec2 coord, const float incoming_spill_level)
        {
            if (!is_valid_global_sample(grid, coord)) return;

            const auto& sample = grid.field_samples[global_field_index(grid, coord)];
            if (sample.terrain >= 0.0f) return;

            const vec2 sample_world = global_sample_world_position(grid, coord);
            const float radial = radial_dist(sample_world, grid.world_center);
            const float spill_level = std::max(incoming_spill_level, radial);
            const auto key = sample_key(coord);

            if (const auto it = best_spill_levels.find(key);
                it != best_spill_levels.end() && spill_level >= it->second - 1e-5f)
                return;

            best_spill_levels[key] = spill_level;
            const vec2 start_delta = sample_world - start_world;
            frontier.push({
                .coord = coord,
                .radial = radial,
                .spill_level = spill_level,
                .start_dist_sq = start_delta.lengthSquared()
            });
        };

        push_neighbor(start_coord, radial_dist(start_world, grid.world_center));

        std::vector<FillCandidate> selected;
        selected.reserve(desired_wet_sample_count);
        std::optional<FillCandidate> next_unselected;

        while (!frontier.empty())
        {
            const auto node = frontier.top();
            frontier.pop();

            const auto key = sample_key(node.coord);
            const auto best_it = best_spill_levels.find(key);
            if (best_it == best_spill_levels.end() || node.spill_level > best_it->second + 1e-5f)
                continue;

            const FillCandidate candidate{
                .coord = node.coord,
                .radial = node.radial,
                .spill_level = node.spill_level,
                .start_dist_sq = node.start_dist_sq
            };

            if (selected.size() < desired_wet_sample_count)
            {
                selected.push_back(candidate);
            }
            else
            {
                next_unselected = candidate;
                break;
            }

            for (const auto& offset : neighbors)
            {
                push_neighbor({node.coord.x + offset.x, node.coord.y + offset.y}, node.spill_level);
            }
        }

        const auto selected_count = selected.size();
        if (selected_count == 0u) return plan;

        const float highest_selected = selected.back().spill_level;
        const float next_level = next_unselected.has_value() ? next_unselected->spill_level : highest_selected + cell_extent;
        const float surface_level = 0.5f * (highest_selected + next_level);

        for (std::size_t i = 0; i < selected_count; ++i)
        {
            const auto& candidate = selected[i];
            const float planned_water = std::max((surface_level - candidate.radial + surface_bias) * water_strength, 0.25f);
            plan.affected_samples.push_back({
                .coord = candidate.coord,
                .water = planned_water
            });
        }

        plan.wet_sample_count = static_cast<std::uint32_t>(selected_count);
        return plan;
    }

    std::uint32_t water_volume_at_anchor(const GridView& grid, const ivec2 anchor, ivec2* const plan_start)
    {
        if (plan_start != nullptr) *plan_start = anchor;

        if (!is_valid_global_sample(grid, anchor)) return 0u;
        if (!has_water(grid.field_samples[global_field_index(grid, anchor)])) return 0u;

        auto component = collect_water_component(grid, anchor, false);
        if (component.empty()) return 0u;

        ivec2 lowest_coord = component.front();
        float lowest_radial = radial_dist(global_sample_world_position(grid, lowest_coord), grid.world_center);
        for (const auto coord : component)
        {
            const float radial = radial_dist(global_sample_world_position(grid, coord), grid.world_center);
            if (radial > lowest_radial + 1e-5f) continue;

            if (std::abs(radial - lowest_radial) <= 1e-5f &&
                (coord.y > lowest_coord.y || (coord.y == lowest_coord.y && coord.x >= lowest_coord.x)))
                continue;

            lowest_coord = coord;
            lowest_radial = radial;
        }

        if (plan_start != nullptr)
            *plan_start = lowest_coord;
        return static_cast<std::uint32_t>(component.size());
    }

    std::optional<WaterPlan> build_targeted_water_plan(
	    const GridView&      grid,
	    const vec2           world_position,
	    const std::uint32_t  volume_cap,
	    const bool           pickup,
	    std::uint32_t* const existing_volume)
    {
        if (existing_volume != nullptr) *existing_volume = 0u;
        if (!grid_ready(grid) || volume_cap == 0u)  return std::nullopt;

        std::optional<ivec2> anchor;
        if (pickup)
        {
            anchor = find_water_sample(grid, world_position);
        }
        else
        {
            anchor = find_direct_water_sample(grid, world_position);
            if (!anchor.has_value())
                anchor = find_water_anchor(grid, world_position);
        }

        if (!anchor.has_value()) return std::nullopt;

        ivec2 plan_start = *anchor;
        std::uint32_t current_volume = 0u;
        if (pickup)
        {
            auto component = collect_water_component(grid, *anchor, false);
            if (component.empty()) return std::nullopt;

            current_volume = static_cast<std::uint32_t>(component.size());
            if (existing_volume != nullptr) *existing_volume = current_volume;

            const auto desired_total = current_volume > volume_cap ? current_volume - volume_cap : 0u;
            return build_settled_water_plan(grid, plan_start, std::move(component), desired_total);
        }
        else
        {
            const auto& anchor_sample = grid.field_samples[global_field_index(grid, *anchor)];
            if (has_water(anchor_sample))
            {
                auto component = collect_water_component(grid, *anchor, false);
                current_volume = static_cast<std::uint32_t>(component.size());
                if (existing_volume != nullptr) *existing_volume = current_volume;
                return build_settled_water_plan(grid, plan_start, std::move(component), current_volume + volume_cap);
            }
        }
        if (existing_volume != nullptr)
            *existing_volume = current_volume;

        return build_settled_water_plan(grid, plan_start, {}, volume_cap);
    }

    std::optional<WaterPlan> build_water_plan(
	    const GridView&     grid,
	    const ivec2         start_coord,
	    const std::uint32_t desired_wet_sample_count,
	    const bool          preserve_existing_water)
    {
        if (!is_valid_global_sample(grid, start_coord)) return std::nullopt;
        if (grid.field_samples[global_field_index(grid, start_coord)].terrain >= 0.0f) return std::nullopt;
        if (!preserve_existing_water && !can_start_water(grid, start_coord)) return std::nullopt;

        auto previous_water = preserve_existing_water
	                              ? collect_water_component(grid, start_coord, false)
	                              : std::vector<ivec2>{};

        ivec2 settled_start = start_coord;
        if (!previous_water.empty())
        {
	        std::ranges::sort(
		        previous_water,
		        [&grid](const ivec2 lhs, const ivec2 rhs)
		        {
			        return radial_dist(global_sample_world_position(grid, lhs), grid.world_center) <
					        radial_dist(global_sample_world_position(grid, rhs), grid.world_center);
		        });
	        settled_start = previous_water.front();
        }

        return build_settled_water_plan(grid, settled_start, std::move(previous_water), desired_wet_sample_count);
    }
}
