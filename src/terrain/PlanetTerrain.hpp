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

		struct OreRenderResources;
		struct VegetationRenderResources final
		{
			GLuint vao{ 0 };
			GLuint quad_vbo{ 0 };
			GLuint instance_vbo{ 0 };
			GLuint texture_array{ 0 };
			GLsizei instance_count{ 0 };
			std::uint32_t tile_size_pixels{ 32u };
			gfx::Shader shader{};

			~VegetationRenderResources()
			{
				if (texture_array != 0) glDeleteTextures(1, &texture_array);
				if (instance_vbo != 0) glDeleteBuffers(1, &instance_vbo);
				if (quad_vbo != 0) glDeleteBuffers(1, &quad_vbo);
				if (vao != 0) glDeleteVertexArrays(1, &vao);
			}
		};

		struct WaterPreviewMesh final
		{
			std::vector<vec2> current_vertices{};
			std::vector<std::uint32_t> current_indices{};
			std::vector<vec2> future_vertices{};
			std::vector<std::uint32_t> future_indices{};
		};

		struct ResourceInventory final
		{
			std::uint32_t rocks{ 0u };
			std::uint32_t ingots{ 0u };
			std::uint32_t diamonds{ 0u };
			std::uint32_t seeds{ 0u };
		};

		static constexpr float seed_plantable_wetness_threshold = 0.35f;
		static constexpr std::size_t dead_plant_family_count = 10u;

		enum class PlantFamily : std::uint8_t
		{
			Grass,
			Flowers,
			Bush,
			Tree
		};

		explicit PlanetTerrain(b2WorldId world_id);
		~PlanetTerrain();

		PlanetTerrain(const PlanetTerrain&) = delete;
		PlanetTerrain& operator=(const PlanetTerrain&) = delete;
		PlanetTerrain(PlanetTerrain&&) noexcept = default;
		PlanetTerrain& operator=(PlanetTerrain&&) noexcept = default;

		[[nodiscard]] Result<void> initialize();
		void draw_gl(const sf::View& view) const;
		void draw_water_gl(const sf::View& view) const;
		void draw_resource_ui(sf::RenderTarget& target) const;
		void update(float dt);
		void update_active_water_colliders(vec2 player_position);
		[[nodiscard]] std::uint32_t apply_ground_brush(const TerrainEdit& edit,
			std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
			const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
		[[nodiscard]] Result<void> try_harvest_resource(vec2 world_position);
		[[nodiscard]] Result<void> plant_seed(vec2 world_position);
		void grant_seeds(std::uint32_t amount);
		[[nodiscard]] Result<std::uint32_t> place_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		[[nodiscard]] Result<std::uint32_t> pickup_water(vec2 world_position, std::uint32_t volume_cap = 25u);
		[[nodiscard]] Result<std::optional<WaterPreviewMesh>> build_water_preview_mesh(vec2 world_position,
			std::uint32_t volume_cap) const;
		[[nodiscard]] Result<fs::path> save_chunk_field_image(vec2 world_position) const;

		[[nodiscard]] vec2 chunk_size() const;
		[[nodiscard]] vec2 terrain_cell_size() const;
		[[nodiscard]] vec2 planet_center() const;
		[[nodiscard]] vec2 spawn_point_from_top_center(float height_offset) const;
		[[nodiscard]] bool contains_water_volume(vec2 world_position) const;
		[[nodiscard]] const ResourceInventory& inventory() const noexcept { return inventory_; }

	private:
		enum class ResourceKind : std::uint8_t
		{
			Rock,
			IronOre,
			BronzeOre,
			GoldOre,
			DiamondOre,
			DeadPlant
		};

		enum class PlantStage : std::uint8_t
		{
			Empty,
			Seeded,
			Sprout,
			Mature
		};

		struct TerrainEditResult final
		{
			bool changed{ false };
			std::uint32_t units{ 0u };
			std::uint32_t candidates{ 0u };
			std::uint32_t cleared_samples{ 0u };
			bool requires_wetness_rebuild{ false };
		};

		struct ResourceNode final
		{
			ResourceKind kind{ ResourceKind::Rock };
			ivec2 coord{ 0, 0 };
			std::uint8_t variant{ 0u };
			bool cave_variant{ false };
			bool surface_attached{ false };
			vec2 anchor_world{ 0.0f, 0.0f };
			vec2 surface_up{ 0.0f, 0.0f };
		};

		struct SurfaceAttachment final
		{
			vec2 anchor_world{ 0.0f, 0.0f };
			vec2 surface_up{ 0.0f, 0.0f };
			float floor_alignment{ 0.0f };
		};

		struct PlantSample final
		{
			PlantStage stage{ PlantStage::Empty };
			PlantFamily family{ PlantFamily::Grass };
			float age{ 0.0f };
			float spread_age{ 0.0f };
			std::uint8_t variant{ 0u };
			vec2 anchor_world{ 0.0f, 0.0f };
		};

		struct PlantVisualSpec final
		{
			const sf::Texture* texture{ nullptr };
			int tile_size{ 32 };
			std::uint8_t column{ 0u };
			float height{ 0.22f };
			float angle_offset{ std::numbers::pi_v<float> };
			float radial_offset{ -0.12f };
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

		struct TerrainEditCandidate final
		{
			ivec2 coord{ 0, 0 };
			float falloff{ 0.0f };
			float distance_to_center{ 0.0f };
		};

		struct SampleBounds final
		{
			ivec2 min{ 0, 0 };
			ivec2 max{ -1, -1 };
		};

		struct WetnessComponent final
		{
			std::vector<ivec2> water_cells{};
			SampleBounds water_bounds{};
			float max_distance{ 0.0f };
			int radius_cells{ 0 };
		};

		struct WaterPreviewPatch final
		{
			ChunkSettings settings{};
			ivec2 min_coord{ 0, 0 };
			ivec2 max_coord{ 0, 0 };
			uvec2 size{ 0u, 0u };
			std::vector<FieldSample> current_samples{};
			std::vector<FieldSample> future_samples{};
		};

		struct WaterBlobCollider final
		{
			explicit WaterBlobCollider(const b2WorldId world_id) : collider{ world_id } {}

			TerrainCollider collider;
			vec2 bounds_min{ 0.0f, 0.0f };
			vec2 bounds_max{ 0.0f, 0.0f };
		};

		[[nodiscard]] static float compute_planet_radius();
		[[nodiscard]] static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);
		[[nodiscard]] ivec2 chunk_index_from_world(vec2 world_position) const;
		[[nodiscard]] bool is_valid_global_sample(ivec2 coord) const;
		[[nodiscard]] std::size_t global_field_index(ivec2 coord) const;
		[[nodiscard]] vec2 global_sample_world_position(ivec2 coord) const;
		[[nodiscard]] float normalized_depth(vec2 world_position) const;
		[[nodiscard]] ivec2 world_to_global_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<FieldSample> extract_chunk_field(ivec2 chunk_coord) const;
		[[nodiscard]] Result<void> load_overlay_assets();
		void draw_resource_ores_gl(const sf::View& view) const;
		void draw_vegetation_gl(const sf::View& view) const;
		[[nodiscard]] Result<void> initialize_ore_render_resources() const;
		[[nodiscard]] Result<void> initialize_vegetation_render_resources() const;
		[[nodiscard]] Result<void> rebuild_ore_instances() const;
		[[nodiscard]] PlantVisualSpec plant_visual_spec(const PlantSample& plant) const;
		void flush_pending_ground_brush_changes();
		void advance_plants(float dt);
		void smooth_cave_terrain(std::vector<ivec2>& changed_coords);
		[[nodiscard]] Result<void> finalize_generated_field(std::vector<bool>& dirty_chunks, const std::vector<ivec2>& changed_coords);
		void compact_active_plants();
		[[nodiscard]] bool is_surface_exposed_world(vec2 world_position, float clearance_distance) const;
		[[nodiscard]] std::optional<SurfaceAttachment> exposed_surface_attachment(ivec2 coord) const;
		[[nodiscard]] std::optional<vec2> surface_anchor_world(ivec2 coord) const;
		[[nodiscard]] bool is_surface_suitable_for_plant(ivec2 coord) const;
		[[nodiscard]] bool has_resource_at(ivec2 coord) const;
		[[nodiscard]] std::uint32_t nearby_cover_count(ivec2 coord, float radius_samples, bool woody_cover) const;
		[[nodiscard]] std::uint32_t mature_tree_count() const;
		[[nodiscard]] bool can_place_tree_near(ivec2 coord, int min_spacing_samples) const;
		[[nodiscard]] bool can_place_woody_near(ivec2 coord, int min_spacing_samples) const;
		[[nodiscard]] PlantFamily choose_plant_family(ivec2 coord) const;
		[[nodiscard]] std::uint8_t choose_plant_variant(ivec2 coord, PlantFamily family) const;
		[[nodiscard]] std::optional<ivec2> find_plantable_seed_coord(vec2 world_position) const;
		[[nodiscard]] bool spread_plants();
		void generate_caves_resources_and_plants();
		void generate_resource_nodes();
		[[nodiscard]] Result<void> initialize_global_field();
		void rebuild_water_blob_colliders();
		[[nodiscard]] Result<void> rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks, bool smooth_water = false);
		void mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;
		[[nodiscard]] int solid_neighbor_count(ivec2 coord) const;
		[[nodiscard]] bool has_water_neighbor(ivec2 coord) const;
		[[nodiscard]] bool has_protective_water_neighbor(ivec2 coord) const;
		[[nodiscard]] bool is_dig_protected(ivec2 coord) const;
		[[nodiscard]] std::optional<ivec2> find_water_anchor(vec2 world_position) const;
		[[nodiscard]] std::optional<ivec2> find_water_sample(vec2 world_position) const;
		[[nodiscard]] std::vector<ivec2> collect_water_component(ivec2 start_coord, bool include_diagonals = false) const;
		[[nodiscard]] std::uint32_t water_volume_at_anchor(ivec2 anchor, ivec2* plan_start = nullptr) const;
		[[nodiscard]] std::optional<WaterPlan> build_targeted_water_plan(vec2 world_position, std::uint32_t volume_cap,
			bool pickup, std::uint32_t* existing_volume = nullptr) const;
		[[nodiscard]] Result<void> apply_water_plan_and_rebuild(const WaterPlan& plan);
		void collect_terrain_edit_candidates(vec2 edit_center, float radius, float signed_strength,
			float falloff_exponent, bool digging, bool hard_dig, std::vector<TerrainEditCandidate>& candidates,
			bool& requires_wetness_rebuild, const std::optional<GroundBrushBlocker>& blocker = std::nullopt) const;
		void apply_terrain_edit_candidates(const std::vector<TerrainEditCandidate>& candidates, float signed_strength,
			std::uint32_t unit_budget, bool hard_dig, std::vector<bool>& dirty_chunks, std::vector<ivec2>& changed_coords,
			TerrainEditResult& result);
		void remove_resource_nodes(const std::unordered_set<std::uint64_t>& cleared_keys);
		[[nodiscard]] TerrainEditResult apply_terrain_edit_to_global_field(const TerrainEdit& edit, std::vector<bool>& dirty_chunks,
			std::vector<ivec2>& changed_coords, std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
			const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
		[[nodiscard]] std::optional<WaterPlan> build_water_plan(ivec2 start_coord, std::uint32_t desired_wet_sample_count,
			bool preserve_existing_water) const;
		[[nodiscard]] bool apply_water_plan(const WaterPlan& plan, std::vector<bool>& dirty_chunks,
			std::vector<ivec2>& changed_coords);
		[[nodiscard]] SampleBounds clamp_sample_bounds(ivec2 min_coord, ivec2 max_coord) const;
		[[nodiscard]] SampleBounds expand_sample_bounds(const SampleBounds& bounds, int radius_cells) const;
		[[nodiscard]] static SampleBounds merge_sample_bounds(const SampleBounds& lhs, const SampleBounds& rhs);
		[[nodiscard]] static bool sample_bounds_intersect(const SampleBounds& lhs, const SampleBounds& rhs);
		[[nodiscard]] std::vector<WetnessComponent> collect_wetness_components(
			const SampleBounds& discovery_bounds,
			float min_cell_extent,
			int max_wetness_radius_cells) const;
		void recompute_ground_greenness(std::vector<bool>& dirty_chunks);
		void apply_wetness_component(const WetnessComponent& component, const SampleBounds& affected_bounds,
			std::vector<float>& best_wetness) const;
		void write_back_wetness(const SampleBounds& affected_bounds, const std::vector<float>& best_wetness,
			std::vector<bool>& dirty_chunks);
		[[nodiscard]] std::optional<WaterPreviewPatch> build_water_preview_patch(const WaterPlan& plan) const;
		void apply_water_preview_plan(const WaterPlan& plan, WaterPreviewPatch& patch) const;
		[[nodiscard]] Result<WaterPreviewMesh> render_water_preview_patch(const WaterPreviewPatch& patch) const;
		[[nodiscard]] static std::size_t patch_index(int x, int y, std::uint32_t width);
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
		std::vector<PlantSample> plant_samples_{};
		std::vector<std::size_t> active_plant_indices_{};
		std::vector<ResourceNode> resource_nodes_{};
		std::vector<WaterBlobCollider> water_blob_colliders_{};
		ResourceInventory inventory_{};
		std::vector<ivec2> pending_ground_brush_changed_coords_{};
		std::vector<bool> pending_ground_brush_dirty_chunks_{};
		bool pending_ground_brush_requires_wetness_rebuild_{ false };

		sf::Texture rock_node_texture_{};
		sf::Texture iron_ore_texture_{};
		sf::Texture bronze_ore_texture_{};
		sf::Texture gold_ore_texture_{};
		sf::Texture diamond_ore_texture_{};
		sf::Texture processed_resource_texture_{};
		sf::Texture live_plant_texture_{};
		sf::Texture grass_plant_texture_{};
		sf::Texture flowers_plant_texture_{};
		sf::Texture bushes_plant_texture_{};
		sf::Texture trees_plant_texture_{};
		std::array<sf::Texture, dead_plant_family_count> dead_plant_textures_{};
		sf::Font ui_font_{};
		mutable std::unique_ptr<OreRenderResources> ore_render_resources_{};
		bool overlay_assets_ready_{ false };
		bool active_plants_dirty_{ false };
		mutable std::array<std::unique_ptr<VegetationRenderResources>, 4> vegetation_render_resources_{};

	};
}
