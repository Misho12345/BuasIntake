#pragma once

#include "pch.hpp"


#include "Shader.hpp"

namespace game::gfx
{
    // helper for running a sequence of compute shader passes with per-pass barriers
    // terrain generation uses this to express gpu work as ordered passes instead of scattering glDispatchCompute calls everywhere
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
            // lets each caller bind buffers, images, and uniforms right before the matching shader runs
            std::function<void(const Shader&)> configure{};
            GLbitfield barrier_after{ 0 };
        };

        static DispatchSize groups_for(const uvec2 extent, const GLuint local_size_x, const GLuint local_size_y)
        {
            if (local_size_x == 0 || local_size_y == 0)
            {
                Log::error("ComputeDispatcher received zero local group size");
                return {};
            }

            // calculate the number of work groups needed to cover the extent
            return {
                .x = ceil_div(extent.x, local_size_x),
                .y = ceil_div(extent.y, local_size_y),
                .z = 1
            };
        }

        static Result<void> run(const std::span<const Pass> passes)
        {
            for (const auto& [shader, groups, configure, barrier_after] : passes)
            {
                if (shader == nullptr)
                {
                    glUseProgram(0);
                    return fail("Compute pass requires a shader");
                }

                if (auto use_result = shader->use(); !use_result)
                {
                    glUseProgram(0);
                    return fail(use_result.error());
                }

                // configure pass-specific uniforms and resources before dispatch
                if (configure) configure(*shader);

                // run the compute workload and apply the optional memory barrier for chained passes
                glDispatchCompute(groups.x, groups.y, groups.z);
                if (barrier_after != 0) glMemoryBarrier(barrier_after);
            }

            glUseProgram(0);
            return {};
        }

    private:
        static GLuint ceil_div(const GLuint value, const GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    };
}
