#pragma once

#include "pch.hpp"


#include "TerrainChunk.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/TerrainBrushSystem.hpp"
#include "terrain/TerrainColliderManager.hpp"
#include "terrain/TerrainGenerationFinalizer.hpp"
#include "terrain/TerrainSurfaceSampler.hpp"
#include "vegetation/Plant.hpp"
#include "water/WaterInteraction.hpp"

namespace game::resources
{
    class ResourceSystem;
}

namespace game::vegetation
{
    class VegetationSystem;
}

namespace game::terrain
{
    class PlanetTerrain final
    {
      public:
        using TerrainEdit = TerrainGenerator::TerrainEdit;
        using FieldSample = TerrainGenerator::FieldSample;
        using WaterPlanSample = water::WaterPlanSample;
        using WaterPlan = water::WaterPlan;
        using ResourceInventory = resources::ResourceInventory;
        using HudKind = resources::HudKind;
        using HudCounter = resources::HudCounter;
        using HudState = resources::HudState;
        using ResourceKind = resources::ResourceKind;
        using ResourceNode = resources::ResourceNode;
        using Plant = vegetation::Plant;
        using PlantSample = vegetation::Plant;
        using PlantStage = vegetation::PlantStage;
        using PlantFamily = vegetation::PlantFamily;

        static constexpr std::size_t hud_counter_count = resources::hud_counter_count;

        PlanetTerrain(b2WorldId world_id, resources::ResourceSystem& resources, vegetation::VegetationSystem& vegetation);
        ~PlanetTerrain();

        PlanetTerrain(const PlanetTerrain&) = delete;
        PlanetTerrain& operator=(const PlanetTerrain&) = delete;
        PlanetTerrain(PlanetTerrain&&) noexcept = default;
        PlanetTerrain& operator=(PlanetTerrain&&) noexcept = default;

