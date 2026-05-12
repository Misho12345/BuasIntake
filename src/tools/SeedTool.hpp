#pragma once

#include "pch.hpp"


#include "tools/TerrainTool.hpp"

namespace game::tools
{
    // small tool that forwards planting clicks into the vegetation system
    class SeedTool final : public TerrainTool
    {
    public:
        SeedTool()           = default;
        ~SeedTool() override = default;

        void handle_mouse_pressed(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            MouseButton                  button) override;
    };
}
