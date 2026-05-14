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
        enum class VegetationBatchId : std::size_t
        {
            Live32 = 0,
            Live64 = 1,
            Dead32 = 2,
            Dead64 = 3
        };

        inline constexpr std::array dead_plant_world_heights{
            2.90f,
            3.00f,
            3.16f,
            3.50f,
            3.24f,
            3.76f,
            7.90f
        };
        // these heights line up with the dead plant texture family indexes encoded by TerrainResourceSpawner variants

        struct PlantVisualSpec final
        {
            VegetationBatchId batch_id{ VegetationBatchId::Live32 };
            float             texture_layer{ 0.0f };
            std::uint8_t      column{ 0u };
            float             height{ 0.22f };
            float             angle_offset{ pi };
            float             radial_offset{ -0.12f };
        };

        // this is the plant visual lookup table in code form
        // growth stage and family decide which atlas layer column and world height to use so the sim state can stay small
        // the pixel art sheets were generated separately, so this table is the translation from gameplay plant state to sprite sheet position
        PlantVisualSpec plant_visual_spec(const vegetation::Plant& plant)
        {
            PlantVisualSpec spec{};

            const float sprout_fraction =
                    std::clamp((plant.age - vegetation::seed_to_sprout_time) /
                               vegetation::sprout_growth_duration,
                               0.0f, 1.0f);

            if (plant.stage == vegetation::PlantStage::Sprout)
            {
                // columns 2-5 are the growing transition frames, leaving earlier/later columns for seeded and mature art
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
        if (std::ranges::all_of(batch_resources_, [](const auto& resources) { return resources.valid(); })) return {};

        static constexpr std::array live_32_paths{
            "assets/images/vegetation/ground_plants.png",
            "assets/images/vegetation/grass.png",
            "assets/images/vegetation/flowers.png",
            "assets/images/vegetation/bushes.png"
        };

        static constexpr std::array<const char*, 1> live_64_paths{ "assets/images/vegetation/trees.png" };

        static constexpr std::array dead_32_paths{
            "assets/images/vegetation/ground_plants_dead.png",
            "assets/images/vegetation/ferns_dead.png",
            "assets/images/vegetation/broadleaf_plants_dead.png",
            "assets/images/vegetation/reeds_dead.png",
            "assets/images/vegetation/creepers_dead.png",
            "assets/images/vegetation/bushes_dead.png"
        };

        static constexpr std::array<const char*, 1> dead_64_paths{ "assets/images/vegetation/trees_dead.png" };

        // 32px and 64px sheets are separate batches because texture arrays need every layer in a batch to have the same size

        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Live32)].initialize(live_32_paths, 32u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Live64)].initialize(live_64_paths, 64u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Dead32)].initialize(dead_32_paths, 32u));
        TRY(batch_resources_[static_cast<std::size_t>(VegetationBatchId::Dead64)].initialize(dead_64_paths, 64u));

        static_assert(dead_32_paths.size() + dead_64_paths.size() == dead_plant_world_heights.size());
        return {};
    }

    void VegetationRenderer::destroy_graphics_resources()
    {
        for (auto& resources : batch_resources_) resources.destroy_graphics_resources();

        cached_live64_instances_.clear();
        cached_low_cover_live32_instances_.clear();
        cached_woody_live32_instances_.clear();
        cached_dead32_instances_.clear();
        cached_dead64_instances_.clear();
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
    void VegetationRenderer::rebuild_instances(
        const terrain::PlanetTerrain&       terrain,
        const vegetation::VegetationSystem& vegetation,
        const resources::ResourceSystem&    resources)
    {
        cached_live64_instances_.clear();
        cached_low_cover_live32_instances_.clear();
        cached_woody_live32_instances_.clear();
        cached_dead32_instances_.clear();
        cached_dead64_instances_.clear();

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
            else cached_live64_instances_.push_back(instance);
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
                dead_plant_world_heights.size() - 1u);

            // high bits choose the dead plant family, low bits choose the row variant inside that family
            const auto batch_id =
                    family_index == dead_plant_world_heights.size() - 1u
                        ? VegetationBatchId::Dead64
                        : VegetationBatchId::Dead32;

            const float texture_layer = static_cast<float>(
                family_index == dead_plant_world_heights.size() - 1u
                    ? 0u
                    : family_index);

            const vec2 up = resource.surface_up.lengthSquared() > 1e-6f
                                ? normalize(-resource.surface_up)
                                : normalize(world_position - terrain.planet_center());

            auto& cached_instances = batch_id == VegetationBatchId::Dead64 ? cached_dead64_instances_ : cached_dead32_instances_;
            cached_instances.push_back(
                {
                    .center_world  = world_position,
                    .up            = up,
                    .world_height  = dead_plant_world_heights[family_index],
                    .radial_offset = 0.0f,
                    .texture_layer = texture_layer,
                    .tile_column   = 7.0f,
                    .tile_row      = static_cast<float>(resource.variant % 16u),
                    .angle_offset  = 0.12f
                });
        }

        last_vegetation_revision_ = vegetation.revision();
        last_resource_revision_   = resources.nodes_revision();
    }

    // rebuild only when the source revisions changed then do a cheap radius cull against the current view before upload and draw
    void VegetationRenderer::draw(
        const sf::View&                     view,
        const terrain::PlanetTerrain&       terrain,
        const vegetation::VegetationSystem& vegetation,
        const resources::ResourceSystem&    resources)
    {
        if (!std::ranges::all_of(batch_resources_, [](const auto& resources) { return resources.valid(); }))
        {
            Log::error("Vegetation renderer assets are not initialized");
            return;
        }

        const vec2  view_center = view.getCenter();
        const vec2  view_size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
        const float visible_radius    = view_size.length() * 0.5f + 4.0f;
        const float visible_radius_sq = visible_radius * visible_radius;

        if (last_vegetation_revision_ != vegetation.revision() ||
            last_resource_revision_ != resources.nodes_revision())
        {
            rebuild_instances(terrain, vegetation, resources);
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
                if (delta.lengthSquared() > visible_radius_sq) continue;
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
            // upload only visible instances each frame; rebuilding the full cached list is reserved for revision changes
            batch_resources_[batch_index].upload_instances(visible_instances);
            batch_resources_[batch_index].draw(view);
        };

        draw_batch(VegetationBatchId::Live64, cached_live64_instances_, visible_live64_instances_);
        draw_batch(VegetationBatchId::Live32, cached_woody_live32_instances_, visible_woody_live32_instances_);
        draw_batch(VegetationBatchId::Live32, cached_low_cover_live32_instances_, visible_low_cover_live32_instances_);
        draw_batch(VegetationBatchId::Dead32, cached_dead32_instances_, visible_dead32_instances_);
        draw_batch(VegetationBatchId::Dead64, cached_dead64_instances_, visible_dead64_instances_);
    }
}
