#pragma once

#include "pch.hpp"

#include "ChunkSettings.hpp"
#include "TerrainGenerator.hpp"

namespace game::terrain
{
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
			std::vector<vec2> mesh_vertices{};
			std::vector<std::uint32_t> mesh_indices{};
			std::vector<std::vector<vec2>> collider_loops{};
			std::vector<std::vector<vec2>> collider_paths{};
			std::vector<vec2> primary_contour{};
			float contour_score{ 0.0f };
		};

		TerrainContour() = delete;

		[[nodiscard]] static ExtractedContours extract_contours(const std::vector<vec2>& boundary_vertices, const std::vector<TerrainGenerator::BoundaryEdge>& edges);
		[[nodiscard]] static std::vector<vec2> simplify_contour(const std::vector<vec2>& contour, bool closed, float min_segment_length, float collinear_epsilon);
		[[nodiscard]] static ScoredResult score_and_filter(TerrainGenerator::RawPipelineResult raw, const ChunkSettings& settings);
	};
}
