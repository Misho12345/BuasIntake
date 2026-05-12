#pragma once

#include "pch.hpp"


namespace game::gfx
{
    // a 2D OpenGL texture with immutable storage (glTextureStorage2D)
    // supports compute shader image binding and standard sampler use
    class Texture2D final
    {
    public:
        Texture2D() = default;
        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;
        Texture2D(Texture2D&& other) noexcept;
        Texture2D& operator=(Texture2D&& other) noexcept;

        // (re)allocates the texture; safe to call multiple times - destroys the previous handle first
        Result<void> create_rgba32f(uvec2 size);

        // binds as an image for compute shader read/write (e.g. GL_READ_ONLY, GL_WRITE_ONLY, GL_READ_WRITE)
        void bind_image(GLuint unit, GLenum access) const;

        GLuint native_handle() const;
        uvec2 size() const;
        bool valid() const;

    private:
        GLuint handle_{ 0 };
        uvec2 size_{ 0, 0 };
    };
}
