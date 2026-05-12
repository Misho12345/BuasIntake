#pragma once

#include "pch.hpp"

namespace game::terrain
{
    class TerrainResourceNoise final
    {
    public:
        TerrainResourceNoise() = delete;

        static float hash01(float x, float y, std::uint32_t seed)
        {
            const float value = std::sin(x * 12.9898f + y * 78.233f + static_cast<float>(seed) * 0.013f) * 43758.5453f;
            return fract01(value);
        }

        static float perlin_noise(const vec2 point, const std::uint32_t seed)
        {
            const vec2 cell{ std::floor(point.x), std::floor(point.y) };
            const vec2 local{ point.x - cell.x, point.y - cell.y };

            auto gradient = [seed](const vec2 corner)
            {
                const float angle = terrain_hash(corner, seed) * 2.0f * pi;
                return vec2{ std::cos(angle), std::sin(angle) };
            };

            auto fade = [](const float t) { return t * t * (3.0f - 2.0f * t); };

            const vec2 c00 = cell;
            const vec2 c10{ cell.x + 1.0f, cell.y };
            const vec2 c01{ cell.x, cell.y + 1.0f };
            const vec2 c11{ cell.x + 1.0f, cell.y + 1.0f };

            const float n00 = gradient(c00).dot(local - vec2{ 0.0f, 0.0f });
            const float n10 = gradient(c10).dot(local - vec2{ 1.0f, 0.0f });
            const float n01 = gradient(c01).dot(local - vec2{ 0.0f, 1.0f });
            const float n11 = gradient(c11).dot(local - vec2{ 1.0f, 1.0f });

            const float u   = fade(local.x);
            const float v   = fade(local.y);
            const float nx0 = std::lerp(n00, n10, u);
            const float nx1 = std::lerp(n01, n11, u);
            return std::lerp(nx0, nx1, v);
        }

        static float perlin_fbm(vec2 point, const std::uint32_t seed)
        {
            float value     = 0.0f;
            float amplitude = 0.5f;
            for (int i = 0; i < 5; ++i)
            {
                value     += perlin_noise(point, seed + i * 131u) * amplitude;
                point     = { point.x * 2.04f - 4.8f, point.y * 2.04f + 9.2f };
                amplitude *= 0.5f;
            }

            return value;
        }

        static std::uint8_t choose_variant_row(const ivec2 coord, const std::uint32_t seed, const std::uint32_t seed_offset)
        {
            return static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(hash01(static_cast<float>(coord.x), static_cast<float>(coord.y), seed + seed_offset) * 16.0f),
                0,
                15));
        }

    private:
        static float fract01(const float value)
        {
            return value - std::floor(value);
        }

        static float terrain_hash(vec2 point, const std::uint32_t seed)
        {
            const float seed_offset = static_cast<float>(seed) * 0.0009765625f;
            point = { fract01(point.x * 0.1031f + seed_offset), fract01(point.y * 0.11369f + seed_offset) };
            const float dot_value = point.x * (point.y + 19.19f) + point.y * (point.x + 19.19f);
            point = { point.x + dot_value, point.y + dot_value };
            return fract01((point.x + point.y) * point.x * point.y);
        }
    };
}
