#pragma once

#include "pch.hpp"


namespace game::terrain
{
    class PlanetTerrain;
}

namespace game::water
{
    enum class WaterActionStatus : std::uint8_t
    {
        Applied,
        NoValidTarget,
        EmptyAmount,
        NoChange
    };

    struct WaterActionResult final
    {
        WaterActionStatus status{ WaterActionStatus::NoChange };
        std::uint32_t     changed_units{ 0u };
    };

    struct WaterPreviewMesh final
    {
        std::vector<vec2>          current_vertices{};
        std::vector<std::uint32_t> current_indices{};
        std::vector<vec2>          future_vertices{};
        std::vector<std::uint32_t> future_indices{};
    };

    class WaterSystem final
    {
    public:
        // place and pickup are the small public API around the much uglier planning code in water interaction and planet terrain
        Result<WaterActionResult> place_water(
            terrain::PlanetTerrain& terrain,
            vec2                    world_position,
            std::uint32_t           volume_cap = 25u) const;

        Result<WaterActionResult> pickup_water(
            terrain::PlanetTerrain& terrain,
            vec2                    world_position,
            std::uint32_t           volume_cap = 25u) const;

        // preview builds two meshes current and future so the renderer can show only what would change before we commit it
        Result<std::optional<WaterPreviewMesh>> build_preview(
            const terrain::PlanetTerrain& terrain,
            vec2                          world_position,
            std::uint32_t                 volume_cap) const;

    };
}
