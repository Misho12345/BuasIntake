#pragma once

#include "pch.hpp"

#include "terrain/TerrainFieldSample.hpp"

namespace game::terrain
{
    class TerrainField final
    {
    public:
        using FieldSample = TerrainFieldSample;

        void reset(uvec2 size, vec2 origin, vec2 cell_size);

        bool        empty() const;
        bool        is_valid_sample(ivec2 coord) const;
        std::size_t sample_index(ivec2 coord) const;
        std::size_t sample_count() const;

        FieldSample&       sample(ivec2 coord);
        const FieldSample& sample(ivec2 coord) const;

        vec2  sample_world_position(ivec2 coord) const;
        ivec2 world_to_sample(vec2 world_position) const;

        std::span<FieldSample>       sample_span();
        std::span<const FieldSample> sample_span() const;

        std::vector<FieldSample>&       samples();
        const std::vector<FieldSample>& samples() const;

        vec2  origin() const;
        vec2  cell_size() const;
        uvec2 size() const;

    private:
        vec2                     origin_{ 0.0f, 0.0f };
        vec2                     cell_size_{ 1.0f, 1.0f };
        uvec2                    size_{ 0u, 0u };
        std::vector<FieldSample> samples_{};
    };
}
