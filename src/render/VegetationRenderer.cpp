#include "pch.hpp"

#include "render/VegetationRenderer.hpp"

#include "gfx/AlphaBlendPass.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "vegetation/Plant.hpp"
#include "vegetation/VegetationSystem.hpp"

namespace game::render
{
    namespace
    {
        struct DeadPlantSpriteFamily final
        {
            const char* path{ nullptr };
            int         tile_size{ 32 };
            float       world_height{ 1.45f };
        };

        enum class VegetationBatchId : std::size_t
        {
            Live32 = 0,
            Live64 = 1,
            Dead32 = 2,
            Dead64 = 3
        };

        inline constexpr std::size_t vegetation_batch_count{ 4u };

        inline constexpr std::array<DeadPlantSpriteFamily, 10> dead_plant_sprite_families{
            {
                { "assets/images/vegetation/ground_plants_dead.png", 32, 2.90f },
                { "assets/images/vegetation/mushrooms_dead.png", 32, 2.00f },
                { "assets/images/vegetation/ferns_dead.png", 32, 3.00f },
                { "assets/images/vegetation/broadleaf_plants_dead.png", 32, 3.16f },
                { "assets/images/vegetation/reeds_dead.png", 32, 3.50f },
                { "assets/images/vegetation/creepers_dead.png", 32, 3.24f },
                { "assets/images/vegetation/jungle_roots_dead.png", 32, 3.90f },
                { "assets/images/vegetation/hanging_vines_dead.png", 32, 3.76f },
                { "assets/images/vegetation/bushes_dead.png", 32, 3.70f },
                { "assets/images/vegetation/trees_dead.png", 64, 7.90f }
            }
        };

        struct PlantVisualSpec final
        {
            VegetationBatchId batch_id{ VegetationBatchId::Live32 };
            float             texture_layer{ 0.0f };
            std::uint8_t      column{ 0u };
            float             height{ 0.22f };
            float             angle_offset{ pi };
            float             radial_offset{ -0.12f };
            bool              valid{ true };
        };

        // this is the plant visual lookup table in code form
        // growth stage and family decide which atlas layer column and world height to use so the sim state can stay small
        PlantVisualSpec plant_visual_spec(const vegetation::Plant& plant)
        {
            PlantVisualSpec spec{};

            const float sprout_fraction =
                    std::clamp((plant.age - vegetation::seed_to_sprout_time) /
                               vegetation::sprout_growth_duration,
                               0.0f, 1.0f);

            if (plant.stage == vegetation::PlantStage::Sprout)
            {
                spec.column = static_cast<std::uint8_t>(
                    2u + std::min(3, static_cast<int>(std::floor(sprout_fraction * 4.0f))));

                switch (plant.family)
                {
                    case vegetation::PlantFamily::Grass:
                        spec.texture_layer = 1.0f;
                        spec.height = 1.8f;
                        break;

                    case vegetation::PlantFamily::Flowers:
                        spec.texture_layer = 2.0f;
                        spec.height = 2.0f;
                        break;

                    case vegetation::PlantFamily::Bush:
                        spec.texture_layer = 3.0f;
                        spec.height = 2.8f;
                        break;

                    case vegetation::PlantFamily::Tree:
                        spec.batch_id = VegetationBatchId::Live64;
                        spec.texture_layer = 0.0f;
                        spec.height        = 5.2f;
                        break;
                }

                return spec;
            }

            if (plant.stage == vegetation::PlantStage::Mature)
            {
                spec.column = 7u;
                switch (plant.family)
                {
                    case vegetation::PlantFamily::Grass:
                        spec.texture_layer = 1.0f;
                        spec.height = 2.70f;
                        break;

                    case vegetation::PlantFamily::Flowers:
                        spec.texture_layer = 2.0f;
                        spec.height = 3.10f;
                        break;

                    case vegetation::PlantFamily::Bush:
                        spec.texture_layer = 3.0f;
                        spec.height = 4.8f;
                        break;

                    case vegetation::PlantFamily::Tree:
                        spec.batch_id = VegetationBatchId::Live64;
                        spec.texture_layer = 0.0f;
                        spec.height        = 13.6f;
                        break;
                }
            }

            return spec;
        }
    }

