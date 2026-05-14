#include "pch.hpp"

#include "PlanetTerrain.hpp"

#include "resources/ResourceSystem.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGenerationFinalizer.hpp"
#include "terrain/TerrainGridMath.hpp"
#include "terrain/TerrainResourceSpawner.hpp"
#include "vegetation/VegetationSystem.hpp"

namespace game::terrain
{
    namespace
    {
        struct ValidationState final
        {
            bool resource_system_null{ false };
            bool vegetation_system_null{ false };
            bool pending_dirty_chunk_size_mismatch{ false };
            bool global_field_size_mismatch{ false };

            std::optional<std::size_t> non_finite_sample_index{};
            std::optional<std::size_t> water_in_solid_index{};
            std::optional<ivec2>       invalid_resource_node{};
        };

        void log_validation_transition(const bool active, bool& previous, const std::string_view message)
        {
            if (active == previous) return;

            previous = active;
            if (active) Log::error("{}", message);
        }

        float fract01(const float value) { return value - std::floor(value); }

        float terrain_hash(vec2 point, const std::uint32_t seed)
        {
            // cpu copy of the shader hash, used when edits need to clamp against the same generated surface profile
            const float seed_offset = static_cast<float>(seed) * 0.0009765625f;
            point = { fract01(point.x * 0.1031f + seed_offset), fract01(point.y * 0.11369f + seed_offset) };
            const vec2  hash_vector{ point.y + 19.19f + seed_offset * 7.0f, point.x + 19.19f + seed_offset * 7.0f };
            const float hash_offset = point.dot(hash_vector);
            point                   += hash_offset;
            return fract01((point.x + point.y) * (point.x + 13.37f));
        }

        float terrain_noise(const vec2 point, const std::uint32_t seed)
        {
            const vec2 cell{ std::floor(point.x), std::floor(point.y) };
            const vec2 fraction{ fract01(point.x), fract01(point.y) };

            const float a = terrain_hash(cell, seed);
            const float b = terrain_hash(cell + vec2{ 1.0f, 0.0f }, seed);
            const float c = terrain_hash(cell + vec2{ 0.0f, 1.0f }, seed);
            const float d = terrain_hash(cell + vec2{ 1.0f, 1.0f }, seed);

            const vec2 smoothing{
                fraction.x * fraction.x * (3.0f - 2.0f * fraction.x),
                fraction.y * fraction.y * (3.0f - 2.0f * fraction.y)
            };
            return std::lerp(std::lerp(a, b, smoothing.x), std::lerp(c, d, smoothing.x), smoothing.y);
        }

        // this is the broad soft noise band that gives the planet most of its large scale wobble
        float terrain_fbm(vec2 point, const std::uint32_t seed)
        {
            float value     = 0.0f;
            float amplitude = 0.5f;

            for (int i = 0; i < 7; ++i)
            {
                value     += amplitude * terrain_noise(point, seed);
                point     = point * 2.03f + vec2{ 11.7f, -8.3f };
                amplitude *= 0.5f;
            }

            return value;
        }

        // ridged noise adds sharper detail so the shell does not look too smooth
        float terrain_ridged_fbm(vec2 point, const std::uint32_t seed)
        {
            float value     = 0.0f;
            float amplitude = 0.55f;

            for (int i = 0; i < 6; ++i)
            {
                float noise_value = terrain_noise(point, seed);
                noise_value       = 1.0f - std::abs(noise_value * 2.0f - 1.0f);
                value             += noise_value * amplitude;
                point             = point * 2.18f + vec2{ -6.4f, 9.1f };
                amplitude         *= 0.55f;
            }

            return value;
        }

