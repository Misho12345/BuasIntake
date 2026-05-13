#pragma once

#include "pch.hpp"


#include "ChunkSettings.hpp"
#include "TerrainGenerator.hpp"

namespace game::terrain
{
    // turns gpu contour edges into collider-friendly loops and paths
    // the compute shader emits raw directed edges, this class stitches and simplifies them for box2d without changing the render mesh
    // I've used help of AI for properly defining and refining the algorithm, and also for some bugfixing and feedback
    class TerrainContour final
    {
    public:
        struct ExtractedContours final
        {
            std::vector<std::vector<vec2>> loops{};
            std::vector<std::vector<vec2>> open_paths{};
        };

        struct ScoredResult final
        {
            std::vector<vec2>              mesh_vertices{};
            std::vector<std::uint32_t>     mesh_indices{};
            std::vector<std::vector<vec2>> collider_loops{};
            std::vector<std::vector<vec2>> collider_paths{};
        };

        TerrainContour() = delete;

        static ExtractedContours extract_contours(
            const std::vector<vec2>&                           boundary_vertices,
            const std::vector<TerrainGenerator::BoundaryEdge>& edges);

        static std::vector<vec2> simplify_contour(
            const std::vector<vec2>& contour,
            bool                     closed,
            float                    min_segment_length,
            float                    collinear_epsilon);

        static ScoredResult score_and_filter(
            TerrainGenerator::RawPipelineResult raw,
            const ChunkSettings&                settings);
    };
}
