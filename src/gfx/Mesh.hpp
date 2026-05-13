#pragma once

#include "pch.hpp"

#include "gfx/GlHandle.hpp"

namespace game::gfx
{
    // owns the opengl buffers for an indexed sf::Vertex mesh
    // render systems build plain sf::Vertex and index arrays, then Mesh is the small bridge that uploads them to gpu buffers
    // this keeps the rest of the code from needing to know about vao/vbo/ebo lifetime or vertex attribute setup
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
