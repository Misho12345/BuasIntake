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

		struct WaterPreviewSample final
		{
			vec2 world_position{ 0.0f, 0.0f };
			float fill{ 0.0f };
		};

		struct WaterPreviewMesh final
		{
			std::vector<vec2> current_vertices{};
			std::vector<std::uint32_t> current_indices{};
			std::vector<vec2> future_vertices{};
			std::vector<std::uint32_t> future_indices{};
		};

		static constexpr float seed_plantable_wetness_threshold = 0.35f;

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
		[[nodiscard]] std::uint32_t apply_ground_brush(const TerrainEdit& edit,
			std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max());
		[[nodiscard]] std::uint32_t place_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		[[nodiscard]] std::uint32_t pickup_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		[[nodiscard]] std::optional<WaterPreviewMesh> build_water_preview_mesh(vec2 world_position,
			std::uint32_t volume_cap) const;
		[[nodiscard]] std::vector<WaterPreviewSample> preview_water_placement(vec2 world_position,
			std::uint32_t volume_cap) const;
		[[nodiscard]] std::optional<fs::path> save_chunk_field_image(vec2 world_position) const;
		void update_active_colliders(vec2 world_position);

		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;
		[[nodiscard]] vec2 chunk_size() const;
		[[nodiscard]] vec2 terrain_cell_size() const;
		[[nodiscard]] vec2 planet_center() const;
		[[nodiscard]] vec2 spawn_point_from_top_center(float height_offset) const;
		[[nodiscard]] float wetness_at(vec2 world_position) const;
		[[nodiscard]] bool is_seed_plantable(vec2 world_position) const;

	private:
		struct TerrainEditResult final
		{
			bool changed{ false };
			std::uint32_t units{ 0u };
		};

		struct WaterPlanSample final
		{
			ivec2 coord{ 0, 0 };
			float water{ 0.0f };
		};

		struct WaterPlan final
		{
			std::vector<ivec2> dried_component{};
			std::vector<WaterPlanSample> affected_samples{};
			std::uint32_t wet_sample_count{ 0u };
		};

		[[nodiscard]] static uvec2 padded_field_size(const ChunkSettings& settings);
		[[nodiscard]] static ivec2 chunk_sample_stride(const ChunkSettings& settings);
		[[nodiscard]] static vec2 cell_size(const ChunkSettings& settings);
		[[nodiscard]] static float compute_planet_radius();
		[[nodiscard]] static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);
		[[nodiscard]] ivec2 chunk_index_from_world(vec2 world_position) const;
		[[nodiscard]] bool is_valid_global_sample(ivec2 coord) const;
		[[nodiscard]] std::size_t global_field_index(ivec2 coord) const;
		[[nodiscard]] vec2 global_sample_world_position(ivec2 coord) const;
		[[nodiscard]] float normalized_depth(vec2 world_position) const;
		[[nodiscard]] ivec2 world_to_global_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<FieldSample> extract_chunk_field(ivec2 chunk_coord) const;
		void initialize_global_field();
		void rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks);
		void mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;
		[[nodiscard]] int solid_neighbor_count(ivec2 coord) const;
		[[nodiscard]] bool has_water_neighbor(ivec2 coord) const;
		[[nodiscard]] bool has_protective_water_neighbor(ivec2 coord) const;
		[[nodiscard]] bool is_dig_protected(ivec2 coord) const;
		[[nodiscard]] std::optional<ivec2> find_water_anchor(vec2 world_position) const;
		[[nodiscard]] std::optional<ivec2> find_water_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<ivec2> collect_water_component(ivec2 start_coord) const;
		[[nodiscard]] std::uint32_t water_volume_at_anchor(ivec2 anchor, ivec2* plan_start = nullptr) const;
		[[nodiscard]] TerrainEditResult apply_terrain_edit_to_global_field(const TerrainEdit& edit, std::vector<bool>& dirty_chunks,
			std::vector<ivec2>& changed_coords, std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max());
		[[nodiscard]] std::optional<WaterPlan> build_water_plan(ivec2 start_coord, std::uint32_t desired_wet_sample_count) const;
		[[nodiscard]] bool apply_water_plan(const WaterPlan& plan, std::vector<bool>& dirty_chunks,
			std::vector<ivec2>& changed_coords);
		void recompute_wetness_around(const std::vector<ivec2>& changed_coords, std::vector<bool>& dirty_chunks);
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

		static constexpr float hard_rock_depth_threshold = 0.60f;
	};
}
