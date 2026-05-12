#pragma once

#include "pch.hpp"


namespace game::gfx
{
    inline std::vector<sf::Vertex> build_vertices(
        const std::span<const vec2> positions,
        const sf::Color             color,
        const vec2                  tex_coords = { 0.0f, 0.0f })
    {
        std::vector<sf::Vertex> vertices;
        vertices.reserve(positions.size());

        for (const auto& position : positions)
        {
            vertices.emplace_back(position, color, tex_coords);
        }

        return vertices;
    }
}
