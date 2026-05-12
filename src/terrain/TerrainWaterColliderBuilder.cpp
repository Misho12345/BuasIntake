#include "pch.hpp"

#include "terrain/TerrainWaterColliderBuilder.hpp"

#include "terrain/TerrainContour.hpp"
#include "terrain/TerrainWetnessSystem.hpp"

namespace game::terrain
{
    namespace
    {
        std::uint64_t sample_key(const ivec2 coord)
        {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u |
                    static_cast<std::uint32_t>(coord.y);
        }

        std::vector<std::vector<vec2>> extract_contour_loops_from_scalar_field(
            const std::span<const float> values,
            const uvec2                  size,
            const vec2                   origin,
            const vec2                   cell_size)
        {
            if (values.empty() || size.x < 2u || size.y < 2u) return {};

            std::vector horizontal_edge_ids(
                static_cast<std::size_t>(size.y) * static_cast<std::size_t>(size.x - 1u), -1);

            std::vector vertical_edge_ids(static_cast<std::size_t>(size.y) * static_cast<std::size_t>(size.x), -1);
            std::vector<vec2> boundary_vertices;
            std::vector<TerrainGenerator::BoundaryEdge> boundary_edges;

            boundary_vertices.reserve(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
            boundary_edges.reserve(static_cast<std::size_t>(size.x - 1u) * static_cast<std::size_t>(size.y - 1u) * 2u);

            auto sample_value = [&values, size](const std::uint32_t x, const std::uint32_t y)
            {
                return values[static_cast<std::size_t>(y) * size.x + x];
            };

            auto grid_to_world = [origin, cell_size](const float x, const float y)
            {
                return vec2{ origin.x + x * cell_size.x, origin.y + y * cell_size.y };
            };

            for (std::uint32_t y = 0; y < size.y; ++y)
            {
                for (std::uint32_t x = 0; x < size.x; ++x)
                {
                    if (x + 1u < size.x)
                    {
                        const float a     = sample_value(x, y);
                        const float b     = sample_value(x + 1u, y);
                        const bool  ia    = a >= 0.0f;
                        const bool  ib    = b >= 0.0f;
                        const auto  index = static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x - 1u) + x;
                        if (ia != ib)
                        {
                            float       t = 0.5f;
                            const float d = b - a;
                            if (std::abs(d) > 1e-6f) t = std::clamp(-a / d, 0.0f, 1.0f);

                            horizontal_edge_ids[index] = static_cast<int>(boundary_vertices.size());
                            boundary_vertices.push_back(grid_to_world(
                                static_cast<float>(x) + t,
                                static_cast<float>(y)));
                        }
                    }

                    if (y + 1u < size.y)
                    {
                        const float a     = sample_value(x, y);
                        const float b     = sample_value(x, y + 1u);
                        const bool  ia    = a >= 0.0f;
                        const bool  ib    = b >= 0.0f;

                        const auto index =
                                static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(size.x) + x;

                        if (ia != ib)
                        {
                            float       t = 0.5f;
                            const float d = b - a;

                            if (std::abs(d) > 1e-6f) t = std::clamp(-a / d, 0.0f, 1.0f);

                            vertical_edge_ids[index] = static_cast<int>(boundary_vertices.size());
                            boundary_vertices.push_back(grid_to_world(
                                static_cast<float>(x),
                                static_cast<float>(y) + t));
                        }
                    }
                }
            }

            auto push_edge = [&boundary_edges](const int a, const int b)
            {
                if (a < 0 || b < 0) return;
                boundary_edges.push_back({
                    .a = static_cast<std::uint32_t>(a),
                    .b = static_cast<std::uint32_t>(b)
                });
            };

            for (std::uint32_t y = 0; y + 1u < size.y; ++y)
            {
                for (std::uint32_t x = 0; x + 1u < size.x; ++x)
                {
                    const float v0   = sample_value(x, y);
                    const float v1   = sample_value(x + 1u, y);
                    const float v2   = sample_value(x + 1u, y + 1u);
                    const float v3   = sample_value(x, y + 1u);
                    const bool  i0   = v0 >= 0.0f;
                    const bool  i1   = v1 >= 0.0f;
                    const bool  i2   = v2 >= 0.0f;
                    const bool  i3   = v3 >= 0.0f;
                    const int   mask = (i0 ? 1 : 0) | (i1 ? 2 : 0) | (i2 ? 4 : 0) | (i3 ? 8 : 0);
                    if (mask == 0 || mask == 15) continue;

                    const int e0 = horizontal_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x - 1u) + x];
                    const int e1 = vertical_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x) + (x + 1u)];
                    const int e2 = horizontal_edge_ids[static_cast<std::size_t>(y + 1u) * static_cast<std::size_t>(size.x - 1u) + x];
                    const int e3 = vertical_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x) + x];

                    const float center        = 0.25f * (v0 + v1 + v2 + v3);
                    const bool  inside_center = center >= 0.0f;

                    switch (mask)
                    {
                        case 1: push_edge(e0, e3); break;
                        case 2: push_edge(e1, e0); break;
                        case 3: push_edge(e1, e3); break;
                        case 4: push_edge(e2, e1); break;
                        case 5:
                            if (inside_center)
                            {
                                push_edge(e0, e1);
                                push_edge(e2, e3);
                            }
                            else
                            {
                                push_edge(e0, e3);
                                push_edge(e2, e1);
                            }
                            break;
                        case 6: push_edge(e2, e0); break;
                        case 7: push_edge(e2, e3); break;
                        case 8: push_edge(e3, e2); break;
                        case 9: push_edge(e0, e2); break;
                        case 10:
                            if (inside_center)
                            {
                                push_edge(e3, e0);
                                push_edge(e1, e2);
                            }
                            else
                            {
                                push_edge(e3, e2);
                                push_edge(e1, e0);
                            }
                            break;

                        case 11: push_edge(e1, e2); break;
                        case 12: push_edge(e3, e1); break;
                        case 13: push_edge(e0, e1); break;
                        case 14: push_edge(e3, e0); break;
                    }
                }
            }

            return TerrainContour::extract_contours(boundary_vertices, boundary_edges).loops;
        }
    }

    void TerrainWaterColliderBuilder::rebuild(TerrainColliderManager& collider_manager, const water::GridView& grid)
    {
        collider_manager.clear_water_colliders();
        if (grid.field_samples.empty() || grid.field_size.x < 2u || grid.field_size.y < 2u) return;

        const float min_cell_size      = std::min(grid.cell_size.x, grid.cell_size.y);
        const float min_segment_length = min_cell_size * 0.45f;
        const float collinear_epsilon  = min_cell_size * 0.30f;
        const float min_loop_area      = grid.cell_size.x * grid.cell_size.y * 0.5f;

        std::vector visited(grid.field_samples.size(), false);
        for (std::uint32_t y = 0; y < grid.field_size.y; ++y)
        {
            for (std::uint32_t x = 0; x < grid.field_size.x; ++x)
            {
                const ivec2 start_coord{ static_cast<int>(x), static_cast<int>(y) };
                const auto  start_index = water::global_field_index(grid, start_coord);

                if (visited[start_index]) continue;
                visited[start_index] = true;

                if (!water::has_water(grid.field_samples[start_index])) continue;

                auto component = water::collect_water_component(grid, start_coord, false);
                if (component.empty()) continue;

                TerrainSampleBounds               bounds{ .min = component.front(), .max = component.front() };
                std::unordered_set<std::uint64_t> component_keys;

                component_keys.reserve(component.size());

                for (const auto coord : component)
                {
                    visited[water::global_field_index(grid, coord)] = true;
                    component_keys.insert(sample_key(coord));
                    bounds.min.x = std::min(bounds.min.x, coord.x);
                    bounds.min.y = std::min(bounds.min.y, coord.y);
                    bounds.max.x = std::max(bounds.max.x, coord.x);
                    bounds.max.y = std::max(bounds.max.y, coord.y);
                }

                bounds = terrain_wetness::expand_sample_bounds(bounds, 1, grid.field_size);
                const uvec2 local_size{
                    static_cast<std::uint32_t>(bounds.max.x - bounds.min.x + 1),
                    static_cast<std::uint32_t>(bounds.max.y - bounds.min.y + 1)
                };

                std::vector local_values(
                    static_cast<std::size_t>(local_size.x) *
                    static_cast<std::size_t>(local_size.y), -1.0f);

                for (std::uint32_t local_y = 0; local_y < local_size.y; ++local_y)
                {
                    for (std::uint32_t local_x = 0; local_x < local_size.x; ++local_x)
                    {
                        const ivec2 global_coord{
                            bounds.min.x + static_cast<int>(local_x), bounds.min.y + static_cast<int>(local_y)
                        };
                        const auto index = static_cast<std::size_t>(local_y) * local_size.x + local_x;
                        if (!component_keys.contains(sample_key(global_coord))) continue;
                        local_values[index] =
                                water::combined_water_field(
                                    grid.field_samples[water::global_field_index(grid, global_coord)]);
                    }
                }

                auto loops =
                        extract_contour_loops_from_scalar_field(
                            local_values,
                            local_size,
                            {
                                grid.field_origin.x + static_cast<float>(bounds.min.x) * grid.cell_size.x,
                                grid.field_origin.y + static_cast<float>(bounds.min.y) * grid.cell_size.y
                            },
                            grid.cell_size);

                std::vector<std::vector<vec2>> simplified_loops;
                vec2 bounds_min{ inf, inf };
                vec2 bounds_max{ -inf, -inf };

                for (auto loop : loops)
                {
                    loop = TerrainContour::simplify_contour(loop, true, min_segment_length, collinear_epsilon);
                    if (loop.size() < 3u) continue;

                    float area = 0.0f;
                    for (std::size_t i = 0; i < loop.size(); ++i)
                    {
                        const auto& a = loop[i];
                        const auto& b = loop[(i + 1u) % loop.size()];

                        area += a.x * b.y - b.x * a.y;

                        bounds_min.x  = std::min(bounds_min.x, a.x);
                        bounds_min.y  = std::min(bounds_min.y, a.y);
                        bounds_max.x  = std::max(bounds_max.x, a.x);
                        bounds_max.y  = std::max(bounds_max.y, a.y);
                    }
                    area *= 0.5f;
                    if (area < 0.0f)
                    {
                        std::ranges::reverse(loop);
                        area = -area;
                    }
                    if (area < min_loop_area) continue;
                    simplified_loops.push_back(std::move(loop));
                }

                if (simplified_loops.empty()) continue;

                auto& blob      = collider_manager.emplace_water_blob();
                blob.bounds_min = bounds_min;
                blob.bounds_max = bounds_max;
                blob.collider.build({}, {}, simplified_loops);
                blob.collider.set_water_enabled(true);
            }
        }
    }
}
