#pragma once

#include "pch.hpp"


namespace game::gfx
{
    // RAII wrappers for OpenGL object handles
    // these exist to safely manage lifetime without inheriting the full complexity of Mesh, SSBO, etc.

    class GlBuffer final
    {
    public:
        GlBuffer() = default;
        ~GlBuffer() { reset(); }

        GlBuffer(const GlBuffer&)            = delete;
        GlBuffer& operator=(const GlBuffer&) = delete;

        GlBuffer(GlBuffer&& other) noexcept : id_{ std::exchange(other.id_, 0u) } {}

        GlBuffer& operator=(GlBuffer&& other) noexcept
        {
            if (this == &other) return *this;
            reset();
            id_ = std::exchange(other.id_, 0u);
            return *this;
        }

        void create()
        {
            reset();
            glCreateBuffers(1, &id_);
        }

        void reset()
        {
            if (id_ != 0) glDeleteBuffers(1, &id_);
            id_ = 0;
        }

        GLuint id() const { return id_; }
        bool   valid() const { return id_ != 0; }

    private:
        GLuint id_{ 0 };
    };

    class GlVertexArray final
    {
    public:
        GlVertexArray() = default;
        ~GlVertexArray() { reset(); }

        GlVertexArray(const GlVertexArray&)            = delete;
        GlVertexArray& operator=(const GlVertexArray&) = delete;
        GlVertexArray(GlVertexArray&& other) noexcept : id_{ std::exchange(other.id_, 0u) } {}

        GlVertexArray& operator=(GlVertexArray&& other) noexcept
        {
            if (this == &other) return *this;
            reset();
            id_ = std::exchange(other.id_, 0u);
            return *this;
        }

        void create()
        {
            reset();
            glCreateVertexArrays(1, &id_);
        }

        void reset()
        {
            if (id_ != 0) glDeleteVertexArrays(1, &id_);
            id_ = 0;
        }

        GLuint id() const { return id_; }
        bool   valid() const { return id_ != 0; }

    private:
        GLuint id_{ 0 };
    };

    class GlTexture final
    {
    public:
        GlTexture() = default;
        ~GlTexture() { reset(); }

        GlTexture(const GlTexture&)            = delete;
        GlTexture& operator=(const GlTexture&) = delete;

        GlTexture(GlTexture&& other) noexcept : id_{ std::exchange(other.id_, 0u) } {}

        GlTexture& operator=(GlTexture&& other) noexcept
        {
            if (this == &other) return *this;
            reset();
            id_ = std::exchange(other.id_, 0u);
            return *this;
        }

        void create_2d_array()
        {
            reset();
            glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &id_);
        }

        void reset()
        {
            if (id_ != 0) glDeleteTextures(1, &id_);
            id_ = 0;
        }

        GLuint id() const { return id_; }
        bool   valid() const { return id_ != 0; }

    private:
        GLuint id_{ 0 };
    };
}
