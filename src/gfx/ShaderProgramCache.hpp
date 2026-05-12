#pragma once

#include "pch.hpp"

#include "gfx/ShaderProgram.hpp"

namespace game::gfx
{
    class ShaderProgramCache final
    {
    public:
        static ShaderProgramCache& instance();

        Result<std::shared_ptr<ShaderProgram>> get_or_create(
            std::string key,
            const std::function<Result<GLuint>()>& build_program);

        void clear();

    private:
        ShaderProgramCache() = default;

        std::unordered_map<
            std::string,
            std::shared_ptr<ShaderProgram>
        > programs_{};
    };
}
