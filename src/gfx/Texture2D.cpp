#include "pch.hpp"

#include "Texture2D.hpp"

namespace game::gfx
{
    Result<void> Texture2D::create_rgba32f(const uvec2 size)
    {
        if (size.x == 0 || size.y == 0) return fail("Texture size must be non-zero");

        // destroy previous handle if recreating
        if (handle_ != 0)
        {
            glDeleteTextures(1, &handle_);
            handle_ = 0;
        }

        size_ = size;

        glCreateTextures(GL_TEXTURE_2D, 1, &handle_);
        // glTextureStorage2D allocates immutable storage - size and format cannot change after this
        glTextureStorage2D(handle_, 1, GL_RGBA32F, static_cast<GLsizei>(size_.x), static_cast<GLsizei>(size_.y));

        glTextureParameteri(handle_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(handle_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(handle_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(handle_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return {};
    }

    Texture2D::~Texture2D()
    {
        if (handle_ != 0) glDeleteTextures(1, &handle_);
    }

    Texture2D::Texture2D(Texture2D&& other) noexcept
        : handle_{ std::exchange(other.handle_, 0) },
          size_{ std::exchange(other.size_, uvec2{ 0, 0 }) } {}

    Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
    {
        if (this == &other) return *this;

        if (handle_ != 0) glDeleteTextures(1, &handle_);

        handle_ = std::exchange(other.handle_, 0);
        size_   = std::exchange(other.size_, uvec2{ 0, 0 });
        return *this;
    }

    void Texture2D::bind_image(const GLuint unit, const GLenum access) const
    {
        if (handle_ == 0) return;
        glBindImageTexture(unit, handle_, 0, GL_FALSE, 0, access, GL_RGBA32F);
    }

    GLuint Texture2D::native_handle() const { return handle_; }

    uvec2 Texture2D::size() const { return size_; }
    bool  Texture2D::valid() const { return handle_ != 0; }
}
