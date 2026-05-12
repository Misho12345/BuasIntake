#include "pch.hpp"

#include "render/WorldRenderer.hpp"

namespace game::render
{
    Result<void> WorldRenderer::initialize_assets()
    {
        TRY(resource_renderer_.initialize_assets());
        TRY(tool_preview_renderer_.initialize_assets());
        TRY(vegetation_renderer_.initialize_assets());
        return {};
    }

    void WorldRenderer::destroy_graphics_resources()
    {
        vegetation_renderer_.destroy_graphics_resources();
        tool_preview_renderer_.destroy_graphics_resources();
        resource_renderer_.destroy_graphics_resources();
    }

    void WorldRenderer::draw(
        const world::World&                                  world,
        const std::optional<tools::WaterTool::PreviewState>& preview_state,
        const sf::View&                                      view) const
    {
        if (!world.ready()) return;

        resource_renderer_.draw_ores(view, world.terrain(), world.resources());
        world.terrain().draw_gl(view);
        world.terrain().draw_water_gl(view);
        vegetation_renderer_.draw(view, world.terrain(), world.vegetation(), world.resources());
        tool_preview_renderer_.draw_water_preview(preview_state, view);
    }
}
