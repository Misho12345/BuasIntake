#pragma once

#include "pch.hpp"


namespace game::gfx
{
    enum class TextureFormat
    {
        R32F,
        RG32F,
        RGBA8,
        RGBA32F,
    };

    class Texture2D final
    {
    public:
        Texture2D() = default;
        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;
        Texture2D(Texture2D&& other) noexcept;
        Texture2D& operator=(Texture2D&& other) noexcept;

        Result<void> create(uvec2 size, TextureFormat format);
        void bind_image(GLuint unit, GLenum access) const;

        GLuint native_handle() const;
        uvec2 size() const;
        bool valid() const;

        static GLenum to_gl_format(TextureFormat format);

    private:
        GLuint handle_{ 0 };
        uvec2 size_{ 0, 0 };
        TextureFormat format_{ TextureFormat::RGBA8 };
    };
}
