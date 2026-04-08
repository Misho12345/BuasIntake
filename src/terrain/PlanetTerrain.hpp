#pragma once

#include "pch.hpp"

#include "TerrainChunk.hpp"

namespace game::terrain
{
	class PlanetTerrain final
	{
	public:
		using TerrainEdit = TerrainGenerator::TerrainEdit;
		using FieldSample = TerrainGenerator::FieldSample;

		explicit PlanetTerrain(b2WorldId world_id);
		~PlanetTerrain() = default;

		PlanetTerrain(const PlanetTerrain&) = delete;
		PlanetTerrain& operator=(const PlanetTerrain&) = delete;
		PlanetTerrain(PlanetTerrain&&) noexcept = default;
		PlanetTerrain& operator=(PlanetTerrain&&) noexcept = default;

		void draw_gl(const sf::View& view) const;
		void draw_water_gl(const sf::View& view) const;
		void render_debug(sf::RenderTarget& target) const;
		void queue_edit(const TerrainEdit& edit);
		void apply_pending_edits();
		bool place_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		bool pickup_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		[[nodiscard]] std::optional<fs::path> save_chunk_field_image(vec2 world_position) const;
		void update_active_colliders(vec2 world_position);

		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;
		[[nodiscard]] vec2 chunk_size() const;
		[[nodiscard]] vec2 planet_center() const;
		[[nodiscard]] vec2 spawn_point_from_top_center(float height_offset) const;

	private:
		[[nodiscard]] static uvec2 padded_field_size(const ChunkSettings& settings);
		[[nodiscard]] static ivec2 chunk_sample_stride(const ChunkSettings& settings);
		[[nodiscard]] static vec2 cell_size(const ChunkSettings& settings);
		[[nodiscard]] static float compute_planet_radius();
		[[nodiscard]] static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);
		[[nodiscard]] ivec2 chunk_index_from_world(vec2 world_position) const;
		[[nodiscard]] bool is_valid_global_sample(ivec2 coord) const;
		[[nodiscard]] std::size_t global_field_index(ivec2 coord) const;
		[[nodiscard]] vec2 global_sample_world_position(ivec2 coord) const;
		[[nodiscard]] ivec2 world_to_global_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<FieldSample> extract_chunk_field(ivec2 chunk_coord) const;
		void initialize_global_field();
		void rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks);
		void mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;
		[[nodiscard]] int solid_neighbor_count(ivec2 coord) const;
		[[nodiscard]] bool has_water_neighbor(ivec2 coord) const;
		[[nodiscard]] bool is_dig_protected(ivec2 coord) const;
		[[nodiscard]] std::optional<ivec2> find_water_anchor(vec2 world_position) const;
		[[nodiscard]] std::optional<ivec2> find_water_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<ivec2> collect_water_component(ivec2 start_coord) const;
		[[nodiscard]] bool apply_terrain_edit_to_global_field(const TerrainEdit& edit, std::vector<bool>& dirty_chunks);
		[[nodiscard]] bool fill_water_basin(ivec2 start_coord, std::uint32_t volume_cap, std::vector<bool>& dirty_chunks);
		[[nodiscard]] bool remove_water_volume(ivec2 start_coord, std::uint32_t volume_cap, std::vector<bool>& dirty_chunks);
		[[nodiscard]] static constexpr ivec2 chunk_count() { return { 10, 10 }; }

		b2WorldId world_id_{ b2_nullWorldId };
		std::vector<TerrainChunk> chunks_{};

		ChunkSettings base_chunk_settings_{};
		vec2 grid_min_{};
		vec2 grid_max_{};
		vec2 display_min_{};
		vec2 display_max_{};
		vec2 terrain_cell_size_{ 0.0f, 0.0f };
		vec2 global_field_origin_{ 0.0f, 0.0f };
		uvec2 global_field_size_{ 0, 0 };
		std::vector<FieldSample> global_field_{};
		std::vector<TerrainEdit> pending_edits_{};

		bool active_chunk_initialized_{ false };
		vec2 active_collider_center_{ 0.0f, 0.0f };
	};
}
