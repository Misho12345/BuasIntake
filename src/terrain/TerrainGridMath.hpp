#pragma once

#include "pch.hpp"

#include "ChunkSettings.hpp"

namespace game::terrain
{
	[[nodiscard]]
	inline vec2 chunk_min(const ChunkSettings& settings)
	{
		return {
			settings.world_center.x +
				(static_cast<float>(settings.chunk_coord.x) - 0.5f * static_cast<float>(settings.chunk_grid_size.x)) * settings.chunk_size.x,
			settings.world_center.y +
				(static_cast<float>(settings.chunk_coord.y) - 0.5f * static_cast<float>(settings.chunk_grid_size.y)) * settings.chunk_size.y
		};
	}

	[[nodiscard]]
	inline vec2 chunk_max(const ChunkSettings& settings)
	{
		const auto min = chunk_min(settings);
		return { min.x + settings.chunk_size.x, min.y + settings.chunk_size.y };
	}

	[[nodiscard]]
	inline vec2 cell_size(const ChunkSettings& settings)
	{
		return {
			settings.chunk_size.x / static_cast<float>(std::max(settings.field_size.x - 1u, 1u)),
			settings.chunk_size.y / static_cast<float>(std::max(settings.field_size.y - 1u, 1u))
		};
	}

	[[nodiscard]]
	inline uvec2 padded_field_size(const ChunkSettings& settings)
	{
		return {
			settings.field_size.x + settings.field_padding.x * 2u,
			settings.field_size.y + settings.field_padding.y * 2u
		};
	}

	[[nodiscard]]
	inline ivec2 chunk_sample_stride(const ChunkSettings& settings)
	{
		return {
			static_cast<std::int32_t>(std::max(settings.field_size.x, 1u) - 1u),
			static_cast<std::int32_t>(std::max(settings.field_size.y, 1u) - 1u)
		};
	}

	[[nodiscard]]
	inline ivec2 field_padding(const ChunkSettings& settings)
	{
		return {
			static_cast<std::int32_t>(settings.field_padding.x),
			static_cast<std::int32_t>(settings.field_padding.y)
		};
	}

	[[nodiscard]]
	inline vec2 field_origin(const ChunkSettings& settings)
	{
		const auto terrain_cell_size = cell_size(settings);
		const auto min = chunk_min(settings);
		return {
			min.x - terrain_cell_size.x * static_cast<float>(settings.field_padding.x),
			min.y - terrain_cell_size.y * static_cast<float>(settings.field_padding.y)
		};
	}
}
