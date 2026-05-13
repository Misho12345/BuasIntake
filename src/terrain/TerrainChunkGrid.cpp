#include "pch.hpp"

#include "terrain/TerrainChunkGrid.hpp"

#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
    namespace
    {
        bool aabb_intersects_view_circle(const vec2 min, const vec2 max, const sf::View& view)
        {
            const vec2  center{ view.getCenter().x, view.getCenter().y };
            const vec2  size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
            const float radius    = std::sqrt(size.x * size.x + size.y * size.y) * 0.5f;
            const float closest_x = std::clamp(center.x, min.x, max.x);
            const float closest_y = std::clamp(center.y, min.y, max.y);
            const float dx        = center.x - closest_x;
            const float dy        = center.y - closest_y;
            return dx * dx + dy * dy <= radius * radius;
        }

        std::pair<ivec2, ivec2> owned_local_sample_bounds(const ChunkSettings& settings)
        {
            const auto padded_size = padded_field_size(settings);
            ivec2      min_coord{
                settings.chunk_coord.x == 0 ? 0 : static_cast<int>(settings.field_padding.x),
                settings.chunk_coord.y == 0 ? 0 : static_cast<int>(settings.field_padding.y)
            };
            ivec2 max_coord{
                settings.chunk_coord.x == settings.chunk_grid_size.x - 1
                    ? static_cast<int>(padded_size.x) - 1
                    : static_cast<int>(settings.field_padding.x + settings.field_size.x - 2u),
                settings.chunk_coord.y == settings.chunk_grid_size.y - 1
                    ? static_cast<int>(padded_size.y) - 1
                    : static_cast<int>(settings.field_padding.y + settings.field_size.y - 2u)
            };
            return { min_coord, max_coord };
        }
    }

    Result<void> TerrainChunkGrid::initialize(const b2WorldId world_id, const ChunkSettings& settings)
    {
        settings_ = settings;
        settings_.chunk_grid_size = chunk_grid_size();

        const auto total_chunk_count    = settings_.chunk_grid_size;
        const auto terrain_chunk_size   = settings_.chunk_size;
        const auto terrain_world_center = settings_.world_center;

        const vec2 total_world_size{
            terrain_chunk_size.x * static_cast<float>(total_chunk_count.x),
            terrain_chunk_size.y * static_cast<float>(total_chunk_count.y)
        };

        grid_min_ = terrain_world_center - total_world_size * 0.5f;
        grid_max_ = grid_min_ + total_world_size;

        display_min_ = { inf, inf };
        display_max_ = { -inf, -inf };

        chunks_.clear();
        chunks_.reserve(static_cast<std::size_t>(total_chunk_count.x * total_chunk_count.y));

        for (int y = 0; y < total_chunk_count.y; ++y)
        {
            for (int x = 0; x < total_chunk_count.x; ++x)
            {
                auto chunk_settings            = settings_;
                chunk_settings.chunk_coord     = { x, y };
                chunk_settings.chunk_grid_size = total_chunk_count;

                auto& chunk    = chunks_.emplace_back(world_id, chunk_settings);
                display_min_.x = std::min(display_min_.x, chunk.display_min().x);
                display_min_.y = std::min(display_min_.y, chunk.display_min().y);
                display_max_.x = std::max(display_max_.x, chunk.display_max().x);
                display_max_.y = std::max(display_max_.y, chunk.display_max().y);
            }
        }

        if (chunks_.empty())
        {
            display_min_ = grid_min_;
            display_max_ = grid_max_;
            return {};
        }

        for (auto& chunk : chunks_)
        {
            auto chunk_initialize_result = chunk.initialize();
            if (!chunk_initialize_result) return fail(chunk_initialize_result.error());

            auto dispatch_result = chunk.dispatch_generation();
            if (!dispatch_result) return fail(dispatch_result.error());
        }

        for (auto& chunk : chunks_)
        {
            auto finalize_result = chunk.finalize_generation();
            if (!finalize_result) return fail(finalize_result.error());
        }

        return {};
    }

    void TerrainChunkGrid::draw_gl(const sf::View& view) const
    {
        for (const auto& chunk : chunks_)
        {
            if (!aabb_intersects_view_circle(chunk.display_min(), chunk.display_max(), view)) continue;
            chunk.draw_gl(view);
        }
    }

    void TerrainChunkGrid::draw_water_gl(const sf::View& view) const
    {
        for (const auto& chunk : chunks_)
        {
            if (!aabb_intersects_view_circle(chunk.display_min(), chunk.display_max(), view)) continue;
            chunk.draw_water_gl(view);
        }
    }

    void TerrainChunkGrid::reset_field_layout(TerrainField& field) const
    {
        const auto total_chunk_count = settings_.chunk_grid_size;
        const auto padded_size       = padded_field_size(settings_);
        const auto stride            = chunk_sample_stride(settings_);
        const auto padding           = settings_.field_padding;
        const auto terrain_cell_size = cell_size(settings_);

        const uvec2 global_field_size = {
            static_cast<std::uint32_t>((total_chunk_count.x - 1) * stride.x + static_cast<int>(padded_size.x)),
            static_cast<std::uint32_t>((total_chunk_count.y - 1) * stride.y + static_cast<int>(padded_size.y))
        };

        const vec2 global_field_origin = {
            grid_min_.x - terrain_cell_size.x * static_cast<float>(padding.x),
            grid_min_.y - terrain_cell_size.y * static_cast<float>(padding.y)
        };

        field.reset(global_field_size, global_field_origin, terrain_cell_size);
    }

    Result<void> TerrainChunkGrid::sync_generated_field_to_global(TerrainField& field) const
    {
        for (const auto& chunk : chunks_)
        {
            auto chunk_field = chunk.readback_field();
            if (!chunk_field)
            {
                return fail("Failed to read back field for chunk ({}, {}): {}",
                            chunk.chunk_coord().x,
                            chunk.chunk_coord().y,
                            chunk_field.error().message);
            }
            if (chunk_field->empty()) continue;

            sync_chunk_field_to_global(field, chunk.chunk_coord(), *chunk_field);
        }

        return {};
    }

    void TerrainChunkGrid::sync_chunk_field_to_global(
        TerrainField&                   field,
        const ivec2                     chunk_coord,
        const std::span<const FieldSample> field_samples) const
    {
        if (field_samples.empty()) return;

        auto chunk_settings                = settings_;
        chunk_settings.chunk_coord         = chunk_coord;
        chunk_settings.chunk_grid_size     = settings_.chunk_grid_size;
        const auto  padded_size            = padded_field_size(chunk_settings);
        const auto  stride                 = chunk_sample_stride(chunk_settings);
        const auto  [owned_min, owned_max] = owned_local_sample_bounds(chunk_settings);
        const ivec2 chunk_base{ chunk_coord.x * stride.x, chunk_coord.y * stride.y };

        for (int y = owned_min.y; y <= owned_max.y; ++y)
        {
            for (int x = owned_min.x; x <= owned_max.x; ++x)
            {
                const ivec2 global_coord{ chunk_base.x + x, chunk_base.y + y };
                if (!field.is_valid_sample(global_coord)) continue;

                const auto  global_index    = field.sample_index(global_coord);
                const auto& next_sample     = field_samples[static_cast<std::size_t>(y) * padded_size.x + static_cast<std::size_t>(x)];

                field.samples()[global_index] = next_sample;
            }
        }
    }

    Result<void> TerrainChunkGrid::rebuild_dirty_chunks(
        TerrainField&                  field,
        const std::vector<bool>&        dirty_chunks,
        const bool                      rebuild_water,
        const bool                      rebuild_terrain_geometry,
        const bool                      refresh_terrain_visuals)
    {
        const bool any_dirty = std::ranges::any_of(dirty_chunks, [](const bool dirty) { return dirty; });
        if (!any_dirty) return {};

        struct PendingChunkRebuild final
        {
            std::size_t              chunk_index{ 0u };
            std::vector<FieldSample> field_samples{};
        };

        std::vector<PendingChunkRebuild> pending_chunks;
        pending_chunks.reserve(chunks_.size());
        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if (!dirty_chunks[i]) continue;

            pending_chunks.push_back({
                .chunk_index   = i,
                .field_samples = extract_chunk_field(field, chunks_[i].chunk_coord())
            });
        }

        for (auto& pending : pending_chunks)
        {
            if (auto upload_result = chunks_[pending.chunk_index].upload_rebuild_field(pending.field_samples);
                !upload_result)
            {
                return fail("Failed to rebuild chunk ({}, {}): {}",
                            chunks_[pending.chunk_index].chunk_coord().x,
                            chunks_[pending.chunk_index].chunk_coord().y,
                            upload_result.error().message);
            }
        }

        if (rebuild_terrain_geometry)
        {
            for (auto& pending : pending_chunks)
            {
                if (auto dispatch_result = chunks_[pending.chunk_index].dispatch_terrain_surface_rebuild();
                    !dispatch_result)
                {
                    return fail("Failed to dispatch terrain rebuild for chunk ({}, {}): {}",
                                chunks_[pending.chunk_index].chunk_coord().x,
                                chunks_[pending.chunk_index].chunk_coord().y,
                                dispatch_result.error().message);
                }
            }

            for (auto& pending : pending_chunks)
            {
                if (auto finalize_result = chunks_[pending.chunk_index].finalize_terrain_surface_rebuild(
                        pending.field_samples);
                    !finalize_result)
                {
                    return fail("Failed to finalize terrain rebuild for chunk ({}, {}): {}",
                                chunks_[pending.chunk_index].chunk_coord().x,
                                chunks_[pending.chunk_index].chunk_coord().y,
                                finalize_result.error().message);
                }
            }
        }
        else if (refresh_terrain_visuals)
        {
            for (auto& pending : pending_chunks)
            {
                chunks_[pending.chunk_index].refresh_cached_terrain_mesh(pending.field_samples);
            }
        }

        if (rebuild_water)
        {
            for (auto& pending : pending_chunks)
            {
                if (auto dispatch_result = chunks_[pending.chunk_index].dispatch_water_surface_rebuild();
                    !dispatch_result)
                {
                    return fail("Failed to dispatch water rebuild for chunk ({}, {}): {}",
                                chunks_[pending.chunk_index].chunk_coord().x,
                                chunks_[pending.chunk_index].chunk_coord().y,
                                dispatch_result.error().message);
                }
            }

            for (auto& pending : pending_chunks)
            {
                if (auto finalize_result = chunks_[pending.chunk_index].finalize_water_surface_rebuild();
                    !finalize_result)
                {
                    return fail("Failed to finalize water rebuild for chunk ({}, {}): {}",
                                chunks_[pending.chunk_index].chunk_coord().x,
                                chunks_[pending.chunk_index].chunk_coord().y,
                                finalize_result.error().message);
                }
            }
        }

        return {};
    }

    void TerrainChunkGrid::mark_chunks_covering_global_sample(const ivec2 coord, std::vector<bool>& dirty_chunks) const
    {
        const auto total_chunk_count = settings_.chunk_grid_size;
        const auto padded_size       = padded_field_size(settings_);
        const auto stride            = chunk_sample_stride(settings_);

        const int base_x = coord.x / std::max(stride.x, 1);
        const int base_y = coord.y / std::max(stride.y, 1);

        for (int chunk_y = std::max(0, base_y - 1); chunk_y <= std::min(total_chunk_count.y - 1, base_y + 1); ++chunk_y)
        {
            for (int chunk_x = std::max(0, base_x - 1); chunk_x <= std::min(total_chunk_count.x - 1, base_x + 1); ++chunk_x)
            {
                const int local_x = coord.x - chunk_x * stride.x;
                const int local_y = coord.y - chunk_y * stride.y;
                if (local_x < 0 || local_y < 0) continue;
                if (local_x >= static_cast<int>(padded_size.x) || local_y >= static_cast<int>(padded_size.y)) continue;

                dirty_chunks[flat_index({ chunk_x, chunk_y }, total_chunk_count)] = true;
            }
        }
    }

    std::vector<bool> TerrainChunkGrid::make_dirty_chunk_flags(const bool dirty) const
    {
        return std::vector(chunks_.size(), dirty);
    }

    std::size_t TerrainChunkGrid::chunk_count() const
    {
        return chunks_.size();
    }

    vec2 TerrainChunkGrid::grid_min() const
    {
        return grid_min_;
    }

    vec2 TerrainChunkGrid::grid_max() const
    {
        return grid_max_;
    }

    vec2 TerrainChunkGrid::display_min() const
    {
        return display_min_;
    }

    vec2 TerrainChunkGrid::display_max() const
    {
        return display_max_;
    }

    ivec2 TerrainChunkGrid::chunk_grid_size()
    {
        return { 10, 10 };
    }

    std::size_t TerrainChunkGrid::flat_index(const ivec2 chunk_index, const ivec2 chunk_count)
    {
        return static_cast<std::size_t>(chunk_index.y * chunk_count.x + chunk_index.x);
    }

    std::vector<TerrainChunkGrid::FieldSample> TerrainChunkGrid::extract_chunk_field(
        const TerrainField& field,
        const ivec2         chunk_coord) const
    {
        const auto  padded_size = padded_field_size(settings_);
        const auto  stride      = chunk_sample_stride(settings_);
        const ivec2 chunk_base{ chunk_coord.x * stride.x, chunk_coord.y * stride.y };

        std::vector<FieldSample> field_samples(
            static_cast<std::size_t>(padded_size.x) * static_cast<std::size_t>(padded_size.y));
        for (std::uint32_t y = 0; y < padded_size.y; ++y)
        {
            for (std::uint32_t x = 0; x < padded_size.x; ++x)
            {
                const ivec2 global_coord{ chunk_base.x + static_cast<int>(x), chunk_base.y + static_cast<int>(y) };
                field_samples[static_cast<std::size_t>(y) * padded_size.x + x] = field.sample(global_coord);
            }
        }

        return field_samples;
    }
}
