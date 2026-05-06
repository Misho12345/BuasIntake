#pragma once

#include "pch.hpp"

#include "render/ResourceRenderer.hpp"
#include "render/ToolPreviewRenderer.hpp"
#include "render/VegetationRenderer.hpp"
#include "world/World.hpp"

namespace game::render
{
    class WorldRenderer final
    {
      public:
        Result<void> initialize_assets();
        void destroy_graphics_resources();
        void draw(const world::World& world,
                  const std::optional<tools::WaterTool::PreviewState>& preview_state,
                  const sf::View& view) const;

      private:
        mutable ResourceRenderer resource_renderer_{};
        mutable ToolPreviewRenderer tool_preview_renderer_{};
        mutable VegetationRenderer vegetation_renderer_{};
    };
}
