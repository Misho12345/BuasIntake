#pragma once

#include "pch.hpp"

#include "render/ResourceRenderer.hpp"
#include "render/ToolPreviewRenderer.hpp"
#include "render/VegetationRenderer.hpp"
#include "world/World.hpp"

namespace game::render
{
    // draws the world systems that are not already rendered by PlanetTerrain itself
    // this keeps resource sprites vegetation sprites and tool previews on the same render path
    class WorldRenderer final
    {
    public:
        Result<void> initialize_assets();
        void         destroy_graphics_resources();

        void draw(
            const world::World&                                  world,
            const std::optional<tools::WaterTool::PreviewState>& preview_state,
            const sf::View&                                      view);

    private:
        ResourceRenderer    resource_renderer_{};
        ToolPreviewRenderer tool_preview_renderer_{};
        VegetationRenderer  vegetation_renderer_{};
    };
}
