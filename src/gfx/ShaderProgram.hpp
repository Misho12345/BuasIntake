#pragma once

#include "pch.hpp"

namespace game::gfx
{
    struct ShaderProgram final
    {
        GLuint id{ 0 };
        mutable std::unordered_map<std::string, GLint> uniform_locations{};

        ~ShaderProgram()
        {
            if (id != 0) glDeleteProgram(id);
        }
    };
}
