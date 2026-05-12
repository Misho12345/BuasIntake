#pragma once

#include "pch.hpp"


namespace game::gfx
{
    inline mat4 make_projection(const sf::View& view) { return mat4{ view.getTransform().getMatrix() }; }
}
