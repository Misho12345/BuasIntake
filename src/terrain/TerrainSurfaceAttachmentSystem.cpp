#include "pch.hpp"

#include "terrain/TerrainSurfaceAttachmentSystem.hpp"

namespace game::terrain
{
    std::optional<TerrainSurfaceAttachment> TerrainSurfaceAttachmentSystem::exposed_surface_attachment(
        const TerrainField& field,
        const vec2          world_center,
        const ivec2         coord) const
    {
        return terrain_surface_sampler::exposed_surface_attachment(make_field_view(field, world_center), coord);
    }

    std::optional<vec2> TerrainSurfaceAttachmentSystem::surface_anchor_world(
        const TerrainField& field,
        const vec2          world_center,
        const ivec2         coord) const
    {
        return terrain_surface_sampler::surface_anchor_world(make_field_view(field, world_center), coord);
    }

    void TerrainSurfaceAttachmentSystem::refresh_around(
        const TerrainField&        field,
        const vec2                 world_center,
        const std::vector<ivec2>&  changed_coords,
        resources::ResourceSystem* resources,
        const RefreshVegetation&   refresh_vegetation) const
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
                    const ivec2 neighbor{ coord.x + ox, coord.y + oy };
                    if (!field.is_valid_sample(neighbor)) continue;
                    affected_keys.insert(sample_key(neighbor));
                }
            }
        }

        if (refresh_vegetation) refresh_vegetation(affected_keys);

        if (resources == nullptr) return;

        static_cast<void>(resources->erase_nodes_if(
            [this, &field, world_center, &affected_keys](resources::ResourceNode& node)
            {
                if (!node.surface_attached) return false;
                if (!affected_keys.contains(sample_key(node.coord))) return false;

                const auto attachment = exposed_surface_attachment(field, world_center, node.coord);
                if (!attachment.has_value()) return true;

                node.anchor_world = attachment->anchor_world;
                node.surface_up   = attachment->surface_up;
                return false;
            }));
    }

    TerrainSurfaceFieldView TerrainSurfaceAttachmentSystem::make_field_view(
        const TerrainField& field,
        const vec2          world_center)
    {
        return {
            .world_center  = world_center,
            .field_origin  = field.origin(),
            .cell_size     = field.cell_size(),
            .field_size    = field.size(),
            .field_samples = field.sample_span()
        };
    }
}
