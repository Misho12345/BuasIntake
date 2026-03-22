#pragma once

#include "pch.hpp"

#include "TerrainChunk.hpp"

namespace game::terrain
{
	class PlanetTerrain final
	{
	public:
		explicit PlanetTerrain(b2WorldId world_id);
		~PlanetTerrain() = default;

		PlanetTerrain(const PlanetTerrain&) = delete;
		PlanetTerrain& operator=(const PlanetTerrain&) = delete;
		PlanetTerrain(PlanetTerrain&&) noexcept = default;
		PlanetTerrain& operator=(PlanetTerrain&&) noexcept = default;

		void draw_gl(const sf::View& view) const;
		void render_debug(sf::RenderTarget& target) const;
		void update_active_colliders(vec2 world_position);

		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;
		[[nodiscard]] vec2 chunk_size() const;
		[[nodiscard]] vec2 spawn_point_from_top_center(float height_offset) const;

	private:
		[[nodiscard]] static float compute_planet_radius();
		[[nodiscard]] static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);
		[[nodiscard]] ivec2 chunk_index_from_world(vec2 world_position) const;
		[[nodiscard]] static constexpr ivec2 chunk_count() { return { 10, 10 }; }

		b2WorldId world_id_{ b2_nullWorldId };
		std::vector<TerrainChunk> chunks_{};

		ChunkSettings base_chunk_settings_{};
		vec2 grid_min_{};
		vec2 grid_max_{};
		vec2 display_min_{};
		vec2 display_max_{};

		bool active_chunk_initialized_{ false };
		ivec2 active_chunk_index_{ 0, 0 };
	};
}
