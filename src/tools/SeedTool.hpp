#pragma once

#include "pch.hpp"


#include "tools/ToolStrategy.hpp"

namespace game::tools
{
    class SeedTool final : public TerrainTool
    {
      public:
        SeedTool() = default;
        ~SeedTool() override = default;

        void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) override;
        void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver, MouseButton button) override;
    };
}
