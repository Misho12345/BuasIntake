#pragma once

#include "pch.hpp"


namespace game::gfx
{
    class ScopedAlphaBlendPass final
    {
    public:
        ScopedAlphaBlendPass()
        {
            blend_enabled_ = glIsEnabled(GL_BLEND);
            depth_test_enabled_ = glIsEnabled(GL_DEPTH_TEST);
            cull_face_enabled_ = glIsEnabled(GL_CULL_FACE);
            glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb_);
            glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb_);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha_);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha_);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        ~ScopedAlphaBlendPass()
        {
            if (blend_enabled_) glEnable(GL_BLEND);
            else glDisable(GL_BLEND);

            if (depth_test_enabled_) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);

            if (cull_face_enabled_) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);

            glBlendFuncSeparate(blend_src_rgb_, blend_dst_rgb_, blend_src_alpha_, blend_dst_alpha_);
            glUseProgram(0);
        }

        ScopedAlphaBlendPass(const ScopedAlphaBlendPass&) = delete;
        ScopedAlphaBlendPass& operator=(const ScopedAlphaBlendPass&) = delete;
        ScopedAlphaBlendPass(ScopedAlphaBlendPass&&) = delete;
        ScopedAlphaBlendPass& operator=(ScopedAlphaBlendPass&&) = delete;

    private:
        GLboolean blend_enabled_{ GL_FALSE };
        GLboolean depth_test_enabled_{ GL_FALSE };
        GLboolean cull_face_enabled_{ GL_FALSE };

        GLint blend_src_rgb_{ GL_ONE };
        GLint blend_dst_rgb_{ GL_ZERO };
        GLint blend_src_alpha_{ GL_ONE };
        GLint blend_dst_alpha_{ GL_ZERO };
    };
}
