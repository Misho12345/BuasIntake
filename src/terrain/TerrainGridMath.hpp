#pragma once

#include "pch.hpp"


#include "ChunkSettings.hpp"

namespace game::terrain
{
    inline vec2 chunk_min(const ChunkSettings& settings)
    {
        return settings.world_center + (settings.chunk_coord - 0.5f * settings.chunk_grid_size) * settings.chunk_size;
    }

    inline vec2 chunk_max(const ChunkSettings& settings)
    {
        const auto min = chunk_min(settings);
        return min + settings.chunk_size;
    }

    inline vec2 cell_size(const ChunkSettings& settings)
    {
        return {
            settings.chunk_size.x / std::max(settings.field_size.x - 1.0f, 1.0f),
            settings.chunk_size.y / std::max(settings.field_size.y - 1.0f, 1.0f)
        };
    }

    inline uvec2 padded_field_size(const ChunkSettings& settings)
    {
        return settings.field_size + settings.field_padding * 2u;
    }

    inline ivec2 chunk_sample_stride(const ChunkSettings& settings)
    {
        return {
            static_cast<std::int32_t>(std::max(settings.field_size.x, 1u) - 1u),
            static_cast<std::int32_t>(std::max(settings.field_size.y, 1u) - 1u)
        };
    }

    inline ivec2 field_padding(const ChunkSettings& settings)
    {
        return static_cast<ivec2>(settings.field_padding);
    }

    inline vec2 field_origin(const ChunkSettings& settings)
    {
        const auto terrain_cell_size = cell_size(settings);
        const auto min               = chunk_min(settings);
        return min - terrain_cell_size * settings.field_padding;
    }

    template <typename FieldSamples, typename Accessor>
    float sample_field_channel_bilinear(
        const FieldSamples& samples,
        const uvec2         field_size,
        const vec2          origin,
        const vec2          sample_spacing,
        const vec2          world_position,
        Accessor&&          accessor)
    {
        if (samples.empty() || field_size.x == 0u || field_size.y == 0u) return 0.0f;

        const vec2 grid = (world_position - origin) / sample_spacing;

        const float clamped_x = std::clamp(grid.x, 0.0f, static_cast<float>(field_size.x - 1u));
        const float clamped_y = std::clamp(grid.y, 0.0f, static_cast<float>(field_size.y - 1u));

        const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
        const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
        const auto x1 = std::min(x0 + 1u, field_size.x - 1u);
        const auto y1 = std::min(y0 + 1u, field_size.y - 1u);

        const float tx = clamped_x - static_cast<float>(x0);
        const float ty = clamped_y - static_cast<float>(y0);

        auto sample_at = [&](const std::uint32_t x, const std::uint32_t y)
        {
            return accessor(samples[static_cast<std::size_t>(y) * field_size.x + x]);
        };

        return std::lerp(
            std::lerp(sample_at(x0, y0), sample_at(x1, y0), tx),
            std::lerp(sample_at(x0, y1), sample_at(x1, y1), tx),
            ty);
    }
}
