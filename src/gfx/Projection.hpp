#pragma once

#include "pch.hpp"


namespace game::gfx
{
    // helper function for easier access of the projection matrix
    inline mat4 make_projection(const sf::View& view) { return mat4{ view.getTransform().getMatrix() }; }
}
