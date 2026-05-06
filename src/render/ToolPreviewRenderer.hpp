#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "tools/WaterTool.hpp"
#include "water/WaterRenderable.hpp"

namespace game::render
{
    class ToolPreviewRenderer final
    {
      public:
        ToolPreviewRenderer() = default;
        ~ToolPreviewRenderer() = default;

        ToolPreviewRenderer(const ToolPreviewRenderer&) = delete;
        ToolPreviewRenderer& operator=(const ToolPreviewRenderer&) = delete;
        ToolPreviewRenderer(ToolPreviewRenderer&&) noexcept = default;
        ToolPreviewRenderer& operator=(ToolPreviewRenderer&&) noexcept = default;

        Result<void> initialize_assets() const;
        void destroy_graphics_resources();
        void draw_water_preview(const std::optional<tools::WaterTool::PreviewState>& preview_state, const sf::View& view) const;

      private:
        mutable std::optional<gfx::Mesh> current_mesh_{std::nullopt};
        mutable std::optional<gfx::Mesh> future_mesh_{std::nullopt};
        mutable std::optional<water::WaterRenderable> preview_renderable_{std::nullopt};
        mutable std::uint64_t preview_revision_{std::numeric_limits<std::uint64_t>::max()};
    };
}
