#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "gfx/SSBO.hpp"
#include "gfx/Shader.hpp"

namespace game::terrain
{
	struct ChunkSettings final
	{
		uvec2 field_size{ 80, 60 };
		vec2 world_origin{ -12.0f, -8.5f };
		vec2 world_size{ 23.75f, 17.75f };
	};

	class TerrainChunk final
	{
	public:
		explicit TerrainChunk(b2WorldId world_id, ChunkSettings settings = {});
		~TerrainChunk();

		TerrainChunk(const TerrainChunk&) = delete;
		TerrainChunk& operator=(const TerrainChunk&) = delete;
		TerrainChunk(TerrainChunk&&) noexcept = delete;
		TerrainChunk& operator=(TerrainChunk&&) noexcept = delete;

		void render_debug(sf::RenderTarget& target) const;

		[[nodiscard]] const gfx::Mesh& mesh() const;
		[[nodiscard]] vec2 player_spawn() const;
		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;

	private:
		struct BoundaryEdge final
		{
			std::uint32_t a{};
			std::uint32_t b{};
		};

		struct Candidate final
		{
			vec2 noise_min{};
			vec2 noise_max{};
			float time{};
			float iso{};
		};

		struct ExtractedContours final
		{
			std::vector<std::vector<vec2>> loops{};
			std::vector<std::vector<vec2>> open_paths{};
		};

		struct PipelineResult final
		{
			std::vector<vec2> mesh_vertices{};
			std::vector<std::uint32_t> mesh_indices{};
			std::vector<std::vector<vec2>> loops{};
			std::vector<std::vector<vec2>> open_paths{};
			std::vector<std::vector<vec2>> collider_loops{};
			std::vector<std::vector<vec2>> collider_paths{};
			std::vector<vec2> primary_contour{};
			float contour_score{ 0.0f };
		};

		[[nodiscard]] PipelineResult generate_best_chunk();
		[[nodiscard]] PipelineResult run_candidate(const Candidate& candidate);
		[[nodiscard]] ExtractedContours extract_contours(const std::vector<vec2>& boundary_vertices, const std::vector<BoundaryEdge>& edges) const;
		[[nodiscard]] std::vector<vec2> simplify_contour(const std::vector<vec2>& contour, bool closed, float min_segment_length, float collinear_epsilon) const;
		[[nodiscard]] vec2 calculate_spawn(const std::vector<vec2>& contour) const;
		[[nodiscard]] vec2 cell_size() const;

		void reset_buffers();
		void build_chunk(const PipelineResult& result);
		void build_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices);
		void build_debug_lines(const std::vector<std::vector<vec2>>& loops, const std::vector<std::vector<vec2>>& open_paths, const std::vector<std::vector<vec2>>& collider_loops, const std::vector<std::vector<vec2>>& collider_paths);
		void create_colliders(const std::vector<std::vector<vec2>>& collider_loops, const std::vector<std::vector<vec2>>& collider_paths);

		ChunkSettings settings_{};

		b2WorldId world_id_{ b2_nullWorldId };
		b2BodyId terrain_body_{ b2_nullBodyId };

		GLuint field_texture_handle_{ 0 };

		gfx::Shader terrain_shader_{};
		gfx::Shader edge_shader_{};
		gfx::Shader mesh_shader_{};

		gfx::SSBO boundary_vertices_buffer_{};
		gfx::SSBO horizontal_edge_ids_buffer_{};
		gfx::SSBO vertical_edge_ids_buffer_{};
		gfx::SSBO boundary_vertex_counter_buffer_{};
		gfx::SSBO mesh_vertices_buffer_{};
		gfx::SSBO mesh_indices_buffer_{};
		gfx::SSBO boundary_edges_buffer_{};
		gfx::SSBO counters_buffer_{};

		gfx::Mesh mesh_{};
		std::vector<sf::VertexArray> edge_debug_lines_{};
		std::vector<sf::VertexArray> collider_debug_lines_{};

		vec2 chunk_min_{};
		vec2 chunk_max_{};
		vec2 display_min_{};
		vec2 display_max_{};
		vec2 player_spawn_{};
	};
}