        // this mixes several noise bands into one radius target so the shell reads as one shape with big medium and tiny detail all at once
        float generated_surface_radius(const vec2 dir, const ChunkSettings& settings)
        {
            const vec2  seed_offset = vec2{ 0.0137f, 0.0211f } * static_cast<float>(settings.seed);
            const float macro       = terrain_fbm(dir * 1.85f + (seed_offset + vec2{ 3.1f, -7.4f }), settings.seed);
            const float medium      = terrain_fbm(dir * 6.20f + vec2{ -seed_offset.y - 11.2f, -seed_offset.x + 4.6f }, settings.seed);
            const float ridges = terrain_ridged_fbm(dir * 11.50f + (seed_offset * 1.3f + vec2{ 8.4f, -5.6f }), settings.seed);
            const float micro = terrain_fbm(dir * 23.0f + (seed_offset * -0.75f + vec2{ -4.2f, 12.8f }), settings.seed);

            return settings.planet_radius + (macro - 0.5f) * settings.planet_radius * 0.19f +
                    (medium - 0.5f) * settings.planet_radius * 0.07f + (ridges - 0.45f) * settings.planet_radius *
                    0.045f +
                    (micro - 0.5f) * settings.planet_radius * 0.02f;
        }

        float clamp_terrain_density(const float density, const vec2 world_position, const ChunkSettings& settings)
        {
            // edited terrain still respects the original planet shell bounds, so digging cannot leave huge noisy spikes behind
            const vec2  offset           = world_position - settings.world_center;
            const float dist_from_center = offset.length();
            const vec2  dir              = dist_from_center > 1e-5f ? offset / dist_from_center : vec2{ 0.0f, 1.0f };
            const float base_density     = generated_surface_radius(dir, settings) - dist_from_center;
            return std::clamp(density, -1.0f, std::max(1.0f, base_density));
        }

        bool is_exposed_to_air(const PlanetTerrain::FieldSample& sample, const int solid_neighbors)
        {
            return is_solid_sample(sample) && solid_neighbors >= 2 && solid_neighbors < 8;
        }
    }

    PlanetTerrain::~PlanetTerrain() = default;

    PlanetTerrain::PlanetTerrain(const b2WorldId               world_id, resources::ResourceSystem& resources,
                                  vegetation::VegetationSystem& vegetation)
        : world_id_{ world_id },
          vegetation_{ &vegetation },
          resources_{ &resources } {}

    Result<void> PlanetTerrain::initialize()
    {
        base_chunk_settings_.chunk_grid_size = chunk_count();
        base_chunk_settings_.planet_radius   = compute_planet_radius();

        TRY(chunk_grid_.initialize(world_id_, base_chunk_settings_));

        return initialize_global_field();
    }

    void PlanetTerrain::draw_gl(const sf::View& view) const
    {
        chunk_grid_.draw_gl(view);
    }

    void PlanetTerrain::draw_water_gl(const sf::View& view) const
    {
        chunk_grid_.draw_water_gl(view);
    }

    void PlanetTerrain::flush_pending_edits()
    {
        // the brush controller owns batching, while PlanetTerrain supplies the callbacks that know how to update dependent systems
        brush_controller_.flush_pending_edits({
            .rebuild_pending = [this](const std::vector<bool>& dirty_chunks, const bool changed_water)
            {
                return rebuild_dirty_chunks(dirty_chunks, changed_water);
            },
            .make_dirty_chunks = [this]
            {
                return chunk_grid_.make_dirty_chunk_flags(false);
            },
            .recompute_wetness = [this](const std::vector<ivec2>& changed_coords, std::vector<bool>& dirty_chunks)
            {
                recompute_wetness_around(changed_coords, dirty_chunks, true);
            },
            .rebuild_deferred_wetness = [this](const std::vector<bool>& dirty_chunks)
            {
                return rebuild_dirty_chunks(dirty_chunks, false, false, true);
            },
            .refresh_surface_attachments = [this](const std::vector<ivec2>& changed_coords)
            {
                refresh_surface_attachments_around(changed_coords);
            }
        });
    }

    // greenness is terrain side data derived from plants so when plants change we can refresh just that and skip a full geometry rebuild
    void PlanetTerrain::rebuild_after_vegetation_change()
    {
        // Plant growth changes the terrain's visual channels without changing geometry. Refresh every chunk so cached
        // boundary vertices do not keep stale wetness or greenness until the player edits that chunk again.
        auto dirty_chunks = chunk_grid_.make_dirty_chunk_flags(true);
        recompute_ground_greenness(dirty_chunks);
        ++field_revision_;
        if (const auto rebuild_result = rebuild_dirty_chunks(dirty_chunks, false, false);
            !rebuild_result) { Log::error(rebuild_result.error()); }
    }

