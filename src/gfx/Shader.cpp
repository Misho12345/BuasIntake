#include "pch.hpp"
#include "Shader.hpp"

namespace game::gfx
{
	namespace
	{
		std::string trim_log(std::string log)
		{
			while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r'))
			{
				log.pop_back();
			}

			return log;
		}

		std::string shader_log(const GLuint shader)
		{
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

			if (length <= 1) return {};

			std::string log(static_cast<std::size_t>(length), '\0');
			glGetShaderInfoLog(shader, length, nullptr, log.data());
			return trim_log(std::move(log));
		}

		std::string program_log(const GLuint program)
		{
			GLint length = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

			if (length <= 1) return {};

			std::string log(static_cast<std::size_t>(length), '\0');
			glGetProgramInfoLog(program, length, nullptr, log.data());
			return trim_log(std::move(log));
		}

		std::string read_text_file(const fs::path& path)
		{
			const std::ifstream file{ path };
			if (!file)
			{
				std::println(std::cerr, "Failed to read {}", path.string());
				return {};
			}

			std::ostringstream stream;
			stream << file.rdbuf();
			return stream.str();
		}
	}

	Shader::Shader(const GLuint program) : program_{ program } {}

	Shader::~Shader()
	{
		if (program_ != 0) glDeleteProgram(program_);
	}


	Shader::Shader(Shader&& other) noexcept
		: program_{ std::exchange(other.program_, 0) },
		  uniform_locations_{ std::move(other.uniform_locations_) } {}

	Shader& Shader::operator=(Shader&& other) noexcept
	{
		if (this == &other) return *this;
		if (program_ != 0) glDeleteProgram(program_);

		program_           = std::exchange(other.program_, 0);
		uniform_locations_ = std::move(other.uniform_locations_);
		return *this;
	}


	Shader Shader::from_compute_file(const fs::path& path)
	{
		const auto       source = read_text_file(path);
		const auto       stage  = compile_stage(GL_COMPUTE_SHADER, source, path);
		const std::array stages{ stage };
		const auto       program = link_program(stages, path.string());
		glDeleteShader(stage);
		return Shader{ program };
	}

	Shader Shader::from_graphics_files(const fs::path& vertex_path, const fs::path& fragment_path)
	{
		const auto vertex_source   = read_text_file(vertex_path);
		const auto fragment_source = read_text_file(fragment_path);

		const auto vertex_stage   = compile_stage(GL_VERTEX_SHADER, vertex_source, vertex_path);
		const auto fragment_stage = compile_stage(GL_FRAGMENT_SHADER, fragment_source, fragment_path);

		const std::array stages{ vertex_stage, fragment_stage };
		const auto program = link_program(stages, std::format("{} + {}", vertex_path.string(), fragment_path.string()));

		glDeleteShader(vertex_stage);
		glDeleteShader(fragment_stage);
		return Shader{ program };
	}


	void Shader::use() const
	{
		assert(program_ != 0 && "Shader program is not initialized");
		glUseProgram(program_);
	}

	GLuint Shader::id() const
	{
		return program_;
	}

	bool Shader::valid() const
	{
		return program_ != 0;
	}


	GLuint Shader::compile_stage(const GLenum stage, const std::string& source, const fs::path& path)
	{
		const GLuint shader = glCreateShader(stage);
		const char*  data   = source.c_str();
		glShaderSource(shader, 1, &data, nullptr);
		glCompileShader(shader);

		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

		if (compiled == GL_FALSE)
		{
			const auto log = shader_log(shader);
			glDeleteShader(shader);
			std::println(std::cerr,
				"Failed to compile shader '{}':\n{}",
				path.string(),
				log.empty() ? "No additional info" : log);
			return 0;
		}

		return shader;
	}

	GLuint Shader::link_program(const std::span<const GLuint> shaders, const std::string_view label)
	{
		const GLuint program = glCreateProgram();

		for (const auto shader : shaders)
		{
			glAttachShader(program, shader);
		}

		glLinkProgram(program);

		for (const auto shader : shaders)
		{
			glDetachShader(program, shader);
		}

		GLint linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);

		if (linked == GL_FALSE)
		{
			const auto log = program_log(program);
			glDeleteProgram(program);
			std::println(std::cerr,
				"Failed to link shader program '{}':\n{}",
				label,
				log.empty() ? "No additional info" : log);
			return 0;
		}

		return program;
	}

	GLint Shader::uniform_location(const std::string_view name) const
	{
		assert(program_ != 0 && "Shader program is not initialized");

		const std::string key{ name };
		if (const auto it = uniform_locations_.find(key); it != uniform_locations_.end())
		{
			return it->second;
		}

		const auto location = glGetUniformLocation(program_, key.c_str());
		uniform_locations_.emplace(key, location);
		return location;
	}
}
