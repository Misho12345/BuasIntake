#include "pch.hpp"

#include "gfx/ShaderPreprocessor.hpp"

namespace game::gfx
{
    Result<std::string> ShaderPreprocessor::preprocess_file(const fs::path& path)
    {
        std::vector<fs::path> include_stack;
        return preprocess_file(path, include_stack);
    }

    Result<std::string> ShaderPreprocessor::preprocess_file(const fs::path& path, std::vector<fs::path>& include_stack)
    {
        const fs::path normalized_path = path.lexically_normal();
        if (std::ranges::find(include_stack, normalized_path) != include_stack.end())
        {
            return fail("Shader include cycle detected at '{}'", normalized_path.string());
        }

        auto source = read_text_file(normalized_path);
        if (!source) return fail(source.error());

        include_stack.push_back(normalized_path);
        std::istringstream input{ *source };
        std::ostringstream output;
        std::string        line;
        std::uint32_t      line_number = 1u;
        const fs::path     shader_root{ "assets/shaders" };

        while (std::getline(input, line))
        {
            auto include_path = parse_include_path(line);
            if (!include_path)
            {
                include_stack.pop_back();
                return fail("{} in '{}'", include_path.error().message, normalized_path.string());
            }

            if (!include_path->empty())
            {
                const fs::path resolved_path   = (shader_root / *include_path).lexically_normal();
                auto           included_source = preprocess_file(resolved_path, include_stack);
                if (!included_source)
                {
                    include_stack.pop_back();
                    return fail("{} included from '{}'", included_source.error().message, normalized_path.string());
                }

                output << "\n// begin include " << include_path->generic_string() << "\n";
                output << *included_source;
                output << "\n// end include " << include_path->generic_string() << "\n";
                output << "#line " << (line_number + 1u) << "\n";
            }
            else { output << line << '\n'; }

            ++line_number;
        }

        include_stack.pop_back();
        return output.str();
    }

    Result<fs::path> ShaderPreprocessor::parse_include_path(const std::string& line)
    {
        const auto trimmed = trim_left(line);
        if (!trimmed.starts_with("#include")) return fs::path{};

        const auto first_quote = trimmed.find('"');
        const auto last_quote  = trimmed.find_last_of('"');
        if (first_quote == std::string::npos || last_quote == first_quote)
        {
            return fail("Malformed shader include directive '{}'", line);
        }

        return fs::path{ trimmed.substr(first_quote + 1u, last_quote - first_quote - 1u) };
    }

    Result<std::string> ShaderPreprocessor::read_text_file(const fs::path& path)
    {
        const std::ifstream file{ path };
        if (!file) return fail("Failed to read shader file '{}'", path.string());

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    std::string ShaderPreprocessor::trim_left(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<std::uint8_t>(value.front()))) value.remove_prefix(1u);
        return std::string{ value };
    }
}
