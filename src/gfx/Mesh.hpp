#pragma once

#include "pch.hpp"

#include "gfx/GlHandle.hpp"

namespace game::gfx
{
    // owns the opengl buffers for an indexed sf::Vertex mesh
    class Mesh final
    {
    public:
        Mesh();
        ~Mesh() = default;

        Mesh(const Mesh&)            = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;
        Mesh& operator=(Mesh&& other) noexcept;

        void set_data(std::span<const sf::Vertex> vertices, std::span<const std::uint32_t> indices);
        void draw() const;

        bool empty() const;

    private:
        GlVertexArray vao_{};
        GlBuffer      vbo_{};
        GlBuffer      ebo_{};
        GLsizei index_count_{ 0 };
    };
}
