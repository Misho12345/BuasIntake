#include "pch.hpp"

#include "terrain/TerrainField.hpp"

namespace game::terrain
{
    void TerrainField::reset(const uvec2 size, const vec2 origin, const vec2 cell_size)
    {
        size_      = size;
        origin_    = origin;
        cell_size_ = cell_size;
        samples_.assign(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y), {});
    }

    bool TerrainField::empty() const
    {
        return samples_.empty() || size_.x == 0u || size_.y == 0u;
    }

    bool TerrainField::is_valid_sample(const ivec2 coord) const
    {
        return coord.x >= 0 && coord.y >= 0 && coord.x < static_cast<int>(size_.x) && coord.y < static_cast<int>(size_.y);
    }

    std::size_t TerrainField::sample_index(const ivec2 coord) const
    {
        return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(size_.x) + static_cast<std::size_t>(coord.x);
    }

    std::size_t TerrainField::sample_count() const
    {
        return samples_.size();
    }

    TerrainField::FieldSample& TerrainField::sample(const ivec2 coord)
    {
        return samples_[sample_index(coord)];
    }

    const TerrainField::FieldSample& TerrainField::sample(const ivec2 coord) const
    {
        return samples_[sample_index(coord)];
    }

    vec2 TerrainField::sample_world_position(const ivec2 coord) const
    {
        if (empty()) return origin_;

        return origin_ + cell_size_ * coord;
    }

    ivec2 TerrainField::world_to_sample(const vec2 world_position) const
    {
        if (empty()) return { 0, 0 };

        const vec2 grid = (world_position - origin_) / cell_size_;

        return {
            std::clamp(static_cast<int>(std::lround(grid.x)), 0, static_cast<int>(size_.x) - 1),
            std::clamp(static_cast<int>(std::lround(grid.y)), 0, static_cast<int>(size_.y) - 1)
        };
    }

    std::span<TerrainField::FieldSample> TerrainField::sample_span()
    {
	    return samples_;
    }

    std::span<const TerrainField::FieldSample> TerrainField::sample_span() const
    {
	    return samples_;
    }

    std::vector<TerrainField::FieldSample>& TerrainField::samples()
    {
	    return samples_;
    }

    const std::vector<TerrainField::FieldSample>& TerrainField::samples() const
    {
	    return samples_;
    }

    vec2 TerrainField::origin() const
    {
	    return origin_;
    }

    vec2 TerrainField::cell_size() const
    {
	    return cell_size_;
    }

    uvec2 TerrainField::size() const
    {
	    return size_;
    }
}
