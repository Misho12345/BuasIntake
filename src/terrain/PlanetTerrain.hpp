#pragma once

#include "pch.hpp"


#include "TerrainChunkGrid.hpp"
#include "TerrainField.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/TerrainBrushController.hpp"
#include "terrain/TerrainGenerationCoordinator.hpp"
#include "terrain/TerrainGenerationFinalizer.hpp"
#include "terrain/TerrainMoistureSystem.hpp"
#include "terrain/TerrainSurfaceAttachmentSystem.hpp"
#include "terrain/TerrainSurfaceSampler.hpp"
#include "vegetation/Plant.hpp"
#include "water/TerrainWaterSystem.hpp"

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
        using TerrainEdit       = TerrainGenerator::TerrainEdit;
        using FieldSample       = TerrainFieldSample;
        using WaterPlan         = water::WaterPlan;
        using ResourceInventory = resources::ResourceInventory;
        using ResourceNode      = resources::ResourceNode;
        using Plant             = vegetation::Plant;
        using PlantStage        = vegetation::PlantStage;
        using PlantFamily       = vegetation::PlantFamily;

        static constexpr std::size_t hud_counter_count = resources::hud_counter_count;

        PlanetTerrain(
            b2WorldId                     world_id,
            resources::ResourceSystem&    resources,
            vegetation::VegetationSystem& vegetation);

        ~PlanetTerrain();

        PlanetTerrain(const PlanetTerrain&)                = delete;
        PlanetTerrain& operator=(const PlanetTerrain&)     = delete;
        PlanetTerrain(PlanetTerrain&&) noexcept            = default;
        PlanetTerrain& operator=(PlanetTerrain&&) noexcept = default;

        // initialize builds every chunk first then stitches the whole thing into one global field that the gameplay systems use
        Result<void> initialize();
        void         draw_gl(const sf::View& view) const;
        void         draw_water_gl(const sf::View& view) const;

        // brush edits are deferred a couple frames on purpose so sculpting does not stall on a full rebuild every click
        void flush_pending_edits();
        void rebuild_after_vegetation_change();

        // this is the main terrain edit entry point used by the sculpt tool and it updates revisions right away but delays the expensive chunk rebuild
        std::uint32_t apply_ground_brush(
            const TerrainEdit&                       edit,
            std::uint32_t                            unit_budget = std::numeric_limits<std::uint32_t>::max(),
            const std::optional<GroundBrushBlocker>& blocker     = std::nullopt);

        Result<void> try_harvest_resource(vec2 world_position);
        Result<void> plant_seed(vec2 world_position);

        vec2          chunk_size() const;
        vec2          terrain_cell_size() const;
        vec2          planet_center() const;
        std::uint32_t seed() const { return base_chunk_settings_.seed; }
        std::uint64_t field_revision() const { return field_revision_; }
        std::uint64_t water_revision() const { return water_revision_; }

        // the win condition reads this as the percent of surface shell samples that are both exposed and green enough
        float green_surface_coverage() const;
        vec2 spawn_point_from_top_center(float height_offset) const;
        bool is_valid_global_sample(ivec2 coord) const;
        const FieldSample& global_sample(ivec2 coord) const { return field_.sample(coord); }
        uvec2 global_field_size() const { return field_.size(); }
        vec2 global_sample_world_position(ivec2 coord) const;
        float normalized_depth(vec2 world_position) const;
        ivec2 world_to_global_sample(vec2 world_position) const;
        bool is_sample_exposed_to_air(ivec2 coord) const;
        std::optional<vec2> surface_anchor_world(ivec2 coord) const;
        bool is_surface_suitable_for_plant(ivec2 coord) const;
        bool has_resource_at(ivec2 coord) const;
        bool contains_water_volume(vec2 world_position) const;

        // water tool calls this to turn a click plus desired volume into a settled plan without changing the field yet
        std::optional<WaterPlan> build_targeted_water_plan(
            vec2           world_position,
            std::uint32_t  volume_cap,
            bool           pickup) const;

        // this is the commit side of water edits and it owns the recompute and rebuild order afterwards
        Result<bool>  apply_water_plan_and_rebuild(const WaterPlan& plan);
        std::uint32_t total_water_sample_count() const;
        void          validate() const;

    private:
        using TerrainEditResult = TerrainBrushController::EditResult;
        using SurfaceAttachment = TerrainSurfaceAttachment;

        static float               compute_planet_radius();
        std::size_t                global_field_index(ivec2 coord) const;
        TerrainGenerationFieldView make_generation_field_view();

        std::optional<SurfaceAttachment> exposed_surface_attachment(ivec2 coord) const;

        void         refresh_surface_attachments_around(const std::vector<ivec2>& changed_coords);
        void         generate_caves_resources_and_plants();
        Result<void> initialize_global_field();

        // this is the big rebuild funnel used after generation sculpting water and plant changes
        Result<void> rebuild_dirty_chunks(
            const std::vector<bool>& dirty_chunks,
            bool                     smooth_water             = false,
            bool                     rebuild_water            = true,
            bool                     rebuild_terrain_geometry = true,
            bool                     refresh_terrain_visuals  = true);

        void                 mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;
        int                  solid_neighbor_count(ivec2 coord) const;
        bool                 has_water_neighbor(ivec2 coord) const;
        bool                 has_protective_water_neighbor(ivec2 coord) const;
        bool                 is_dig_protected(ivec2 coord) const;
        std::vector<ivec2>   collect_water_component(ivec2 start_coord, bool include_diagonals = false) const;

        TerrainEditResult apply_terrain_edit_to_global_field(
            const TerrainEdit&  edit,
            std::vector<bool>&  dirty_chunks,
            std::vector<ivec2>& changed_coords,
            std::uint32_t       unit_budget = std::numeric_limits<
                std::uint32_t>::max(),
            const std::optional<GroundBrushBlocker>& blocker =
                    std::nullopt);

        bool apply_water_plan(
            const WaterPlan&    plan,
            std::vector<bool>&  dirty_chunks,
            std::vector<ivec2>& changed_coords);

        void recompute_ground_greenness(std::vector<bool>& dirty_chunks);

        void recompute_wetness_around(
            const std::vector<ivec2>& changed_coords,
            std::vector<bool>&        dirty_chunks,
            bool                      recompute_greenness = true);

        static constexpr ivec2 chunk_count() { return { 10, 10 }; }

        b2WorldId        world_id_{ b2_nullWorldId };
        TerrainChunkGrid chunk_grid_{};

        ChunkSettings base_chunk_settings_{};
        TerrainField                  field_{};
        vegetation::VegetationSystem* vegetation_{ nullptr };
        resources::ResourceSystem*    resources_{ nullptr };
        water::TerrainWaterSystem     water_system_{};
        TerrainBrushController        brush_controller_{};
        TerrainSurfaceAttachmentSystem surface_attachments_{};
        TerrainGenerationCoordinator  generation_coordinator_{};
        TerrainMoistureSystem         moisture_system_{};

        std::uint64_t field_revision_{ 0u };
        std::uint64_t water_revision_{ 0u };
    };
}
