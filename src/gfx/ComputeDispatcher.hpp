#pragma once

#include "pch.hpp"
#include "Shader.hpp"

namespace game::gfx
{
	class ComputeDispatcher final
	{
	public:
		struct DispatchSize final
		{
			GLuint x{ 1 };
			GLuint y{ 1 };
			GLuint z{ 1 };
		};

		struct Pass final
		{
			const Shader* shader{ nullptr };
			DispatchSize groups{};
			std::function<void(const Shader&)> configure{};
			GLbitfield barrier_after{ 0 };
		};

		[[nodiscard]] 
		static DispatchSize groups_for(const uvec2 extent, const GLuint local_size_x, const GLuint local_size_y)
		{
			return {
				.x = ceil_div(extent.x, local_size_x),
				.y = ceil_div(extent.y, local_size_y),
				.z = 1
			};
		}

		static void run(const std::span<const Pass> passes)
		{
			for (const auto& [shader, groups, configure, barrier_after] : passes)
			{
				assert(shader && "Compute pass requires a shader");
				shader->use();
				if (configure) configure(*shader);
				glDispatchCompute(groups.x, groups.y, groups.z);
				if (barrier_after != 0) glMemoryBarrier(barrier_after);
			}

			glUseProgram(0);
		}

	private:
		[[nodiscard]]
		static GLuint ceil_div(const GLuint value, const GLuint divisor)
		{
			assert(divisor != 0 && "Divisor must be non-zero");
			return (value + divisor - 1) / divisor;
		}
	};
}
