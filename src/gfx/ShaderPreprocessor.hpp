#pragma once

#include "pch.hpp"

namespace game::gfx
{
    class ShaderPreprocessor final
    {
    public:
        static Result<std::string> preprocess_file(const fs::path& path);

    private:
        static Result<std::string> preprocess_file(const fs::path& path, std::vector<fs::path>& include_stack);
        static Result<fs::path>    parse_include_path(const std::string& line);
        static Result<std::string> read_text_file(const fs::path& path);
        static std::string         trim_left(std::string_view value);
    };
}
