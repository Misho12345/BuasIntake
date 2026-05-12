#pragma once

#include "pch.hpp"

#include "resources/ResourceSystem.hpp"
#include "terrain/TerrainField.hpp"
#include "terrain/TerrainSurfaceSampler.hpp"

namespace game::terrain
{
    class TerrainSurfaceAttachmentSystem final
    {
    public:
        using RefreshVegetation = std::function<void(const std::unordered_set<std::uint64_t>&)>;

        std::optional<TerrainSurfaceAttachment> exposed_surface_attachment(
            const TerrainField& field,
            vec2                world_center,
            ivec2               coord) const;

        std::optional<vec2> surface_anchor_world(
            const TerrainField& field,
            vec2                world_center,
            ivec2               coord) const;

        void refresh_around(
            const TerrainField&       field,
            vec2                      world_center,
            const std::vector<ivec2>& changed_coords,
            resources::ResourceSystem* resources,
            const RefreshVegetation&  refresh_vegetation) const;

    private:
        static TerrainSurfaceFieldView make_field_view(const TerrainField& field, vec2 world_center);
    };
}
