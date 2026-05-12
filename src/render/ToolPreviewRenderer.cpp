#include "pch.hpp"

#include "render/ToolPreviewRenderer.hpp"

#include "gfx/MeshBuilders.hpp"

namespace game::render
{
    Result<void> ToolPreviewRenderer::initialize_assets()
    {
        if (preview_renderable_.has_value() && current_mesh_.has_value() && future_mesh_.has_value()) return {};

        if (!current_mesh_.has_value()) current_mesh_.emplace();
        if (!future_mesh_.has_value()) future_mesh_.emplace();

        preview_renderable_.emplace();
        if (const auto result = preview_renderable_->initialize(); !result)
        {
            preview_renderable_.reset();
            current_mesh_.reset();
            future_mesh_.reset();
            return fail(result.error());
        }

        return {};
    }

    void ToolPreviewRenderer::destroy_graphics_resources()
    {
        current_mesh_.reset();
        future_mesh_.reset();
        preview_renderable_.reset();
        preview_revision_ = std::numeric_limits<std::uint64_t>::max();
    }

    void ToolPreviewRenderer::draw_water_preview(
        const std::optional<tools::WaterTool::PreviewState>& preview_state,
        const sf::View&                                      view)
    {
        if (!preview_state.has_value()) return;
        if (preview_state->preview == nullptr) return;

        const auto& preview = *preview_state->preview;
        if (preview.future_vertices.empty() || preview.future_indices.empty()) return;

        if (!preview_renderable_.has_value())
        {
            if (const auto result = initialize_assets(); !result)
            {
                Log::error(result.error());
                return;
            }
        }

        if (preview_revision_ != preview_state->revision)
        {
            const auto current_vertices = gfx::build_vertices(preview.current_vertices, 0xE8F8FF80_rgba);
            const auto future_vertices  = gfx::build_vertices(preview.future_vertices, 0xD6FCFF60_rgba);

            current_mesh_->set_data(current_vertices, preview.current_indices);
            future_mesh_->set_data(future_vertices, preview.future_indices);
            preview_revision_ = preview_state->revision;
        }

        const auto& preview_renderable = *preview_renderable_;

        // This pass shows only the net-new water area, so it has to manage and restore stencil state explicitly.
        const GLboolean stencil_enabled = glIsEnabled(GL_STENCIL_TEST);

        GLboolean color_mask[4]{};
        GLint     stencil_write_mask{ 0 };
        GLint     stencil_func{ 0 };
        GLint     stencil_ref{ 0 };
        GLint     stencil_value_mask{ 0 };
        GLint     stencil_fail{ 0 };
        GLint     stencil_pass_depth_fail{ 0 };
        GLint     stencil_pass_depth_pass{ 0 };

        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_write_mask);
        glGetIntegerv(GL_STENCIL_FUNC, &stencil_func);
        glGetIntegerv(GL_STENCIL_REF, &stencil_ref);
        glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencil_value_mask);
        glGetIntegerv(GL_STENCIL_FAIL, &stencil_fail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &stencil_pass_depth_fail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &stencil_pass_depth_pass);

        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glClear(GL_STENCIL_BUFFER_BIT);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        if (!current_mesh_->empty()) preview_renderable.draw(*current_mesh_, view);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0x00);
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        preview_renderable.draw(*future_mesh_, view);

        glStencilMask(static_cast<GLuint>(stencil_write_mask));
        glStencilFunc(static_cast<GLenum>(stencil_func), stencil_ref, static_cast<GLuint>(stencil_value_mask));

        glStencilOp(
            static_cast<GLenum>(stencil_fail),
            static_cast<GLenum>(stencil_pass_depth_fail),
            static_cast<GLenum>(stencil_pass_depth_pass));

        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);

        if (stencil_enabled) glEnable(GL_STENCIL_TEST);
        else glDisable(GL_STENCIL_TEST);
    }
}
