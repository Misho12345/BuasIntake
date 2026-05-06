#include "pch.hpp"

#include "PlanetTerrain.hpp"

#include "core/ScopedProfiler.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGenerationFinalizer.hpp"
#include "terrain/TerrainGreennessSystem.hpp"
#include "terrain/TerrainGridMath.hpp"
#include "terrain/TerrainResourceSpawner.hpp"
#include "terrain/TerrainSurfaceSampler.hpp"
#include "terrain/TerrainWaterColliderBuilder.hpp"
#include "terrain/TerrainWetnessSystem.hpp"
#include "vegetation/VegetationSystem.hpp"

namespace game::terrain
{
    namespace
    {
        struct ValidationState final
        {
            bool resource_system_null{false};
            bool vegetation_system_null{false};
            bool pending_dirty_chunk_size_mismatch{false};
            bool global_field_size_mismatch{false};
            std::optional<std::size_t> non_finite_sample_index{};
            std::optional<std::size_t> water_in_solid_index{};
            std::optional<ivec2> invalid_resource_node{};
        };

        void log_validation_transition(const bool active, bool& previous, const std::string_view message)
        {
            if (active == previous)
                return;

            previous = active;
            if (active)
                Log::error("{}", message);
        }

        float radial_dist(const vec2& point, const vec2& center)
        {
            const vec2 offset = point - center;
            return std::sqrt(offset.x * offset.x + offset.y * offset.y);
        }

        bool has_water(const PlanetTerrain::FieldSample& sample)
        {
            return water::has_water(sample);
        }

        bool is_solid(const PlanetTerrain::FieldSample& sample)
        {
            return sample.terrain >= 0.0f;
        }

        float dry_water_density(const PlanetTerrain::FieldSample& sample)
        {
            return -std::abs(sample.terrain);
        }

        std::uint8_t normalized_channel(const float value, const float min_value, const float max_value)
        {
            if (std::abs(max_value - min_value) <= 1e-6f)
                return 127u;
            const float normalized = std::clamp((value - min_value) / (max_value - min_value), 0.0f, 1.0f);
            return static_cast<std::uint8_t>(std::lround(normalized * 255.0f));
        }

