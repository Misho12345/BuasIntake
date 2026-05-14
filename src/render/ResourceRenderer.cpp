#include "pch.hpp"

#include "render/ResourceRenderer.hpp"

#include "gfx/AlphaBlendPass.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "terrain/TerrainResourceNoise.hpp"

namespace game::render
{
    namespace
    {
        float texture_layer_for(const resources::ResourceKind kind)
        {
            switch (kind)
            {
                case resources::ResourceKind::Rock: return 0.0f;
                case resources::ResourceKind::IronOre: return 1.0f;
                case resources::ResourceKind::CopperOre: return 2.0f;
                case resources::ResourceKind::GoldOre: return 3.0f;
                case resources::ResourceKind::DiamondOre: return 4.0f;
            }

            return -1.0f;
        }
    }

    Result<void> ResourceRenderer::initialize_assets()
    {
        if (ore_batch_.valid()) return {};

        static constexpr std::array ore_paths
        {
            "assets/images/ores/rock.png",
            "assets/images/ores/iron_ore.png",
            "assets/images/ores/copper_ore.png",
            "assets/images/ores/gold_ore.png",
            "assets/images/ores/diamond_ore.png"
        };

        TRY(ore_batch_.initialize(ore_paths, 32u));
        last_nodes_revision_ = std::numeric_limits<std::uint64_t>::max();
        return {};
    }

    void ResourceRenderer::destroy_graphics_resources()
    {
        ore_batch_.destroy_graphics_resources();
        cached_instances_.clear();
        last_nodes_revision_    = std::numeric_limits<std::uint64_t>::max();
        ore_instances_uploaded_ = false;
    }

    Result<void> ResourceRenderer::rebuild_ore_instances(
        const terrain::PlanetTerrain&    terrain,
        const resources::ResourceSystem& resources)
    {
        if (!ore_batch_.valid()) return fail("Resource renderer assets are not initialized");

        cached_instances_.clear();
        cached_instances_.reserve(resources.nodes().size());

        for (const auto& resource : resources.nodes())
        {
            if (resource.kind == resources::ResourceKind::DeadPlant) continue;
            if (!terrain.is_valid_global_sample(resource.coord)) continue;

            const auto& sample = terrain.global_sample(resource.coord);
            if (sample.terrain < 0.0f) continue;

            const vec2 world_position = resource.surface_attached
                                            ? resource.anchor_world
                                            : terrain.global_sample_world_position(resource.coord);

            const vec2 up = resource.surface_up.lengthSquared() > 1e-6f
                                ? normalize(resource.surface_up * -1.0f)
                                : normalize(world_position - terrain.planet_center());

            const bool exposed = terrain.is_sample_exposed_to_air(resource.coord);

            float world_height  = 1.96f;
            float radial_offset = 0.01f;
            if (resource.surface_attached)
            {
                world_height  = 2.24f;
                radial_offset = world_height * (exposed ? 0.46f : 0.38f);
            }

            const float angle_offset = game::terrain::TerrainResourceNoise::hash01(
                static_cast<float>(resource.coord.x),
                static_cast<float>(resource.coord.y),
                static_cast<std::uint32_t>(resource.variant) + static_cast<std::uint32_t>(resource.kind) * 131u) * tau;

            cached_instances_.push_back({
                .center_world  = world_position,
                .up            = up,
                .world_height  = world_height,
                .radial_offset = radial_offset,
                .texture_layer = texture_layer_for(resource.kind),
                .tile_column   = static_cast<float>((resource.variant * 5u + 3u) % 8u),
                .tile_row      = static_cast<float>(resource.variant % 16u),
                .angle_offset  = angle_offset
            });
        }

        last_nodes_revision_    = resources.nodes_revision();
        ore_instances_uploaded_ = false;
        return {};
    }

    void ResourceRenderer::draw_ores(
        const sf::View&                  view,
        const terrain::PlanetTerrain&    terrain,
        const resources::ResourceSystem& resources)
    {
        if (!ore_batch_.valid())
        {
            Log::error("Resource renderer assets are not initialized");
            return;
        }

        if (last_nodes_revision_ != resources.nodes_revision())
        {
            if (const auto rebuild_result = rebuild_ore_instances(terrain, resources);
                !rebuild_result)
            {
                Log::error(rebuild_result.error());
                return;
            }
        }

        if (cached_instances_.empty()) return;

        [[maybe_unused]]
        const gfx::ScopedAlphaBlendPass blend_pass{};

        if (!ore_instances_uploaded_)
        {
            ore_batch_.upload_instances(cached_instances_);
            ore_instances_uploaded_ = true;
        }

        ore_batch_.draw(view);
    }
}
