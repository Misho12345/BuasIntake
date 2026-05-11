#pragma once

#include "pch.hpp"

namespace game::gfx
{
    class Mesh final
    {
    public:
        Mesh();
        ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;
        Mesh& operator=(Mesh&& other) noexcept;

        void set_data(std::span<const sf::Vertex> vertices, std::span<const std::uint32_t> indices);
        void draw() const;

        bool empty() const;

    private:
        GLuint vao_{ 0 };
        GLuint vbo_{ 0 };
        GLuint ebo_{ 0 };
        GLsizei index_count_{ 0 };
    };
}