    std::optional<PlanetTerrain::SurfaceAttachment> PlanetTerrain::exposed_surface_attachment(const ivec2 coord) const
    {
        return surface_attachments_.exposed_surface_attachment(field_, base_chunk_settings_.world_center, coord);
    }

    std::optional<vec2> PlanetTerrain::surface_anchor_world(const ivec2 coord) const
    {
        return surface_attachments_.surface_anchor_world(field_, base_chunk_settings_.world_center, coord);
    }

    bool PlanetTerrain::is_surface_suitable_for_plant(const ivec2 coord) const
    {
        const auto attachment = exposed_surface_attachment(coord);
        if (!attachment.has_value()) return false;
        if (normalized_depth(attachment->anchor_world) > constants::surface_plant_depth_limit) return false;
        return attachment->floor_alignment >= 0.74f;
    }

    bool PlanetTerrain::is_sample_exposed_to_air(const ivec2 coord) const
    {
        if (!is_valid_global_sample(coord)) return false;
        return is_exposed_to_air(field_.sample(coord), solid_neighbor_count(coord));
    }

    bool PlanetTerrain::has_resource_at(const ivec2 coord) const
    {
        return resources_ != nullptr && resources_->has_at(coord);
    }

    // the goal is based on exposed outer shell coverage not raw green sample count everywhere
    // so we only count solid samples near the surface that actually border open space and then test their greenness
    float PlanetTerrain::green_surface_coverage() const
    {
        if (field_.empty()) return 0.0f;

        const auto field_size = field_.size();

        std::uint32_t surface_sample_count = 0u;
        std::uint32_t green_sample_count   = 0u;

        for (int y = 0; y < static_cast<int>(field_size.y); ++y)
        {
            for (int x = 0; x < static_cast<int>(field_size.x); ++x)
            {
                const ivec2 coord{ x, y };
                const auto& sample = field_.sample(coord);
                if (!is_solid_sample(sample)) continue;
                if (normalized_depth(global_sample_world_position(coord)) > constants::surface_plant_depth_limit) continue;

                bool has_open_neighbor = false;
                for (int oy = -1; oy <= 1 && !has_open_neighbor; ++oy)
                {
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        if (ox == 0 && oy == 0) continue;

                        const ivec2 neighbor = coord + ivec2{ ox, oy };
                        if (!is_valid_global_sample(neighbor) || !is_solid_sample(field_.sample(neighbor)))
                        {
                            has_open_neighbor = true;
                            break;
                        }
                    }
                }

                if (!has_open_neighbor) continue;

                ++surface_sample_count;
                if (sample.greenness >= constants::green_surface_sample_threshold) ++green_sample_count;
            }
        }

        if (surface_sample_count == 0u) return 0.0f;