    Result<void> VegetationRenderer::initialize_assets()
    {
        static constexpr std::array live_32_paths{
            "assets/images/vegetation/ground_plants.png",
            "assets/images/vegetation/grass.png",
            "assets/images/vegetation/flowers.png",
            "assets/images/vegetation/bushes.png"
        };

        static constexpr std::array<const char*, 1> live_64_paths{ "assets/images/vegetation/trees.png" };

        static constexpr std::array dead_32_paths{
            "assets/images/vegetation/ground_plants_dead.png",
            "assets/images/vegetation/mushrooms_dead.png",
            "assets/images/vegetation/ferns_dead.png",
            "assets/images/vegetation/broadleaf_plants_dead.png",
            "assets/images/vegetation/reeds_dead.png",
            "assets/images/vegetation/creepers_dead.png",
            "assets/images/vegetation/jungle_roots_dead.png",
            "assets/images/vegetation/hanging_vines_dead.png",
            "assets/images/vegetation/bushes_dead.png"
        };

        static constexpr std::array<const char*, 1> dead_64_paths{ "assets/images/vegetation/trees_dead.png" };

        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Live32)].initialize(live_32_paths, 32u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Live64)].initialize(live_64_paths, 64u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Dead32)].initialize(dead_32_paths, 32u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Dead64)].initialize(dead_64_paths, 64u));
        return {};
    }

    void VegetationRenderer::destroy_graphics_resources()
    {
        for (auto& resources : batch_resources_) resources.destroy_graphics_resources();
        for (auto& instances : cached_instances_by_batch_) instances.clear();

        cached_low_cover_live32_instances_.clear();
        cached_woody_live32_instances_.clear();
        visible_live64_instances_.clear();
        visible_woody_live32_instances_.clear();
        visible_low_cover_live32_instances_.clear();
        visible_dead32_instances_.clear();
        visible_dead64_instances_.clear();

        last_vegetation_revision_ = std::numeric_limits<std::uint64_t>::max();
        last_resource_revision_   = std::numeric_limits<std::uint64_t>::max();
    }

    // this rebuilds the cpu side instance lists from the sim state
    // the split between low cover woody and dead variants is mostly about batching and draw order sanity later on
    Result<void> VegetationRenderer::rebuild_instances(
        const terrain::PlanetTerrain&       terrain,
        const vegetation::VegetationSystem& vegetation,
        const resources::ResourceSystem&    resources)
    {
        for (auto& instances : cached_instances_by_batch_) instances.clear();
        cached_low_cover_live32_instances_.clear();
        cached_woody_live32_instances_.clear();

        const auto plant_samples = vegetation.plant_samples();
        for (const auto index : vegetation.active_plant_indices())
        {
            if (index >= plant_samples.size()) continue;
            const auto& plant = plant_samples[index];
            if (plant.stage == vegetation::PlantStage::Empty) continue;

            const ivec2 coord{
                static_cast<int>(index % terrain.global_field_size().x),
                static_cast<int>(index / terrain.global_field_size().x)
            };

            if (!terrain.is_valid_global_sample(coord)) continue;
            if (terrain.global_sample(coord).terrain < 0.0f) continue;

            const vec2 world_position = terrain.global_sample_world_position(coord);
            const vec2 anchor_world = plant.anchor_world.lengthSquared() > 1e-6f ? plant.anchor_world : world_position;
            const auto spec = plant_visual_spec(plant);
            if (!spec.valid) continue;

            const vec2           up = normalize(anchor_world - terrain.planet_center());
            const SpriteInstance instance{
                .center_world  = anchor_world,
                .up            = up,
                .world_height  = spec.height,
                .radial_offset = spec.radial_offset,
                .texture_layer = spec.texture_layer,
                .tile_column   = static_cast<float>(spec.column),
                .tile_row      = static_cast<float>(plant.variant % 16u),
                .angle_offset  = spec.angle_offset
            };

            if (spec.batch_id == VegetationBatchId::Live32)
            {
                if (plant.family == vegetation::PlantFamily::Bush) cached_woody_live32_instances_.push_back(instance);
                else cached_low_cover_live32_instances_.push_back(instance);
            }
            else cached_instances_by_batch_[static_cast<std::size_t>(spec.batch_id)].push_back(instance);
        }

        for (const auto& resource : resources.nodes())
        {
            if (resource.kind != resources::ResourceKind::DeadPlant) continue;
            if (!terrain.is_valid_global_sample(resource.coord)) continue;
            if (terrain.global_sample(resource.coord).terrain < 0.0f) continue;

            const vec2 world_position =
                    resource.surface_attached
                        ? resource.anchor_world
                        : terrain.global_sample_world_position(resource.coord);

            const std::size_t family_index = std::min<std::size_t>(
                resource.variant / 16u,
                dead_plant_sprite_families.size() - 1u);

            const auto batch_id =
                    family_index == dead_plant_sprite_families.size() - 1u
                        ? VegetationBatchId::Dead64
                        : VegetationBatchId::Dead32;

            const float texture_layer = static_cast<float>(
                family_index == dead_plant_sprite_families.size() - 1u
                    ? 0u
                    : family_index);

            const vec2 up = resource.surface_up.lengthSquared() > 1e-6f
                                ? normalize(resource.surface_up * -1.0f)
                                : normalize(world_position - terrain.planet_center());

            cached_instances_by_batch_[static_cast<std::size_t>(batch_id)].push_back(
                {
                    .center_world  = world_position,
                    .up            = up,
                    .world_height  = dead_plant_sprite_families[family_index].world_height,
                    .radial_offset = 0.0f,
                    .texture_layer = texture_layer,
                    .tile_column   = 7.0f,
                    .tile_row      = static_cast<float>(resource.variant % 16u),
                    .angle_offset  = 0.12f
                });
        }

        last_vegetation_revision_ = vegetation.revision();
        last_resource_revision_   = resources.nodes_revision();
        return {};
    }

    // rebuild only when the source revisions changed then do a cheap radius cull against the current view before upload and draw
    void VegetationRenderer::draw(
        const sf::View&                     view,
        const terrain::PlanetTerrain&       terrain,
        const vegetation::VegetationSystem& vegetation,
        const resources::ResourceSystem&    resources)
    {
        if (const auto result = initialize_assets(); !result)
        {
            Log::error(result.error());
            return;
        }

        const vec2  view_center{ view.getCenter().x, view.getCenter().y };
        const vec2  view_size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
        const float visible_radius    = std::sqrt(view_size.x * view_size.x + view_size.y * view_size.y) * 0.5f + 4.0f;
        const float visible_radius_sq = visible_radius * visible_radius;

        if (last_vegetation_revision_ != vegetation.revision() ||
            last_resource_revision_ != resources.nodes_revision())
        {
            if (const auto result = rebuild_instances(terrain, vegetation, resources); !result)
            {
                Log::error(result.error());
                return;
            }
        }

        [[maybe_unused]]
        const gfx::ScopedAlphaBlendPass blend_pass{};

        auto filter_visible_instances = [&](
            std::span<const SpriteInstance> source,
            std::vector<SpriteInstance>&    visible_instances)
        {
            visible_instances.clear();
            visible_instances.reserve(source.size());
            for (const auto& instance : source)
            {
                const vec2 delta = instance.center_world - view_center;
                if (delta.x * delta.x + delta.y * delta.y > visible_radius_sq) continue;
                visible_instances.push_back(instance);
            }
        };

        auto draw_batch = [&](
            const VegetationBatchId         batch_id,
            std::span<const SpriteInstance> source_instances,
            std::vector<SpriteInstance>&    visible_instances)
        {
            const auto batch_index = static_cast<std::size_t>(batch_id);
            filter_visible_instances(source_instances, visible_instances);
            if (visible_instances.empty()) return;
            batch_resources_[batch_index].upload_instances(visible_instances);
            batch_resources_[batch_index].draw(view);
        };

        draw_batch(VegetationBatchId::Live64,
                   cached_instances_by_batch_[static_cast<std::size_t>(VegetationBatchId::Live64)],
                   visible_live64_instances_);

        draw_batch(VegetationBatchId::Live32, cached_woody_live32_instances_, visible_woody_live32_instances_);
        draw_batch(VegetationBatchId::Live32, cached_low_cover_live32_instances_, visible_low_cover_live32_instances_);
        draw_batch(VegetationBatchId::Dead32,
                   cached_instances_by_batch_[static_cast<std::size_t>(VegetationBatchId::Dead32)],
                   visible_dead32_instances_);

        draw_batch(VegetationBatchId::Dead64,
                   cached_instances_by_batch_[static_cast<std::size_t>(VegetationBatchId::Dead64)],
                   visible_dead64_instances_);
    }
}
