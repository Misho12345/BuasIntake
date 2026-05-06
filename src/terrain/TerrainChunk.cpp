#include "pch.hpp"

#include "TerrainChunk.hpp"

#include "gfx/MeshBuilders.hpp"
#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
    namespace
    {
        float clamp01(const float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        sf::Color lerp_color(const sf::Color& a, const sf::Color& b, const float t)
        {
            const auto blend = clamp01(t);
            auto channel = [blend](const std::uint8_t lhs, const std::uint8_t rhs)
            { return static_cast<std::uint8_t>(std::lround(std::lerp(static_cast<float>(lhs), static_cast<float>(rhs), blend))); };

            return {
                channel(a.r, b.r),
                channel(a.g, b.g),
                channel(a.b, b.b),
                channel(a.a, b.a)
            };
        }

        float bilerp(const float a, const float b, const float c, const float d, const float tx, const float ty)
        {
            const float ab = std::lerp(a, b, tx);
            const float cd = std::lerp(c, d, tx);
            return std::lerp(ab, cd, ty);
        }

        template <typename Accessor>
        float sample_field_channel(const std::span<const TerrainChunk::FieldSample> field_samples,
                                   const ChunkSettings& settings,
                                   const vec2 world_position,
                                   Accessor&& accessor)
        {
            if (field_samples.empty())
                return 0.0f;

            const auto size = padded_field_size(settings);
            const auto terrain_cell_size = cell_size(settings);
            const auto origin = field_origin(settings);

            const float gx = (world_position.x - origin.x) / terrain_cell_size.x;
            const float gy = (world_position.y - origin.y) / terrain_cell_size.y;

            const float clamped_x = std::clamp(gx, 0.0f, static_cast<float>(size.x - 1u));
            const float clamped_y = std::clamp(gy, 0.0f, static_cast<float>(size.y - 1u));

            const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
            const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
            const auto x1 = std::min(x0 + 1u, size.x - 1u);
            const auto y1 = std::min(y0 + 1u, size.y - 1u);

            const float tx = clamped_x - static_cast<float>(x0);
            const float ty = clamped_y - static_cast<float>(y0);

            auto channel_at = [&](const std::uint32_t x, const std::uint32_t y)
            { return accessor(field_samples[static_cast<std::size_t>(y) * size.x + x]); };

            return bilerp(channel_at(x0, y0), channel_at(x1, y0), channel_at(x0, y1), channel_at(x1, y1), tx, ty);
        }

    }

    TerrainChunk::TerrainChunk(const b2WorldId world_id, const ChunkSettings& settings) : settings_{settings}, collider_{world_id}
    {
        const auto chunk_min = terrain::chunk_min(settings_);
        const auto chunk_max = terrain::chunk_max(settings_);

        const auto terrain_cell_size = cell_size(settings_);
        const vec2 padding_extent{terrain_cell_size.x * static_cast<float>(settings_.field_padding.x),
                                  terrain_cell_size.y * static_cast<float>(settings_.field_padding.y)};
        display_min_ = {chunk_min.x - padding_extent.x, chunk_min.y - padding_extent.y};
        display_max_ = {chunk_max.x + padding_extent.x, chunk_max.y + padding_extent.y};
    }

    Result<void> TerrainChunk::initialize()
    {
        if (auto generator_result = generator_.initialize(settings_); !generator_result)
        {
            return fail("Failed to initialize terrain generator for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        generator_result.error().message);
        }

        if (auto renderable_result = renderable_.initialize(); !renderable_result)
        {
            return fail("Failed to initialize terrain renderer for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        renderable_result.error().message);
        }

        if (auto water_renderable_result = water_renderable_.initialize(); !water_renderable_result)
        {
            return fail("Failed to initialize water renderer for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        water_renderable_result.error().message);
        }

        return {};
    }

    void TerrainChunk::draw_gl(const sf::View& view) const
    {
        renderable_.draw(mesh_, view);
    }

    void TerrainChunk::draw_water_gl(const sf::View& view) const
    {
        water_renderable_.draw(water_mesh_, view);
    }

    Result<void> TerrainChunk::dispatch_generation()
    {
        if (generation_dispatched_ || generation_finalized_) return {};

        if (auto dispatch_result = generator_.dispatch(); !dispatch_result)
        {
            return fail("Failed to dispatch terrain generation for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        dispatch_result.error().message);
        }

        generation_dispatched_ = true;
        return {};
    }

    Result<void> TerrainChunk::finalize_generation()
    {
        if (generation_finalized_) return {};
        if (!generation_dispatched_)
            TRY(dispatch_generation());

        auto field_samples = generator_.read_field();
        if (!field_samples)
            return fail(field_samples.error());

        auto terrain_result = read_scored_surface();
        if (!terrain_result)
            return fail(terrain_result.error());

        auto water_result = rebuild_scored_surface(TerrainGenerator::water_channel_index, 0.0f);
        if (!water_result)
            return fail(water_result.error());

        build_chunk(*terrain_result, *water_result, *field_samples);
        generation_dispatched_ = false;
        generation_finalized_ = true;
        return {};
    }

    Result<void> TerrainChunk::rebuild_from_field(const std::span<const FieldSample> field_samples,
                                                  const bool smooth_water,
                                                  const bool rebuild_water)
    {
        TRY(generator_.upload_field(field_samples));

        if (smooth_water)
        {
            // Water gets blurred on the GPU first, so rebuild from that version instead of the raw edit.
            TRY(generator_.smooth_water_field());

            auto effective_field_samples = generator_.read_field();
            if (!effective_field_samples)
                return fail(effective_field_samples.error());

            TRY(rebuild_chunk_meshes(*effective_field_samples, rebuild_water));
            return {};
        }

        TRY(rebuild_chunk_meshes(field_samples, rebuild_water));

        return {};
    }

    Result<std::vector<TerrainChunk::FieldSample>> TerrainChunk::readback_field() const
    {
        return generator_.read_field();
    }

    ivec2 TerrainChunk::chunk_coord() const
    {
        return settings_.chunk_coord;
    }
    vec2 TerrainChunk::display_min() const
    {
        return display_min_;
    }
    vec2 TerrainChunk::display_max() const
    {
        return display_max_;
    }

    Result<TerrainContour::ScoredResult> TerrainChunk::read_scored_surface()
    {
        auto readback_result = generator_.readback();
        if (!readback_result)
            return fail(readback_result.error());
        return TerrainContour::score_and_filter(std::move(*readback_result), settings_);
    }

    Result<TerrainContour::ScoredResult> TerrainChunk::rebuild_scored_surface(const std::uint32_t channel_index, const float iso)
    {
        TRY(generator_.dispatch_surface_rebuild(channel_index, iso));
        return read_scored_surface();
    }

    Result<void> TerrainChunk::rebuild_chunk_meshes(const std::span<const FieldSample> field_samples, const bool rebuild_water)
    {
        auto terrain_result = rebuild_scored_surface(TerrainGenerator::terrain_channel_index, 0.0f);
        if (!terrain_result)
            return fail(terrain_result.error());

        build_terrain_mesh(terrain_result->mesh_vertices, terrain_result->mesh_indices, field_samples);
        collider_.build(terrain_result->collider_loops, terrain_result->collider_paths);

        if (!rebuild_water)
            return {};

        auto water_result = rebuild_scored_surface(TerrainGenerator::water_channel_index, 0.0f);
        if (!water_result)
            return fail(water_result.error());

        build_water_mesh(water_result->mesh_vertices, water_result->mesh_indices);
        return {};
    }

    void TerrainChunk::build_chunk(const TerrainContour::ScoredResult& terrain_result,
                                   const TerrainContour::ScoredResult& water_result,
                                   const std::span<const FieldSample> field_samples)
    {
        build_terrain_mesh(terrain_result.mesh_vertices, terrain_result.mesh_indices, field_samples);
        build_water_mesh(water_result.mesh_vertices, water_result.mesh_indices);
        collider_.build(terrain_result.collider_loops, terrain_result.collider_paths);
    }

    void TerrainChunk::build_terrain_mesh(const std::vector<vec2>& vertices,
                                          const std::vector<std::uint32_t>& indices,
                                          const std::span<const FieldSample> field_samples)
    {
        std::vector<sf::Vertex> mesh_vertices;
        mesh_vertices.reserve(vertices.size());

        const auto radius = std::max(settings_.planet_radius, 0.001f);

        for (const auto& point : vertices)
        {
            const vec2 offset{point.x - settings_.world_center.x, point.y - settings_.world_center.y};

            const auto dist_from_center = std::sqrt(offset.x * offset.x + offset.y * offset.y);
            const auto gradient = clamp01(dist_from_center / radius);

            const auto wetness =
                std::clamp(sample_field_channel(field_samples, settings_, point, [](const FieldSample& sample) { return sample.wetness; }),
                           0.0f,
                           1.0f);

            const auto greenness = std::clamp(
                sample_field_channel(field_samples, settings_, point, [](const FieldSample& sample) { return sample.greenness; }),
                0.0f,
                1.0f);

            // The fragment shader reads wetness from texcoord.x, depth from texcoord.y, and greenness from color.a.
            auto color = lerp_color(0x3F2C1C_rgb, 0xD6B27B_rgb, gradient);
            color.a = static_cast<std::uint8_t>(std::lround(greenness * 255.0f));

            mesh_vertices.emplace_back(point, color, vec2{wetness, gradient});
        }

        mesh_.set_data(mesh_vertices, indices);
    }

    void TerrainChunk::build_water_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
        {
            const std::vector<sf::Vertex> empty_vertices;
            const std::vector<std::uint32_t> empty_indices;
            water_mesh_.set_data(empty_vertices, empty_indices);
            return;
        }

        const auto mesh_vertices = gfx::build_tinted_vertices(vertices, 0xE8F8FFC4_rgba);
        water_mesh_.set_data(mesh_vertices, indices);
    }

}