        return static_cast<float>(green_sample_count) / static_cast<float>(surface_sample_count);
    }

    Result<void> PlanetTerrain::try_harvest_resource(const vec2 world_position)
    {
        if (resources_ == nullptr) return fail("Terrain resource system is not initialized");
        if (const auto harvest_result = resources_->harvest_at(world_position);
            !harvest_result) { return fail(harvest_result.error()); }

        return {};
    }

    Result<void> PlanetTerrain::plant_seed(const vec2 world_position)
    {
        if (vegetation_ == nullptr || resources_ == nullptr) return fail("Terrain subsystems are not initialized");
        if (const auto plant_result = vegetation_->plant_seed(*this, *resources_, world_position);
            !plant_result)
            return fail(plant_result.error());

        auto dirty_chunks = chunk_grid_.make_dirty_chunk_flags(false);
        recompute_ground_greenness(dirty_chunks);
        ++field_revision_;
        if (auto res = rebuild_dirty_chunks(dirty_chunks, false, false);
            !res)
            return fail(res.error());

        return {};
    }

    // sculpting marks revisions immediately so the rest of the game can react, while the costly rebuild is delayed a frame or two
    // this keeps the tool responsive without rebuilding synchronously on every edit
    std::uint32_t PlanetTerrain::apply_ground_brush(
        const TerrainEdit&                       edit,
        const std::uint32_t                      unit_budget,
        const std::optional<GroundBrushBlocker>& blocker)
    {
        const auto outcome = brush_controller_.apply_ground_brush(
            field_,
            chunk_grid_.chunk_count(),
            edit,
            unit_budget,
            blocker,
            [this](const TerrainEdit&                       callback_edit,
                   std::vector<bool>&                       dirty_chunks,
                   std::vector<ivec2>&                      changed_coords,
                   const std::uint32_t                      callback_unit_budget,
                   const std::optional<GroundBrushBlocker>& callback_blocker)
            {
                return apply_terrain_edit_to_global_field(
                    callback_edit,
                    dirty_chunks,
                    changed_coords,
                    callback_unit_budget,
                    callback_blocker);
            });

        if (!outcome.changed) return 0u;

        // Only mark revisions here; the actual chunk rebuild is deferred to update() so sculpting stays responsive.
        ++field_revision_;
        if (outcome.water_changed) ++water_revision_;

        return outcome.units;
    }

    // once chunk generation is stitched into one global field this pass builds all the gameplay side data from that shared view
    // caves resources plants wetness and greenness all want the same final field so doing them from one source keeps them from drifting apart
    void PlanetTerrain::generate_caves_resources_and_plants()
    {
        const auto callbacks = TerrainGenerationCallbacks{
            .global_field_index = [this](const ivec2 coord) { return global_field_index(coord); },
            .solid_neighbor_count = [this](const ivec2 coord) { return solid_neighbor_count(coord); },
            .global_sample_world_position = [this](const ivec2 coord) { return global_sample_world_position(coord); },
            .normalized_depth = [this](const vec2 world_position) { return normalized_depth(world_position); },
            .exposed_surface_attachment = [this](const ivec2 coord) { return exposed_surface_attachment(coord); },
            .has_water_neighbor = [this](const ivec2 coord) { return has_water_neighbor(coord); },
            .is_valid_global_sample = [this](const ivec2 coord) { return is_valid_global_sample(coord); },
            .dry_water_density = [](const FieldSample& sample) { return dry_water_density(sample); },
            .add_resource_node =
            [this](const resources::ResourceNode& node)
            {
                if (resources_ == nullptr) return;
                auto stored_node = node;
                if (!stored_node.surface_attached)
                {
                    stored_node.anchor_world = global_sample_world_position(stored_node.coord);
                }
                resources_->add_node(stored_node);
            }
        };

        generation_coordinator_.finalize_generated_field(
            field_,
            chunk_grid_,
            resources_,
            vegetation_,
            make_generation_field_view(),
            callbacks,
            [this](const std::vector<ivec2>& changed_coords, std::vector<bool>& dirty_chunks)
            {
                recompute_wetness_around(changed_coords, dirty_chunks);
            },
            [this](const std::vector<bool>& dirty_chunks)
            {
                return rebuild_dirty_chunks(dirty_chunks);
            });
    }

    Result<void> PlanetTerrain::initialize_global_field()
    {
        chunk_grid_.reset_field_layout(field_);
        TRY(chunk_grid_.sync_generated_field_to_global(field_));

        generate_caves_resources_and_plants();
        return {};
    }

    void PlanetTerrain::refresh_surface_attachments_around(const std::vector<ivec2>& changed_coords)
    {
        // resources and plants store anchors so they can render on surfaces, but terrain edits can move or delete those surfaces
        surface_attachments_.refresh_around(
            field_,
            base_chunk_settings_.world_center,
            changed_coords,
            resources_,
            [this](const std::unordered_set<std::uint64_t>& affected_keys)
            {
                if (vegetation_ != nullptr) vegetation_->refresh_surface_anchors(*this, affected_keys);
            });
    }

    bool PlanetTerrain::is_valid_global_sample(const ivec2 coord) const
    {
        return field_.is_valid_sample(coord);
    }

    std::size_t PlanetTerrain::global_field_index(const ivec2 coord) const
    {
        return field_.sample_index(coord);
    }

    vec2 PlanetTerrain::global_sample_world_position(const ivec2 coord) const
    {
        return field_.sample_world_position(coord);
    }

    float PlanetTerrain::normalized_depth(const vec2 world_position) const
    {
        const float surface_radius = std::max(base_chunk_settings_.planet_radius, 1e-4f);
        const float radius         = distance(world_position, base_chunk_settings_.world_center);
        return std::clamp(1.0f - radius / surface_radius, 0.0f, 1.0f);
    }

    ivec2 PlanetTerrain::world_to_global_sample(const vec2 world_position) const
    {
        return field_.world_to_sample(world_position);
    }

    Result<void> PlanetTerrain::rebuild_dirty_chunks(
        const std::vector<bool>& dirty_chunks,
        const bool               rebuild_water,
        const bool               rebuild_terrain_geometry,
        const bool               refresh_terrain_visuals)
    {
        return chunk_grid_.rebuild_dirty_chunks(
            field_, dirty_chunks, rebuild_water, rebuild_terrain_geometry, refresh_terrain_visuals);
    }

    void PlanetTerrain::mark_chunks_covering_global_sample(const ivec2 coord, std::vector<bool>& dirty_chunks) const
    {
        chunk_grid_.mark_chunks_covering_global_sample(coord, dirty_chunks);
    }

    int PlanetTerrain::solid_neighbor_count(const ivec2 coord) const
    {
        int count = 0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;

                const ivec2 neighbor = coord + ivec2{ x, y };
                if (!is_valid_global_sample(neighbor)) continue;
                if (is_solid_sample(field_.sample(neighbor))) ++count;
            }
        }

        return count;
    }

    bool PlanetTerrain::has_water_neighbor(const ivec2 coord) const
    {
        return water_system_.has_water_neighbor(field_, coord);
    }

    bool PlanetTerrain::has_protective_water_neighbor(const ivec2 coord) const
    {
        return water_system_.has_protective_water_neighbor(field_, base_chunk_settings_.world_center, coord);
    }

    bool PlanetTerrain::is_dig_protected(const ivec2 coord) const
    {
        if (!is_valid_global_sample(coord)) return false;
        const auto& sample           = field_.sample(coord);
        const float protected_radius = base_chunk_settings_.planet_radius * constants::undiggable_core_radius_fraction;
        return has_water_sample(sample) ||
		        has_protective_water_neighbor(coord) ||
		        distance(global_sample_world_position(coord), base_chunk_settings_.world_center) <= protected_radius;
    }

    TerrainGenerationFieldView PlanetTerrain::make_generation_field_view()
    {
        return {
            .global_field      = field_.sample_span(),
            .global_field_size = field_.size(),
            .seed              = base_chunk_settings_.seed
        };
    }

    std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_targeted_water_plan(const vec2 world_position,
        const std::uint32_t                                                                     volume_cap,
        const bool                                                                              pickup) const
    {
        return water_system_.build_targeted_water_plan(field_, base_chunk_settings_.world_center, world_position,
                                                       volume_cap, pickup);
    }

    Result<bool> PlanetTerrain::apply_water_plan_and_rebuild(const WaterPlan& plan)
    {
        auto               dirty_chunks = chunk_grid_.make_dirty_chunk_flags(false);
        std::vector<ivec2> changed_coords;
        const bool         changed = apply_water_plan(plan, dirty_chunks, changed_coords);
        if (!changed) return false;

        // Water edits still go through the same rebuild path so mesh and wetness stay together.
        ++field_revision_;
        ++water_revision_;
        recompute_wetness_around(changed_coords, dirty_chunks, false, true);
        TRY(rebuild_dirty_chunks(dirty_chunks, true, false, true));
        return true;
    }

    std::uint32_t PlanetTerrain::total_water_sample_count() const
    {
        return water_system_.total_water_sample_count(field_);
    }

    PlanetTerrain::TerrainEditResult PlanetTerrain::apply_terrain_edit_to_global_field(
        const TerrainEdit&                       edit,
        std::vector<bool>&                       dirty_chunks,
        std::vector<ivec2>&                      changed_coords,
        const std::uint32_t                      unit_budget,
        const std::optional<GroundBrushBlocker>& blocker)
    {
        std::unordered_set<std::uint64_t> cleared_keys;
        // The brush system does the per-sample math; this wrapper supplies the terrain-specific rules.
        auto result = TerrainBrushSystem::apply_edit(
            field_.sample_span(),
            field_.size(),
            field_.origin(),
            field_.cell_size(),
            chunk_grid_.grid_min(),
            chunk_grid_.grid_max(),
            base_chunk_settings_,
            edit,
            dirty_chunks,
            changed_coords,
            unit_budget,
            blocker,
            [this](const ivec2 coord) { return is_dig_protected(coord); },
            [this](const ivec2 coord) { return has_water_neighbor(coord); },
            [this](const float density, const vec2 world, const ChunkSettings& settings)
            {
                return clamp_terrain_density(density, world, settings);
            },
            [](const FieldSample& sample) { return dry_water_density(sample); },
            [this](const ivec2 coord) { return global_sample_world_position(coord); },
            [this](const ivec2 coord, std::vector<bool>& dirty_flags)
            {
                mark_chunks_covering_global_sample(coord, dirty_flags);
            },
            [this, &cleared_keys](const ivec2 coord, const std::size_t sample_index)
            {
                if (vegetation_ != nullptr) vegetation_->clear_plant_at(sample_index);
                cleared_keys.insert(sample_key(coord));
            });

        if (resources_ != nullptr && !cleared_keys.empty())
        {
            static_cast<void>(resources_->remove_nodes(cleared_keys));
        }
        return result;
    }

    bool PlanetTerrain::apply_water_plan(
        const WaterPlan&    plan,
        std::vector<bool>&  dirty_chunks,
        std::vector<ivec2>& changed_coords)
    {
        return water_system_.apply_water_plan(
            field_,
            plan,
            [this, &dirty_chunks](const ivec2 coord) { mark_chunks_covering_global_sample(coord, dirty_chunks); },
            changed_coords);
    }

    void PlanetTerrain::recompute_wetness_around(
        const std::vector<ivec2>& changed_coords,
        std::vector<bool>&        dirty_chunks,
        const bool                recompute_greenness,
        const bool                mark_affected_visuals_dirty)
    {
        moisture_system_.recompute_wetness_around(
            field_,
            changed_coords,
            [this, &dirty_chunks](const ivec2 coord) { mark_chunks_covering_global_sample(coord, dirty_chunks); },
            vegetation_,
            recompute_greenness,
            mark_affected_visuals_dirty);
        ++field_revision_;
    }

    void PlanetTerrain::recompute_ground_greenness(std::vector<bool>& dirty_chunks)
    {
        moisture_system_.recompute_ground_greenness(
            field_,
            vegetation_,
            [this, &dirty_chunks](const ivec2 coord) { mark_chunks_covering_global_sample(coord, dirty_chunks); });
    }

    void PlanetTerrain::validate() const
    {
        #ifndef NDEBUG
        static ValidationState previous_state;
        ValidationState        current_state{};

        current_state.resource_system_null              = resources_ == nullptr;
        current_state.vegetation_system_null            = vegetation_ == nullptr;
        current_state.pending_dirty_chunk_size_mismatch =
                brush_controller_.has_pending_dirty_chunk_size_mismatch(chunk_grid_.chunk_count());
        current_state.global_field_size_mismatch = false;

        if (!field_.empty() &&
            field_.sample_count() != static_cast<std::size_t>(field_.size().x) * static_cast<std::size_t>(
                field_.size().y)) { current_state.global_field_size_mismatch = true; }

        for (std::size_t i = 0; i < field_.sample_count(); ++i)
        {
            const auto& sample = field_.samples()[i];
            if (!std::isfinite(sample.terrain) || !std::isfinite(sample.water) || !std::isfinite(sample.wetness) ||
                !std::isfinite(sample.greenness))
            {
                current_state.non_finite_sample_index = i;
                break;
            }

            if (sample.water > 1e-4f && sample.terrain >= 0.0f)
            {
                current_state.water_in_solid_index = i;
                break;
            }
        }

        if (resources_ != nullptr)
        {
            for (const auto& node : resources_->nodes())
            {
                if (is_valid_global_sample(node.coord)) continue;
                current_state.invalid_resource_node = node.coord;
                break;
            }
        }

        log_validation_transition(
	        current_state.resource_system_null,
	        previous_state.resource_system_null,
	        "PlanetTerrain validation failed: resource system is null");

        log_validation_transition(
	        current_state.vegetation_system_null,
	        previous_state.vegetation_system_null,
	        "PlanetTerrain validation failed: vegetation system is null");

        if (current_state.pending_dirty_chunk_size_mismatch != previous_state.pending_dirty_chunk_size_mismatch)
        {
            previous_state.pending_dirty_chunk_size_mismatch = current_state.pending_dirty_chunk_size_mismatch;
            if (current_state.pending_dirty_chunk_size_mismatch)
            {
                Log::error(
                    "PlanetTerrain validation failed: pending dirty chunk array size {} does not match chunk count {}",
                    brush_controller_.pending_dirty_chunk_count(),
                    chunk_grid_.chunk_count());
            }
        }

        if (current_state.global_field_size_mismatch != previous_state.global_field_size_mismatch)
        {
            previous_state.global_field_size_mismatch = current_state.global_field_size_mismatch;
            if (current_state.global_field_size_mismatch)
            {
                Log::error("PlanetTerrain validation failed: global field size {} does not match dimensions {}x{}",
                           field_.sample_count(),
                           field_.size().x,
                           field_.size().y);
            }
        }

        if (current_state.non_finite_sample_index != previous_state.non_finite_sample_index)
        {
            previous_state.non_finite_sample_index = current_state.non_finite_sample_index;
            if (current_state.non_finite_sample_index.has_value())
            {
                Log::error("PlanetTerrain validation failed: non-finite field sample at index {}",
                           *current_state.non_finite_sample_index);
            }
        }

        if (current_state.water_in_solid_index != previous_state.water_in_solid_index)
        {
            previous_state.water_in_solid_index = current_state.water_in_solid_index;
            if (current_state.water_in_solid_index.has_value())
            {
                Log::error("PlanetTerrain validation failed: water exists in solid terrain at field index {}",
                           *current_state.water_in_solid_index);
            }
        }

        if (current_state.invalid_resource_node != previous_state.invalid_resource_node)
        {
            previous_state.invalid_resource_node = current_state.invalid_resource_node;
            if (current_state.invalid_resource_node.has_value())
            {
                Log::error("PlanetTerrain validation failed: resource node at invalid sample ({}, {})",
                           current_state.invalid_resource_node->x,
                           current_state.invalid_resource_node->y);
            }
        }
        #endif
    }

    vec2 PlanetTerrain::chunk_size() const { return base_chunk_settings_.chunk_size; }
    vec2 PlanetTerrain::terrain_cell_size() const { return field_.cell_size(); }
    vec2 PlanetTerrain::planet_center() const { return base_chunk_settings_.world_center; }

    bool PlanetTerrain::contains_water_volume(const vec2 world_position) const
    {
        return water_system_.contains_water_volume(field_, world_position);
    }

    vec2 PlanetTerrain::spawn_point_from_top_center(const float height_offset) const
    {
        const auto terrain_world_center = base_chunk_settings_.world_center;
        const vec2 ray_origin{ terrain_world_center.x, chunk_grid_.display_max().y + base_chunk_settings_.chunk_size.y * 2.0f };
        const vec2 ray_end{ terrain_world_center.x, chunk_grid_.display_min().y - base_chunk_settings_.chunk_size.y * 2.0f };
        const b2QueryFilter filter = b2DefaultQueryFilter();

        const auto ray_result = b2World_CastRayClosest(
	        world_id_,
	        to_b2(ray_origin),
	        to_b2(ray_end - ray_origin),
	        filter);

        if (ray_result.hit)
        {
            const vec2 hit_point = from_b2(ray_result.point);
            const vec2 radial_up = normalize(hit_point - terrain_world_center);
            return hit_point + radial_up * height_offset;
        }

        return { terrain_world_center.x, chunk_grid_.display_max().y + height_offset };
    }

    float PlanetTerrain::compute_planet_radius()
    {
	    const auto          total_chunk_count = chunk_count();
	    const ChunkSettings defaults{};
	    const auto          terrain_chunk_size = defaults.chunk_size;

	    const vec2 total_world_size = terrain_chunk_size * total_chunk_count;

	    const auto min_half_extent = min(total_world_size) * 0.5f;
        const auto edge_padding    = min(terrain_chunk_size) * 0.75f;
        return std::max(min_half_extent - edge_padding, 1.0f);
    }

}