        std::uint64_t sample_key(const ivec2 coord)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u) | static_cast<std::uint32_t>(coord.y);
        }

        bool aabb_intersects_view_circle(const vec2 min, const vec2 max, const sf::View& view)
        {
            const vec2 center{view.getCenter().x, view.getCenter().y};
            const vec2 size{std::abs(view.getSize().x), std::abs(view.getSize().y)};
            const float radius = std::sqrt(size.x * size.x + size.y * size.y) * 0.5f;
            const float closest_x = std::clamp(center.x, min.x, max.x);
            const float closest_y = std::clamp(center.y, min.y, max.y);
            const float dx = center.x - closest_x;
            const float dy = center.y - closest_y;
            return dx * dx + dy * dy <= radius * radius;
        }

        std::pair<ivec2, ivec2> owned_local_sample_bounds(const ChunkSettings& settings)
        {
            const auto padded_size = padded_field_size(settings);
            ivec2 min_coord{settings.chunk_coord.x == 0 ? 0 : static_cast<int>(settings.field_padding.x),
                            settings.chunk_coord.y == 0 ? 0 : static_cast<int>(settings.field_padding.y)};
            ivec2 max_coord{settings.chunk_coord.x == settings.chunk_grid_size.x - 1
                                ? static_cast<int>(padded_size.x) - 1
                                : static_cast<int>(settings.field_padding.x + settings.field_size.x - 2u),
                            settings.chunk_coord.y == settings.chunk_grid_size.y - 1
                                ? static_cast<int>(padded_size.y) - 1
                                : static_cast<int>(settings.field_padding.y + settings.field_size.y - 2u)};
            return {min_coord, max_coord};
        }

        float fract01(const float value)
        {
            return value - std::floor(value);
        }

        float terrain_hash(vec2 point, const std::uint32_t seed)
        {
            const float seed_offset = static_cast<float>(seed) * 0.0009765625f;
            point = {fract01(point.x * 0.1031f + seed_offset), fract01(point.y * 0.11369f + seed_offset)};
            const vec2 hash_vector{point.y + 19.19f + seed_offset * 7.0f, point.x + 19.19f + seed_offset * 7.0f};
            const float hash_offset = point.dot(hash_vector);
            point += vec2{hash_offset, hash_offset};
            return fract01((point.x + point.y) * (point.x + 13.37f));
        }

        float terrain_noise(const vec2 point, const std::uint32_t seed)
        {
            const vec2 cell{std::floor(point.x), std::floor(point.y)};
            const vec2 fraction{fract01(point.x), fract01(point.y)};

            const float a = terrain_hash(cell, seed);
            const float b = terrain_hash(cell + vec2{1.0f, 0.0f}, seed);
            const float c = terrain_hash(cell + vec2{0.0f, 1.0f}, seed);
            const float d = terrain_hash(cell + vec2{1.0f, 1.0f}, seed);

            const vec2 smoothing{fraction.x * fraction.x * (3.0f - 2.0f * fraction.x),
                                 fraction.y * fraction.y * (3.0f - 2.0f * fraction.y)};
            return std::lerp(std::lerp(a, b, smoothing.x), std::lerp(c, d, smoothing.x), smoothing.y);
        }

        float terrain_fbm(vec2 point, const std::uint32_t seed)
        {
            float value = 0.0f;
            float amplitude = 0.5f;

            for (int i = 0; i < 7; ++i)
            {
                value += amplitude * terrain_noise(point, seed);
                point = point * 2.03f + vec2{11.7f, -8.3f};
                amplitude *= 0.5f;
            }

            return value;
        }

        float terrain_ridged_fbm(vec2 point, const std::uint32_t seed)
        {
            float value = 0.0f;
            float amplitude = 0.55f;

            for (int i = 0; i < 6; ++i)
            {
                float noise_value = terrain_noise(point, seed);
                noise_value = 1.0f - std::abs(noise_value * 2.0f - 1.0f);
                value += noise_value * amplitude;
                point = point * 2.18f + vec2{-6.4f, 9.1f};
                amplitude *= 0.55f;
            }

            return value;
        }

        float generated_surface_radius(const vec2 dir, const ChunkSettings& settings)
        {
            const vec2 seed_offset = vec2{0.0137f, 0.0211f} * static_cast<float>(settings.seed);
            const float macro = terrain_fbm(dir * 1.85f + (seed_offset + vec2{3.1f, -7.4f}), settings.seed);
            const float medium = terrain_fbm(dir * 6.20f + vec2{-seed_offset.y - 11.2f, -seed_offset.x + 4.6f}, settings.seed);
            const float ridges = terrain_ridged_fbm(dir * 11.50f + (seed_offset * 1.3f + vec2{8.4f, -5.6f}), settings.seed);
            const float micro = terrain_fbm(dir * 23.0f + (seed_offset * -0.75f + vec2{-4.2f, 12.8f}), settings.seed);

            return settings.planet_radius + (macro - 0.5f) * settings.planet_radius * 0.19f +
                   (medium - 0.5f) * settings.planet_radius * 0.07f + (ridges - 0.45f) * settings.planet_radius * 0.045f +
                   (micro - 0.5f) * settings.planet_radius * 0.02f;
        }

        float clamp_terrain_density(const float density, const vec2 world_position, const ChunkSettings& settings)
        {
            const vec2 offset = world_position - settings.world_center;
            const float dist_from_center = std::sqrt(offset.x * offset.x + offset.y * offset.y);
            const vec2 dir = dist_from_center > 1e-5f ? offset / dist_from_center : vec2{0.0f, 1.0f};
            const float base_density = generated_surface_radius(dir, settings) - dist_from_center;
            return std::clamp(density, -1.0f, std::max(1.0f, base_density));
        }

        bool is_exposed_to_air(const PlanetTerrain::FieldSample& sample, const int solid_neighbors)
        {
            return is_solid(sample) && solid_neighbors >= 2 && solid_neighbors < 8;
        }

    }

    PlanetTerrain::~PlanetTerrain() = default;

    PlanetTerrain::PlanetTerrain(const b2WorldId world_id, resources::ResourceSystem& resources, vegetation::VegetationSystem& vegetation)
        : world_id_{world_id}, water_collider_manager_{world_id}, vegetation_{&vegetation}, resources_{&resources}
    {
    }

    Result<void> PlanetTerrain::initialize()
    {
        const auto total_chunk_count = chunk_count();
        base_chunk_settings_.chunk_grid_size = total_chunk_count;
        const auto terrain_chunk_size = base_chunk_settings_.chunk_size;
        const auto terrain_world_center = base_chunk_settings_.world_center;
        terrain_cell_size_ = cell_size(base_chunk_settings_);

        const vec2 total_world_size{terrain_chunk_size.x * static_cast<float>(total_chunk_count.x),
                                    terrain_chunk_size.y * static_cast<float>(total_chunk_count.y)};

        grid_min_ = terrain_world_center - total_world_size * 0.5f;
        grid_max_ = grid_min_ + total_world_size;

        display_min_ = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        display_max_ = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};

        const auto planet_radius = compute_planet_radius();
        chunks_.reserve(static_cast<std::size_t>(total_chunk_count.x * total_chunk_count.y));

        for (int y = 0; y < total_chunk_count.y; ++y)
        {
            for (int x = 0; x < total_chunk_count.x; ++x)
            {
                auto chunk_settings = base_chunk_settings_;
                chunk_settings.chunk_coord = {x, y};
                chunk_settings.chunk_grid_size = total_chunk_count;
                chunk_settings.planet_radius = planet_radius;

                auto& chunk = chunks_.emplace_back(world_id_, chunk_settings);
                display_min_.x = std::min(display_min_.x, chunk.display_min().x);
                display_min_.y = std::min(display_min_.y, chunk.display_min().y);
                display_max_.x = std::max(display_max_.x, chunk.display_max().x);
                display_max_.y = std::max(display_max_.y, chunk.display_max().y);
            }
        }

        base_chunk_settings_.planet_radius = planet_radius;

        if (chunks_.empty())
        {
            display_min_ = grid_min_;
            display_max_ = grid_max_;
            return {};
        }

        for (auto& chunk : chunks_)
        {
            auto chunk_initialize_result = chunk.initialize();
            if (!chunk_initialize_result)
            {
                return fail(chunk_initialize_result.error());
            }

            auto dispatch_result = chunk.dispatch_generation();
            if (!dispatch_result)
            {
                return fail(dispatch_result.error());
            }
        }

        // Queue generation for every chunk first, then do the readback/finalize pass after.
        for (auto& chunk : chunks_)
        {
            auto finalize_result = chunk.finalize_generation();
            if (!finalize_result)
            {
                return fail(finalize_result.error());
            }
        }

        return initialize_global_field();
    }

    void PlanetTerrain::draw_gl(const sf::View& view) const
    {
        for (const auto& chunk : chunks_)
        {
            if (!aabb_intersects_view_circle(chunk.display_min(), chunk.display_max(), view))
                continue;
            chunk.draw_gl(view);
        }
    }

    void PlanetTerrain::draw_water_gl(const sf::View& view) const
    {
        for (const auto& chunk : chunks_)
        {
            if (!aabb_intersects_view_circle(chunk.display_min(), chunk.display_max(), view))
                continue;
            chunk.draw_water_gl(view);
        }
    }

    void PlanetTerrain::flush_pending_edits()
    {
        flush_pending_ground_brush_changes();
    }

    void PlanetTerrain::rebuild_after_vegetation_change()
    {
        std::vector<bool> dirty_chunks(chunks_.size(), false);
        recompute_ground_greenness(dirty_chunks);
        ++field_revision_;
        if (const auto rebuild_result = rebuild_dirty_chunks(dirty_chunks, false, false); !rebuild_result)
        {
            Log::error(rebuild_result.error());
        }
    }

    void PlanetTerrain::update_active_water_colliders(const vec2 player_position)
    {
        if (water_collider_manager_.empty())
            return;

        const float activation_padding = std::max(base_chunk_settings_.chunk_size.x, base_chunk_settings_.chunk_size.y) * 1.5f;
        water_collider_manager_.update_active_water_colliders(player_position, activation_padding);
    }

    void PlanetTerrain::flush_pending_ground_brush_changes()
    {
        if (pending_ground_brush_changed_coords_.empty() || pending_ground_brush_dirty_chunks_.empty())
            return;

        if (pending_ground_brush_requires_wetness_rebuild_)
        {
            recompute_wetness_around(pending_ground_brush_changed_coords_, pending_ground_brush_dirty_chunks_);
        }

        if (const auto rebuild_result = rebuild_dirty_chunks(pending_ground_brush_dirty_chunks_, false, pending_ground_brush_changed_water_);
            !rebuild_result)
        {
            Log::error(rebuild_result.error());
        }

        pending_ground_brush_changed_coords_.clear();
        std::fill(pending_ground_brush_dirty_chunks_.begin(), pending_ground_brush_dirty_chunks_.end(), false);
        pending_ground_brush_changed_water_ = false;
        pending_ground_brush_requires_wetness_rebuild_ = false;
    }

    bool PlanetTerrain::is_surface_exposed_world(const vec2 world_position, const float clearance_distance) const
    {
        return terrain_surface_sampler::is_surface_exposed_world(make_surface_field_view(), world_position, clearance_distance);
    }

    std::optional<PlanetTerrain::SurfaceAttachment> PlanetTerrain::exposed_surface_attachment(const ivec2 coord) const
    {
        return terrain_surface_sampler::exposed_surface_attachment(make_surface_field_view(), coord);
    }

    std::optional<vec2> PlanetTerrain::surface_anchor_world(const ivec2 coord) const
    {
        return terrain_surface_sampler::surface_anchor_world(make_surface_field_view(), coord);
    }

    bool PlanetTerrain::is_surface_suitable_for_plant(const ivec2 coord) const
    {
        const auto attachment = exposed_surface_attachment(coord);
        if (!attachment.has_value()) return false;
        if (normalized_depth(attachment->anchor_world) > 0.12f) return false;
        return attachment->floor_alignment >= 0.74f;
    }

    bool PlanetTerrain::is_sample_exposed_to_air(const ivec2 coord) const
    {
        if (!is_valid_global_sample(coord)) return false;
        return is_exposed_to_air(global_field_[global_field_index(coord)], solid_neighbor_count(coord));
    }

    bool PlanetTerrain::has_resource_at(const ivec2 coord) const
    {
        return resources_ != nullptr && resources_->has_at(coord);
    }

    float PlanetTerrain::green_surface_coverage() const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return 0.0f;

        static constexpr float surface_depth_limit = 0.12f;
        static constexpr float green_sample_threshold = 0.25f;

        std::uint32_t surface_sample_count = 0u;
        std::uint32_t green_sample_count = 0u;

        for (int y = 0; y < static_cast<int>(global_field_size_.y); ++y)
        {
            for (int x = 0; x < static_cast<int>(global_field_size_.x); ++x)
            {
                const ivec2 coord{x, y};
                const auto& sample = global_field_[global_field_index(coord)];
                if (!is_solid(sample))
                    continue;
                if (normalized_depth(global_sample_world_position(coord)) > surface_depth_limit)
                    continue;

                bool has_open_neighbor = false;
                for (int oy = -1; oy <= 1 && !has_open_neighbor; ++oy)
                {
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        if (ox == 0 && oy == 0)
                            continue;

                        const ivec2 neighbor{coord.x + ox, coord.y + oy};
                        if (!is_valid_global_sample(neighbor) || !is_solid(global_field_[global_field_index(neighbor)]))
                        {
                            has_open_neighbor = true;
                            break;
                        }
                    }
                }

                if (!has_open_neighbor)
                    continue;

                ++surface_sample_count;
                if (sample.greenness >= green_sample_threshold)
                    ++green_sample_count;
            }
        }

        if (surface_sample_count == 0u)
            return 0.0f;

        return static_cast<float>(green_sample_count) / static_cast<float>(surface_sample_count);
    }

    Result<void> PlanetTerrain::try_harvest_resource(const vec2 world_position)
    {
        if (resources_ == nullptr) return fail("Terrain resource system is not initialized");
        if (const auto harvest_result = resources_->harvest_at(world_position); !harvest_result)
        {
            return fail(harvest_result.error());
        }

        return {};
    }

    Result<void> PlanetTerrain::plant_seed(const vec2 world_position)
    {
        if (vegetation_ == nullptr || resources_ == nullptr) return fail("Terrain subsystems are not initialized");
        if (const auto plant_result = vegetation_->plant_seed(*this, *resources_, world_position); !plant_result)
        {
            return fail(plant_result.error());
        }

        std::vector<bool> dirty_chunks(chunks_.size(), false);
        recompute_ground_greenness(dirty_chunks);
        ++field_revision_;
        ++geometry_revision_;
        if (auto res = rebuild_dirty_chunks(dirty_chunks, false, false); !res)
            return fail(res.error());
        return {};
    }

    std::uint32_t PlanetTerrain::apply_ground_brush(const TerrainEdit& edit,
                                                    const std::uint32_t unit_budget,
                                                    const std::optional<GroundBrushBlocker>& blocker)
    {
        const core::ScopedProfiler profiler{"terrain.apply_ground_brush"};
        static_cast<void>(profiler);

        if (global_field_.empty() || unit_budget == 0u) return 0u;
        if (pending_ground_brush_dirty_chunks_.empty()) pending_ground_brush_dirty_chunks_.assign(chunks_.size(), false);

        const auto result = apply_terrain_edit_to_global_field(
            edit, pending_ground_brush_dirty_chunks_, pending_ground_brush_changed_coords_, unit_budget, blocker);

        if (!result.changed) return 0u;

        // Only mark revisions here; the actual chunk rebuild is deferred to update() so sculpting stays responsive.
        ++field_revision_;
        ++geometry_revision_;
        if (result.water_changed) ++water_revision_;
        pending_ground_brush_changed_water_ = pending_ground_brush_changed_water_ || result.water_changed;
        pending_ground_brush_requires_wetness_rebuild_ = pending_ground_brush_requires_wetness_rebuild_ || result.requires_wetness_rebuild;
        return result.units;
    }

    Result<fs::path> PlanetTerrain::save_chunk_field_image(const vec2 world_position) const
    {
        if (global_field_.empty()) return fail("Cannot export chunk field: global terrain field is empty");

        const auto chunk_coord = chunk_index_from_world(world_position);
        const auto field_samples = extract_chunk_field(chunk_coord);
        const auto padded_size = padded_field_size(base_chunk_settings_);
        if (field_samples.empty() || padded_size.x == 0 || padded_size.y == 0)
        {
            return fail("Cannot export chunk ({}, {}): field data is empty", chunk_coord.x, chunk_coord.y);
        }

        float terrain_min = std::numeric_limits<float>::infinity();
        float terrain_max = -std::numeric_limits<float>::infinity();
        float water_min = std::numeric_limits<float>::infinity();
        float water_max = -std::numeric_limits<float>::infinity();

        for (const auto& sample : field_samples)
        {
            terrain_min = std::min(terrain_min, sample.terrain);
            terrain_max = std::max(terrain_max, sample.terrain);
            water_min = std::min(water_min, sample.water);
            water_max = std::max(water_max, sample.water);
        }

        sf::Image image({padded_size.x, padded_size.y}, sf::Color::Black);
        for (std::uint32_t y = 0; y < padded_size.y; ++y)
        {
            for (std::uint32_t x = 0; x < padded_size.x; ++x)
            {
                const auto& sample = field_samples[static_cast<std::size_t>(y) * padded_size.x + x];
                image.setPixel({x, padded_size.y - 1u - y},
                               {normalized_channel(sample.terrain, terrain_min, terrain_max),
                                normalized_channel(sample.water, water_min, water_max),
                                normalized_channel(sample.wetness, 0.0f, 1.0f),
                                static_cast<std::uint8_t>(sample.water > 0.0f || sample.terrain >= 0.0f ? 255u : 0u)});
            }
        }

        const auto timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const fs::path output_dir = fs::path{"chunk_png_save"};
        std::error_code directory_error;
        fs::create_directories(output_dir, directory_error);
        if (directory_error)
        {
            return fail("Failed to create export directory '{}': {}", output_dir.string(), directory_error.message());
        }

        const auto base_name = std::format("chunk_{}_{}_{}", chunk_coord.x, chunk_coord.y, timestamp);
        const fs::path image_path = output_dir / (base_name + ".png");

        if (!image.saveToFile(image_path.string()))
        {
            return fail("Failed to save chunk field image '{}'", image_path.string());
        }

        return image_path;
    }

    void PlanetTerrain::generate_caves_resources_and_plants()
    {
        if (global_field_.empty()) return;

        // Once every chunk is stitched together, the rest of the terrain logic works from one shared field.
        if (vegetation_ != nullptr) vegetation_->initialize(global_field_.size());
        if (resources_ != nullptr) resources_->clear_nodes();
        if (resources_ != nullptr) resources_->reserve_nodes(16000u);

        std::vector<bool> dirty_chunks(chunks_.size(), true);
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
                if (resources_ == nullptr)
                    return;
                auto stored_node = node;
                if (!stored_node.surface_attached)
                {
                    stored_node.anchor_world = global_sample_world_position(stored_node.coord);
                }
                resources_->add_node(stored_node);
            }};

        auto changed_coords =
            terrain_generation_finalizer::initialize_visual_channels_and_smooth_caves(make_generation_field_view(), callbacks);

        recompute_wetness_around(changed_coords, dirty_chunks);
        terrain_generation_finalizer::generate_resource_nodes(make_generation_field_view(), callbacks);
        if (const auto finalize_result = rebuild_dirty_chunks(dirty_chunks); !finalize_result)
        {
            Log::error(finalize_result.error());
        }
    }

    Result<void> PlanetTerrain::initialize_global_field()
    {
        const auto total_chunk_count = chunk_count();
        const auto padded_size = padded_field_size(base_chunk_settings_);
        const auto stride = chunk_sample_stride(base_chunk_settings_);
        const auto padding = base_chunk_settings_.field_padding;

        global_field_size_ = {static_cast<std::uint32_t>((total_chunk_count.x - 1) * stride.x + static_cast<int>(padded_size.x)),
                              static_cast<std::uint32_t>((total_chunk_count.y - 1) * stride.y + static_cast<int>(padded_size.y))};
        global_field_origin_ = {grid_min_.x - terrain_cell_size_.x * static_cast<float>(padding.x),
                                grid_min_.y - terrain_cell_size_.y * static_cast<float>(padding.y)};

        global_field_.assign(static_cast<std::size_t>(global_field_size_.x) * static_cast<std::size_t>(global_field_size_.y), {});

        // Chunks are generated with overlap for meshing, then copied into one global field for gameplay queries and edits.
        for (const auto& chunk : chunks_)
        {
            auto field = chunk.readback_field();
            if (!field)
            {
                return fail("Failed to read back field for chunk ({}, {}): {}",
                            chunk.chunk_coord().x,
                            chunk.chunk_coord().y,
                            field.error().message);
            }
            if (field->empty()) continue;
            sync_chunk_field_to_global(chunk.chunk_coord(), *field);
        }

        for (auto& sample : global_field_)
        {
            sample.wetness = 0.0f;
            sample.greenness = 0.0f;
        }

        generate_caves_resources_and_plants();
        rebuild_water_blob_colliders();
        return {};
    }

    void PlanetTerrain::sync_chunk_field_to_global(const ivec2 chunk_coord,
                                                   const std::span<const FieldSample> field_samples,
                                                   std::unordered_set<std::uint64_t>* const cleared_keys)
    {
        if (field_samples.empty()) return;

        auto chunk_settings = base_chunk_settings_;
        chunk_settings.chunk_coord = chunk_coord;
        chunk_settings.chunk_grid_size = chunk_count();
        const auto padded_size = padded_field_size(chunk_settings);
        const auto stride = chunk_sample_stride(chunk_settings);
        const auto [owned_min, owned_max] = owned_local_sample_bounds(chunk_settings);
        const ivec2 chunk_base{chunk_coord.x * stride.x, chunk_coord.y * stride.y};

        // Only copy the chunk-owned range back; the padded border belongs to neighbors too.
        for (int y = owned_min.y; y <= owned_max.y; ++y)
        {
            for (int x = owned_min.x; x <= owned_max.x; ++x)
            {
                const ivec2 global_coord{chunk_base.x + x, chunk_base.y + y};
                if (!is_valid_global_sample(global_coord))
                    continue;

                const auto global_index = global_field_index(global_coord);
                const auto& previous_sample = global_field_[global_index];
                const auto& next_sample = field_samples[static_cast<std::size_t>(y) * padded_size.x + static_cast<std::size_t>(x)];
                if (cleared_keys != nullptr && is_solid(previous_sample) && !is_solid(next_sample))
                {
                    if (vegetation_ != nullptr)
                        vegetation_->clear_plant_at(global_index);
                    cleared_keys->insert(sample_key(global_coord));
                }

                global_field_[global_index] = next_sample;
            }
        }
    }

    void PlanetTerrain::refresh_surface_attachments_around(const std::vector<ivec2>& changed_coords)
    {
        if (changed_coords.empty()) return;

        std::unordered_set<std::uint64_t> affected_keys;
        affected_keys.reserve(changed_coords.size() * 9u);
        for (const auto coord : changed_coords)
        {
            for (int oy = -1; oy <= 1; ++oy)
            {
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const ivec2 neighbor{coord.x + ox, coord.y + oy};
                    if (!is_valid_global_sample(neighbor))
                        continue;
                    affected_keys.insert(sample_key(neighbor));
                }
            }
        }

        if (vegetation_ != nullptr)
        {
            vegetation_->refresh_surface_anchors(*this, affected_keys);
        }

        if (resources_ != nullptr)
        {
            static_cast<void>(resources_->erase_nodes_if(
                [this, &affected_keys](ResourceNode& node)
                {
                    if (!node.surface_attached)
                        return false;
                    if (!affected_keys.contains(sample_key(node.coord)))
                        return false;

                    const auto attachment = exposed_surface_attachment(node.coord);
                    if (!attachment.has_value())
                        return true;

                    node.anchor_world = attachment->anchor_world;
                    node.surface_up = attachment->surface_up;
                    return false;
                }));
        }
    }

    void PlanetTerrain::rebuild_water_blob_colliders()
    {
        TerrainWaterColliderBuilder::rebuild(water_collider_manager_, make_water_grid_view());
    }

    bool PlanetTerrain::is_valid_global_sample(const ivec2 coord) const
    {
        return coord.x >= 0 && coord.y >= 0 && coord.x < static_cast<int>(global_field_size_.x) &&
               coord.y < static_cast<int>(global_field_size_.y);
    }

    std::size_t PlanetTerrain::global_field_index(const ivec2 coord) const
    {
        return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(global_field_size_.x) + static_cast<std::size_t>(coord.x);
    }

    vec2 PlanetTerrain::global_sample_world_position(const ivec2 coord) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return global_field_origin_;

        return {global_field_origin_.x + static_cast<float>(coord.x) * terrain_cell_size_.x,
                global_field_origin_.y + static_cast<float>(coord.y) * terrain_cell_size_.y};
    }

    float PlanetTerrain::normalized_depth(const vec2 world_position) const
    {
        const float surface_radius = std::max(base_chunk_settings_.planet_radius, 1e-4f);
        const float radius = radial_dist(world_position, base_chunk_settings_.world_center);
        return std::clamp(1.0f - radius / surface_radius, 0.0f, 1.0f);
    }

    ivec2 PlanetTerrain::world_to_global_sample(const vec2 world_position) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return {0, 0};

        const float gx = (world_position.x - global_field_origin_.x) / terrain_cell_size_.x;
        const float gy = (world_position.y - global_field_origin_.y) / terrain_cell_size_.y;

        return {std::clamp(static_cast<int>(std::lround(gx)), 0, static_cast<int>(global_field_size_.x) - 1),
                std::clamp(static_cast<int>(std::lround(gy)), 0, static_cast<int>(global_field_size_.y) - 1)};
    }

    std::vector<PlanetTerrain::FieldSample> PlanetTerrain::extract_chunk_field(const ivec2 chunk_coord) const
    {
        const auto padded_size = padded_field_size(base_chunk_settings_);
        const auto stride = chunk_sample_stride(base_chunk_settings_);
        const ivec2 chunk_base{chunk_coord.x * stride.x, chunk_coord.y * stride.y};

        std::vector<FieldSample> field_samples(static_cast<std::size_t>(padded_size.x) * static_cast<std::size_t>(padded_size.y));
        for (std::uint32_t y = 0; y < padded_size.y; ++y)
        {
            for (std::uint32_t x = 0; x < padded_size.x; ++x)
            {
                const ivec2 global_coord{chunk_base.x + static_cast<int>(x), chunk_base.y + static_cast<int>(y)};
                field_samples[static_cast<std::size_t>(y) * padded_size.x + x] = global_field_[global_field_index(global_coord)];
            }
        }

        return field_samples;
    }

    Result<void> PlanetTerrain::rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks,
                                                     const bool smooth_water,
                                                     const bool rebuild_water)
    {
        const core::ScopedProfiler profiler{"terrain.rebuild_dirty_chunks"};
        static_cast<void>(profiler);

        const bool any_dirty = std::ranges::any_of(dirty_chunks, [](const bool dirty) { return dirty; });
        if (!any_dirty)
            return {};

        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if (!dirty_chunks[i])
                continue;

            auto rebuild_result = chunks_[i].rebuild_from_field(extract_chunk_field(chunks_[i].chunk_coord()), smooth_water, rebuild_water);
            if (!rebuild_result)
            {
                return fail("Failed to rebuild chunk ({}, {}): {}",
                            chunks_[i].chunk_coord().x,
                            chunks_[i].chunk_coord().y,
                            rebuild_result.error().message);
            }

            if (smooth_water)
            {
                // Smoothing happens on the GPU, so pull that version back or the CPU field would drift out of sync.
                auto synced_field = chunks_[i].readback_field();
                if (!synced_field)
                {
                    return fail("Failed to read back smoothed water field for chunk ({}, {}): {}",
                                chunks_[i].chunk_coord().x,
                                chunks_[i].chunk_coord().y,
                                synced_field.error().message);
                }
                sync_chunk_field_to_global(chunks_[i].chunk_coord(), *synced_field);
            }
        }

        if (rebuild_water)
            rebuild_water_blob_colliders();
        return {};
    }

    void PlanetTerrain::mark_chunks_covering_global_sample(const ivec2 coord, std::vector<bool>& dirty_chunks) const
    {
        const auto total_chunk_count = chunk_count();
        const auto padded_size = padded_field_size(base_chunk_settings_);
        const auto stride = chunk_sample_stride(base_chunk_settings_);

        const int base_x = coord.x / std::max(stride.x, 1);
        const int base_y = coord.y / std::max(stride.y, 1);

        for (int chunk_y = std::max(0, base_y - 1); chunk_y <= std::min(total_chunk_count.y - 1, base_y + 1); ++chunk_y)
        {
            for (int chunk_x = std::max(0, base_x - 1); chunk_x <= std::min(total_chunk_count.x - 1, base_x + 1); ++chunk_x)
            {
                const int local_x = coord.x - chunk_x * stride.x;
                const int local_y = coord.y - chunk_y * stride.y;
                if (local_x < 0 || local_y < 0)
                    continue;
                if (local_x >= static_cast<int>(padded_size.x) || local_y >= static_cast<int>(padded_size.y))
                    continue;

                dirty_chunks[flat_index({chunk_x, chunk_y}, total_chunk_count)] = true;
            }
        }
    }

    int PlanetTerrain::solid_neighbor_count(const ivec2 coord) const
    {
        int count = 0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0)
                    continue;

                const ivec2 neighbor{coord.x + x, coord.y + y};
                if (!is_valid_global_sample(neighbor))
                    continue;
                if (is_solid(global_field_[global_field_index(neighbor)]))
                    ++count;
            }
        }

        return count;
    }

    bool PlanetTerrain::has_water_neighbor(const ivec2 coord) const
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0)
                    continue;

                const ivec2 neighbor{coord.x + x, coord.y + y};
                if (!is_valid_global_sample(neighbor))
                    continue;
                if (has_water(global_field_[global_field_index(neighbor)]))
                    return true;
            }
        }

        return false;
    }

    bool PlanetTerrain::has_protective_water_neighbor(const ivec2 coord) const
    {
        if (!is_valid_global_sample(coord))
            return false;

        const vec2 sample_world = global_sample_world_position(coord);
        const vec2 up = normalize(sample_world - base_chunk_settings_.world_center, {0.0f, 1.0f});
        const vec2 tangent{up.y, -up.x};
        const float cell_extent = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
        const float tangential_limit = cell_extent * 2.35f;
        const float outward_limit = cell_extent * 2.35f;
        const float inward_allowance = cell_extent * 0.60f;
        static constexpr int search_radius = 4;

        for (int y = -search_radius; y <= search_radius; ++y)
        {
            for (int x = -search_radius; x <= search_radius; ++x)
            {
                if (x == 0 && y == 0)
                    continue;

                const ivec2 neighbor{coord.x + x, coord.y + y};
                if (!is_valid_global_sample(neighbor))
                    continue;

                const auto& neighbor_sample = global_field_[global_field_index(neighbor)];
                if (!has_water(neighbor_sample))
                    continue;

                const vec2 delta = global_sample_world_position(neighbor) - sample_world;
                const float tangent_offset = std::abs(delta.dot(tangent));
                const float up_offset = delta.dot(up);
                if (tangent_offset > tangential_limit)
                    continue;
                if (up_offset < -inward_allowance || up_offset > outward_limit)
                    continue;

                return true;
            }
        }

        return false;
    }

    bool PlanetTerrain::is_dig_protected(const ivec2 coord) const
    {
        if (!is_valid_global_sample(coord))
            return false;
        const auto& sample = global_field_[global_field_index(coord)];
        return has_water(sample) || has_protective_water_neighbor(coord) ||
               normalized_depth(global_sample_world_position(coord)) >= constants::hard_rock_depth_threshold;
    }

    TerrainSurfaceFieldView PlanetTerrain::make_surface_field_view() const
    {
        return {
            .world_center = base_chunk_settings_.world_center,
            .field_origin = global_field_origin_,
            .cell_size = terrain_cell_size_,
            .field_size = global_field_size_,
            .planet_radius = base_chunk_settings_.planet_radius,
            .field_samples = global_field_
        };
    }

    TerrainGenerationFieldView PlanetTerrain::make_generation_field_view()
    {
        return {
            .global_field = global_field_,
            .global_field_size = global_field_size_,
            .seed = base_chunk_settings_.seed
        };
    }

    water::GridView PlanetTerrain::make_water_grid_view() const
    {
        return water::GridView{
            .world_center = base_chunk_settings_.world_center,
            .field_origin = global_field_origin_,
            .cell_size = terrain_cell_size_,
            .field_size = global_field_size_,
            .field_samples = global_field_
        };
    }

    ivec2 PlanetTerrain::settle_water_anchor(const ivec2 anchor) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return anchor;

        return water::settle_water_anchor(make_water_grid_view(), anchor);
    }

    std::optional<ivec2> PlanetTerrain::find_water_anchor(const vec2 world_position) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return std::nullopt;

        return water::find_water_anchor(make_water_grid_view(), world_position);
    }

    std::optional<ivec2> PlanetTerrain::find_water_sample(const vec2 world_position) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return std::nullopt;

        return water::find_water_sample(make_water_grid_view(), world_position);
    }

    std::vector<ivec2> PlanetTerrain::collect_water_component(const ivec2 start_coord, const bool include_diagonals) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return {};

        return water::collect_water_component(make_water_grid_view(), start_coord, include_diagonals);
    }

    std::uint32_t PlanetTerrain::water_volume_at_anchor(const ivec2 anchor, ivec2* plan_start) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return 0u;

        return water::water_volume_at_anchor(make_water_grid_view(), anchor, plan_start);
    }

    std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_targeted_water_plan(const vec2 world_position,
                                                                                     const std::uint32_t volume_cap,
                                                                                     const bool pickup,
                                                                                     std::uint32_t* const existing_volume) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return std::nullopt;

        return water::build_targeted_water_plan(make_water_grid_view(), world_position, volume_cap, pickup, existing_volume);
    }

    Result<bool> PlanetTerrain::apply_water_plan_and_rebuild(const WaterPlan& plan)
    {
        std::vector<bool> dirty_chunks(chunks_.size(), false);
        std::vector<ivec2> changed_coords;
        const bool changed = apply_water_plan(plan, dirty_chunks, changed_coords);
        if (!changed)
            return false;

        // Water edits still go through the same rebuild path so mesh, wetness, and colliders stay together.
        ++field_revision_;
        ++water_revision_;
        ++geometry_revision_;
        recompute_wetness_around(changed_coords, dirty_chunks);
        TRY(rebuild_dirty_chunks(dirty_chunks, false));
        return true;
    }

    std::uint32_t PlanetTerrain::total_water_sample_count() const
    {
        return static_cast<std::uint32_t>(
            std::ranges::count_if(global_field_, [](const FieldSample& sample) { return has_water(sample); }));
    }

    std::vector<ivec2> PlanetTerrain::dirty_chunk_extent_markers(const std::vector<bool>& dirty_chunks) const
    {
        std::vector<ivec2> markers;
        const auto padded_size = padded_field_size(base_chunk_settings_);
        const auto stride = chunk_sample_stride(base_chunk_settings_);
        markers.reserve(chunks_.size() * 2u);

        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if (!dirty_chunks[i]) continue;
            const auto chunk_coord = chunks_[i].chunk_coord();
            const ivec2 chunk_base{chunk_coord.x * stride.x, chunk_coord.y * stride.y};
            markers.push_back(chunk_base);
            markers.push_back({chunk_base.x + static_cast<int>(padded_size.x) - 1, chunk_base.y + static_cast<int>(padded_size.y) - 1});
        }

        return markers;
    }

    PlanetTerrain::TerrainEditResult PlanetTerrain::apply_terrain_edit_to_global_field(const TerrainEdit& edit,
                                                                                       std::vector<bool>& dirty_chunks,
                                                                                       std::vector<ivec2>& changed_coords,
                                                                                       const std::uint32_t unit_budget,
                                                                                       const std::optional<GroundBrushBlocker>& blocker)
    {
        std::unordered_set<std::uint64_t> cleared_keys;
        // The brush system does the per-sample math; this wrapper supplies the terrain-specific rules.
        auto result = TerrainBrushSystem::apply_edit(
            global_field_,
            global_field_size_,
            global_field_origin_,
            terrain_cell_size_,
            grid_min_,
            grid_max_,
            base_chunk_settings_,
            edit,
            dirty_chunks,
            changed_coords,
            unit_budget,
            blocker,
            [this](const ivec2 coord) { return is_dig_protected(coord); },
            [this](const ivec2 coord) { return has_water_neighbor(coord); },
            [this](const float density, const vec2 world, const ChunkSettings& settings)
            { return clamp_terrain_density(density, world, settings); },
            [](const FieldSample& sample) { return dry_water_density(sample); },
            [this](const ivec2 coord) { return global_sample_world_position(coord); },
            [this](const ivec2 coord, std::vector<bool>& dirty_flags) { mark_chunks_covering_global_sample(coord, dirty_flags); },
            [this, &cleared_keys](const ivec2 coord, const std::size_t sample_index)
            {
                if (vegetation_ != nullptr)
                    vegetation_->clear_plant_at(sample_index);
                cleared_keys.insert(sample_key(coord));
            });

        if (resources_ != nullptr && !cleared_keys.empty())
            static_cast<void>(resources_->remove_nodes(cleared_keys));
        refresh_surface_attachments_around(changed_coords);
        return result;
    }

    std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_water_plan(const ivec2 start_coord,
                                                                            const std::uint32_t desired_wet_sample_count,
                                                                            const bool preserve_existing_water) const
    {
        return water::build_water_plan(make_water_grid_view(), start_coord, desired_wet_sample_count, preserve_existing_water);
    }

    bool PlanetTerrain::apply_water_plan(const WaterPlan& plan, std::vector<bool>& dirty_chunks, std::vector<ivec2>& changed_coords)
    {
        bool changed = false;
        for (const auto& coord : plan.dried_component)
        {
            auto& sample = global_field_[global_field_index(coord)];
            const float next_water = dry_water_density(sample);
            if (std::abs(sample.water - next_water) <= 1e-6f) continue;

            sample.water = next_water;
            changed = true;
            changed_coords.push_back(coord);
            mark_chunks_covering_global_sample(coord, dirty_chunks);
        }

        for (const auto& entry : plan.affected_samples)
        {
            auto& sample = global_field_[global_field_index(entry.coord)];
            const float next_water = sample.terrain < 0.0f && entry.water > 0.0f ? entry.water : dry_water_density(sample);
            if (std::abs(sample.water - next_water) <= 1e-6f) continue;

            sample.water = next_water;
            changed = true;
            changed_coords.push_back(entry.coord);
            mark_chunks_covering_global_sample(entry.coord, dirty_chunks);
        }

        return changed;
    }

    void PlanetTerrain::recompute_wetness_around(const std::vector<ivec2>& changed_coords, std::vector<bool>& dirty_chunks)
    {
        const core::ScopedProfiler profiler{"terrain.recompute_wetness_around"};
        static_cast<void>(profiler);

        // Wetness only needs to be refreshed around changed water, not across the whole planet every time.
        terrain_wetness::recompute_around(
            global_field_,
            global_field_size_,
            terrain_cell_size_,
            changed_coords,
            [this, &dirty_chunks](const ivec2 coord) { mark_chunks_covering_global_sample(coord, dirty_chunks); },
            [this](const ivec2 coord, const bool include_diagonals) { return collect_water_component(coord, include_diagonals); });

        recompute_ground_greenness(dirty_chunks);
        ++field_revision_;
    }

    void PlanetTerrain::recompute_ground_greenness(std::vector<bool>& dirty_chunks)
    {
        if (vegetation_ == nullptr) return;
        const auto plant_samples = vegetation_->plant_samples();
        if (global_field_.empty() || plant_samples.empty()) return;

        // Greenness is just a terrain-side visual mask derived from the current plant layout.
        terrain_greenness::recompute(
            global_field_,
            global_field_size_,
            terrain_cell_size_,
            plant_samples,
            vegetation_->active_plant_indices(),
            [this, &dirty_chunks](const ivec2 coord) { mark_chunks_covering_global_sample(coord, dirty_chunks); },
            [this](const ivec2 coord) { return global_sample_world_position(coord); });
    }

    void PlanetTerrain::validate() const
    {
#ifndef NDEBUG
        static ValidationState previous_state;
        ValidationState current_state{};

        current_state.resource_system_null = resources_ == nullptr;
        current_state.vegetation_system_null = vegetation_ == nullptr;
        current_state.pending_dirty_chunk_size_mismatch =
            !pending_ground_brush_dirty_chunks_.empty() && pending_ground_brush_dirty_chunks_.size() != chunks_.size();
        current_state.global_field_size_mismatch = false;

        if (!global_field_.empty() &&
            global_field_.size() != static_cast<std::size_t>(global_field_size_.x) * static_cast<std::size_t>(global_field_size_.y))
        {
            current_state.global_field_size_mismatch = true;
        }

        for (std::size_t i = 0; i < global_field_.size(); ++i)
        {
            const auto& sample = global_field_[i];
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

        log_validation_transition(current_state.resource_system_null,
                                  previous_state.resource_system_null,
                                  "PlanetTerrain validation failed: resource system is null");
        log_validation_transition(current_state.vegetation_system_null,
                                  previous_state.vegetation_system_null,
                                  "PlanetTerrain validation failed: vegetation system is null");
        if (current_state.pending_dirty_chunk_size_mismatch != previous_state.pending_dirty_chunk_size_mismatch)
        {
            previous_state.pending_dirty_chunk_size_mismatch = current_state.pending_dirty_chunk_size_mismatch;
            if (current_state.pending_dirty_chunk_size_mismatch)
            {
                Log::error("PlanetTerrain validation failed: pending dirty chunk array size {} does not match chunk count {}",
                           pending_ground_brush_dirty_chunks_.size(),
                           chunks_.size());
            }
        }

        if (current_state.global_field_size_mismatch != previous_state.global_field_size_mismatch)
        {
            previous_state.global_field_size_mismatch = current_state.global_field_size_mismatch;
            if (current_state.global_field_size_mismatch)
            {
                Log::error("PlanetTerrain validation failed: global field size {} does not match dimensions {}x{}",
                           global_field_.size(),
                           global_field_size_.x,
                           global_field_size_.y);
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
    vec2 PlanetTerrain::terrain_cell_size() const { return terrain_cell_size_; }
    vec2 PlanetTerrain::planet_center() const { return base_chunk_settings_.world_center; }

    bool PlanetTerrain::contains_water_volume(const vec2 world_position) const
    {
        if (global_field_.empty() || global_field_size_.x == 0u || global_field_size_.y == 0u)
            return false;

        const float gx = (world_position.x - global_field_origin_.x) / terrain_cell_size_.x;
        const float gy = (world_position.y - global_field_origin_.y) / terrain_cell_size_.y;

        const float clamped_x = std::clamp(gx, 0.0f, static_cast<float>(global_field_size_.x - 1u));
        const float clamped_y = std::clamp(gy, 0.0f, static_cast<float>(global_field_size_.y - 1u));

        const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
        const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
        const auto x1 = std::min(x0 + 1u, global_field_size_.x - 1u);
        const auto y1 = std::min(y0 + 1u, global_field_size_.y - 1u);

        const float tx = clamped_x - static_cast<float>(x0);
        const float ty = clamped_y - static_cast<float>(y0);

        auto water_field_at = [this](const std::uint32_t x, const std::uint32_t y)
        {
            const auto& sample = global_field_[static_cast<std::size_t>(y) * global_field_size_.x + x];
            return std::min(-sample.terrain, sample.water);
        };

        const float value = std::lerp(std::lerp(water_field_at(x0, y0), water_field_at(x1, y0), tx),
                                      std::lerp(water_field_at(x0, y1), water_field_at(x1, y1), tx),
                                      ty);

        return value > 0.0f;
    }

    vec2 PlanetTerrain::spawn_point_from_top_center(const float height_offset) const
    {
        const auto terrain_world_center = base_chunk_settings_.world_center;
        const vec2 ray_origin{terrain_world_center.x, display_max_.y + base_chunk_settings_.chunk_size.y * 2.0f};
        const vec2 ray_end{terrain_world_center.x, display_min_.y - base_chunk_settings_.chunk_size.y * 2.0f};
        const b2QueryFilter filter = b2DefaultQueryFilter();
        const auto ray_result =
            b2World_CastRayClosest(world_id_, {ray_origin.x, ray_origin.y}, {ray_end.x - ray_origin.x, ray_end.y - ray_origin.y}, filter);

        if (ray_result.hit)
        {
            const vec2 hit_point{ray_result.point.x, ray_result.point.y};
            const vec2 radial_up = normalize(hit_point - terrain_world_center);
            return { hit_point.x + radial_up.x * height_offset, hit_point.y + radial_up.y * height_offset };
        }

        return { terrain_world_center.x, display_max_.y + height_offset };
    }

    float PlanetTerrain::compute_planet_radius()
    {
        const auto total_chunk_count = chunk_count();
        const ChunkSettings defaults{};
        const auto terrain_chunk_size = defaults.chunk_size;
        const vec2 total_world_size{terrain_chunk_size.x * static_cast<float>(total_chunk_count.x),
                                    terrain_chunk_size.y * static_cast<float>(total_chunk_count.y)};

        const auto min_half_extent = std::min(total_world_size.x, total_world_size.y) * 0.5f;
        const auto edge_padding = std::min(terrain_chunk_size.x, terrain_chunk_size.y) * 0.75f;
        return std::max(min_half_extent - edge_padding, 1.0f);
    }

    std::size_t PlanetTerrain::flat_index(const ivec2 chunk_index, const ivec2 chunk_count)
    {
        return static_cast<std::size_t>(chunk_index.y * chunk_count.x + chunk_index.x);
    }

    ivec2 PlanetTerrain::chunk_index_from_world(const vec2 world_position) const
    {
        const auto total_chunk_count = chunk_count();
        const auto local_x = (world_position.x - grid_min_.x) / base_chunk_settings_.chunk_size.x;
        const auto local_y = (world_position.y - grid_min_.y) / base_chunk_settings_.chunk_size.y;

        return {
            std::clamp(static_cast<int>(std::floor(local_x)), 0, total_chunk_count.x - 1),
            std::clamp(static_cast<int>(std::floor(local_y)), 0, total_chunk_count.y - 1)
        };
    }
}
