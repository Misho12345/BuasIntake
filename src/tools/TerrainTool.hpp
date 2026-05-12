#pragma once

#include "pch.hpp"

namespace game::terrain { class PlanetTerrain; }
namespace game::resources { class ResourceSystem; }
namespace game::water { class WaterSystem; }
namespace game::platform { class InputSystem; }

namespace game::tools
{
    struct TerrainToolContext final
    {
        b2WorldId                    world{ b2_nullWorldId };

        terrain::PlanetTerrain*      terrain{ nullptr };
        resources::ResourceSystem*   resources{ nullptr };
        water::WaterSystem*          water{ nullptr };
        const platform::InputSystem* input{ nullptr };

        b2BodyId                     player_body{ b2_nullBodyId };
        vec2                         player_world_position{ 0.0f, 0.0f };
        vec2                         mouse_world_position{ 0.0f, 0.0f };
    };

    class TerrainTargetResolver;

    class TerrainTool
    {
    public:
        virtual ~TerrainTool() = default;

        virtual void activate() {}
        virtual void deactivate() {}

        virtual void update(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            float                        dt) = 0;

        virtual void handle_mouse_pressed(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            MouseButton                  button) = 0;
    };
}