        Result<void> initialize();
        void draw_gl(const sf::View& view) const;
        void draw_water_gl(const sf::View& view) const;
        void flush_pending_edits();
        void rebuild_after_vegetation_change();
        void update_active_water_colliders(vec2 player_position);
        std::uint32_t apply_ground_brush(const TerrainEdit& edit,
                                         std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
                                         const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
        Result<void> try_harvest_resource(vec2 world_position);
        Result<void> plant_seed(vec2 world_position);

        vec2 chunk_size() const;
        vec2 terrain_cell_size() const;
        vec2 planet_center() const;
        std::uint32_t seed() const noexcept
        {
            return base_chunk_settings_.seed;
        }
        std::uint64_t field_revision() const noexcept
        {
            return field_revision_;
        }
        std::uint64_t water_revision() const noexcept
        {
            return water_revision_;
        }
        std::uint64_t geometry_revision() const noexcept
        {
            return geometry_revision_;
        }
        float green_surface_coverage() const;
        vec2 spawn_point_from_top_center(float height_offset) const;
        bool is_valid_global_sample(ivec2 coord) const;
        const FieldSample& global_sample(ivec2 coord) const
        {
            return global_field_[global_field_index(coord)];
        }
        uvec2 global_field_size() const noexcept
        {
            return global_field_size_;
        }
        vec2 global_sample_world_position(ivec2 coord) const;
        float normalized_depth(vec2 world_position) const;
        ivec2 world_to_global_sample(vec2 world_position) const;
        bool is_sample_exposed_to_air(ivec2 coord) const;
        std::optional<vec2> surface_anchor_world(ivec2 coord) const;
        bool is_surface_suitable_for_plant(ivec2 coord) const;
        bool has_resource_at(ivec2 coord) const;
        const vegetation::VegetationSystem& vegetation_system() const noexcept
        {
            return *vegetation_;
        }
        bool contains_water_volume(vec2 world_position) const;
        resources::ResourceSystem& resource_system() noexcept
        {
            return *resources_;
        }
        const resources::ResourceSystem& resource_system() const noexcept
        {
            return *resources_;
        }
        std::optional<WaterPlan> build_targeted_water_plan(vec2 world_position,
                                                           std::uint32_t volume_cap,
                                                           bool pickup,
                                                           std::uint32_t* existing_volume = nullptr) const;
        Result<bool> apply_water_plan_and_rebuild(const WaterPlan& plan);
        std::uint32_t total_water_sample_count() const;
        void validate() const;

      private:
        using TerrainEditResult = TerrainBrushSystem::Result;
        using SurfaceAttachment = TerrainSurfaceAttachment;

        static float compute_planet_radius();
        static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);
        ivec2 chunk_index_from_world(vec2 world_position) const;
        std::size_t global_field_index(ivec2 coord) const;
        TerrainSurfaceFieldView make_surface_field_view() const;
        TerrainGenerationFieldView make_generation_field_view();
        std::vector<FieldSample> extract_chunk_field(ivec2 chunk_coord) const;
        void flush_pending_ground_brush_changes();
        void flush_deferred_ground_brush_wetness();
        bool is_surface_exposed_world(vec2 world_position, float clearance_distance) const;
        std::optional<SurfaceAttachment> exposed_surface_attachment(ivec2 coord) const;
        void refresh_surface_attachments_around(const std::vector<ivec2>& changed_coords);
        void generate_caves_resources_and_plants();
        Result<void> initialize_global_field();
        void sync_chunk_field_to_global(ivec2 chunk_coord,
                                        std::span<const FieldSample> field_samples,
                                        std::unordered_set<std::uint64_t>* cleared_keys = nullptr);
        water::GridView make_water_grid_view() const;
        std::vector<ivec2> dirty_chunk_extent_markers(const std::vector<bool>& dirty_chunks) const;
        void rebuild_water_blob_colliders();
        Result<void> rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks,
                                          bool smooth_water = false,
                                          bool rebuild_water = true,
                                          bool rebuild_terrain_geometry = true,
                                          bool refresh_terrain_visuals = true);
        void mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;
        int solid_neighbor_count(ivec2 coord) const;
        bool has_water_neighbor(ivec2 coord) const;
        bool has_protective_water_neighbor(ivec2 coord) const;
        bool is_dig_protected(ivec2 coord) const;
        ivec2 settle_water_anchor(ivec2 anchor) const;
        std::optional<ivec2> find_water_anchor(vec2 world_position) const;
        std::optional<ivec2> find_water_sample(vec2 world_position) const;
        std::vector<ivec2> collect_water_component(ivec2 start_coord, bool include_diagonals = false) const;
        std::uint32_t water_volume_at_anchor(ivec2 anchor, ivec2* plan_start = nullptr) const;
        TerrainEditResult apply_terrain_edit_to_global_field(const TerrainEdit& edit,
                                                             std::vector<bool>& dirty_chunks,
                                                             std::vector<ivec2>& changed_coords,
                                                             std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
                                                             const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
        std::optional<WaterPlan> build_water_plan(ivec2 start_coord,
                                                  std::uint32_t desired_wet_sample_count,
                                                  bool preserve_existing_water) const;
        bool apply_water_plan(const WaterPlan& plan, std::vector<bool>& dirty_chunks, std::vector<ivec2>& changed_coords);
        void recompute_ground_greenness(std::vector<bool>& dirty_chunks);
        void recompute_wetness_around(const std::vector<ivec2>& changed_coords,
                                      std::vector<bool>& dirty_chunks,
                                      bool recompute_greenness = true);
        static constexpr ivec2 chunk_count()
        {
            return {10, 10};
        }

        b2WorldId world_id_{b2_nullWorldId};
        std::vector<TerrainChunk> chunks_{};

        ChunkSettings base_chunk_settings_{};
        vec2 grid_min_{};
        vec2 grid_max_{};
        vec2 display_min_{};
        vec2 display_max_{};
        vec2 terrain_cell_size_{0.0f, 0.0f};
        vec2 global_field_origin_{0.0f, 0.0f};
        uvec2 global_field_size_{0, 0};
        std::vector<FieldSample> global_field_{};
        vegetation::VegetationSystem* vegetation_{nullptr};
        resources::ResourceSystem* resources_{nullptr};
        TerrainColliderManager water_collider_manager_{};
        std::vector<ivec2> pending_ground_brush_changed_coords_{};
        std::vector<bool> pending_ground_brush_dirty_chunks_{};
        bool pending_ground_brush_changed_water_{false};
        bool pending_ground_brush_requires_wetness_rebuild_{false};
        int pending_ground_brush_rebuild_delay_frames_{0};
        std::vector<ivec2> deferred_ground_brush_wetness_coords_{};
        int deferred_ground_brush_wetness_delay_frames_{0};
        std::uint64_t field_revision_{0u};
        std::uint64_t water_revision_{0u};
        std::uint64_t geometry_revision_{0u};
    };
}
