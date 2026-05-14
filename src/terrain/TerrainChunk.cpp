#include "pch.hpp"

#include "TerrainChunk.hpp"

#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
    namespace
    {
        float clamp01(const float value) { return std::clamp(value, 0.0f, 1.0f); }

        sf::Color lerp_color(const sf::Color& a, const sf::Color& b, const float t)
        {
            const auto blend   = clamp01(t);
            auto       channel = [blend](const std::uint8_t lhs, const std::uint8_t rhs)
            {
                return static_cast<std::uint8_t>(std::lround(
                    std::lerp(static_cast<float>(lhs), static_cast<float>(rhs), blend)));
            };

            return {
                channel(a.r, b.r),
                channel(a.g, b.g),
                channel(a.b, b.b),
                channel(a.a, b.a)
            };
        }

        template <typename Accessor>
        float sample_field_channel(
            const std::span<const TerrainChunk::FieldSample> field_samples,
            const ChunkSettings&                             settings,
            const vec2                                       world_position,
            Accessor&&                                       accessor)
        {
            return sample_field_channel_bilinear(
                field_samples,
                padded_field_size(settings),
                field_origin(settings),
                cell_size(settings),
                world_position,
                std::forward<Accessor>(accessor));
        }
    }

    TerrainChunk::TerrainChunk(const b2WorldId world_id, const ChunkSettings& settings) : settings_{ settings },
        collider_{ world_id }
    {
        const auto chunk_min = terrain::chunk_min(settings_);
        const auto chunk_max = terrain::chunk_max(settings_);

        const auto terrain_cell_size = cell_size(settings_);
        const vec2 padding_extent = terrain_cell_size * settings_.field_padding;
        display_min_ = chunk_min - padding_extent;
        display_max_ = chunk_max + padding_extent;
    }

    Result<void> TerrainChunk::initialize()
    {
        if (auto generator_result = generator_.initialize(settings_);
            !generator_result)
        {
            return fail("Failed to initialize terrain generator for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        generator_result.error().message);
        }

        if (auto renderable_result = renderable_.initialize();
            !renderable_result)
        {
            return fail("Failed to initialize terrain renderer for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        renderable_result.error().message);
        }

        if (auto water_renderable_result = water_surface_.initialize();
            !water_renderable_result)
        {
            return fail("Failed to initialize water renderer for chunk ({}, {}): {}",
                        settings_.chunk_coord.x,
                        settings_.chunk_coord.y,
                        water_renderable_result.error().message);
        }

        return {};
    }

    void TerrainChunk::draw_gl(const sf::View& view) const { renderable_.draw(mesh_, view); }

    void TerrainChunk::draw_water_gl(const sf::View& view) const { water_surface_.draw_gl(view); }

    Result<void> TerrainChunk::dispatch_generation()
    {
        if (generation_dispatched_ || generation_finalized_) return {};

        if (auto dispatch_result = generator_.dispatch();
            !dispatch_result)
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
        if (!field_samples) return fail(field_samples.error());

        auto terrain_result = read_scored_surface();
        if (!terrain_result) return fail(terrain_result.error());

        auto water_result = rebuild_scored_surface(TerrainGenerator::water_channel_index, 0.0f);
        if (!water_result) return fail(water_result.error());

        build_chunk(*terrain_result, *water_result, *field_samples);
        generation_dispatched_ = false;
        generation_finalized_  = true;
        return {};
    }

    Result<void> TerrainChunk::upload_rebuild_field(const std::span<const FieldSample> field_samples)
    {
        return generator_.upload_field(field_samples);
    }

    void TerrainChunk::refresh_cached_terrain_mesh(const std::span<const FieldSample> field_samples)
    {
        if (cached_terrain_vertices_.empty() || cached_terrain_indices_.empty()) return;
        build_terrain_mesh(cached_terrain_vertices_, cached_terrain_indices_, field_samples);
    }

    Result<void> TerrainChunk::dispatch_terrain_surface_rebuild()
    {
        return generator_.dispatch_surface_rebuild(TerrainGenerator::terrain_channel_index, 0.0f);
    }

    Result<void> TerrainChunk::finalize_terrain_surface_rebuild(const std::span<const FieldSample> field_samples)
    {
        auto terrain_result = read_scored_surface();
        if (!terrain_result) return fail(terrain_result.error());

        cache_terrain_surface(*terrain_result, field_samples);
        collider_.build(terrain_result->collider_loops, terrain_result->collider_paths);
        return {};
    }

    Result<void> TerrainChunk::dispatch_water_surface_rebuild()
    {
        return water_surface_.dispatch_rebuild(generator_);
    }

    Result<void> TerrainChunk::finalize_water_surface_rebuild()
    {
        return water_surface_.finalize_rebuild(generator_, settings_);
    }

    Result<std::vector<TerrainChunk::FieldSample>> TerrainChunk::readback_field() const
    {
        return generator_.read_field();
    }

    ivec2 TerrainChunk::chunk_coord() const { return settings_.chunk_coord; }
    vec2  TerrainChunk::display_min() const { return display_min_; }
    vec2  TerrainChunk::display_max() const { return display_max_; }

    Result<TerrainContour::ScoredResult> TerrainChunk::read_scored_surface()
    {
        auto readback_result = generator_.readback();
        if (!readback_result) return fail(readback_result.error());
        return TerrainContour::score_and_filter(std::move(*readback_result), settings_);
    }

    Result<TerrainContour::ScoredResult> TerrainChunk::rebuild_scored_surface(
        const std::uint32_t channel_index, const float iso)
    {
        TRY(generator_.dispatch_surface_rebuild(channel_index, iso));
        return read_scored_surface();
    }

    void TerrainChunk::build_chunk(
        const TerrainContour::ScoredResult& terrain_result,
        const TerrainContour::ScoredResult& water_result,
        const std::span<const FieldSample>  field_samples)
    {
        cache_terrain_surface(terrain_result, field_samples);
        water_surface_.rebuild_mesh(water_result.mesh_vertices, water_result.mesh_indices);
        collider_.build(terrain_result.collider_loops, terrain_result.collider_paths);
    }

    void TerrainChunk::cache_terrain_surface(
        const TerrainContour::ScoredResult& terrain_result,
        const std::span<const FieldSample>  field_samples)
    {
        cached_terrain_vertices_ = terrain_result.mesh_vertices;
        cached_terrain_indices_  = terrain_result.mesh_indices;
        build_terrain_mesh(cached_terrain_vertices_, cached_terrain_indices_, field_samples);
    }

    void TerrainChunk::build_terrain_mesh(
        const std::vector<vec2>&           vertices,
        const std::vector<std::uint32_t>&  indices,
        const std::span<const FieldSample> field_samples)
    {
        std::vector<sf::Vertex> mesh_vertices;
        mesh_vertices.reserve(vertices.size());

        const auto radius = std::max(settings_.planet_radius, 0.001f);

        for (const auto& point : vertices)
        {
            const auto dist_from_center = distance(point, settings_.world_center);
            const auto gradient         = clamp01(dist_from_center / radius);

            const auto wetness =
                    std::clamp(sample_field_channel(field_samples, settings_, point, [](const FieldSample& sample)
                               {
                                   return sample.wetness;
                               }),
                               0.0f,
                               1.0f);

            const auto greenness = std::clamp(
                sample_field_channel(field_samples, settings_, point,
                                     [](const FieldSample& sample) { return sample.greenness; }),
                0.0f,
                1.0f);

            // The fragment shader reads wetness from texcoord.x, depth from texcoord.y, and greenness from color.a.
            auto color = lerp_color(0x3F2C1C_rgb, 0xD6B27B_rgb, gradient);
            color.a    = static_cast<std::uint8_t>(std::lround(greenness * 255.0f));

            mesh_vertices.emplace_back(point, color, vec2{ wetness, gradient });
        }

        mesh_.set_data(mesh_vertices, indices);
    }

}
