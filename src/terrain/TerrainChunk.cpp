#include "pch.hpp"
#include "TerrainChunk.hpp"

#include "gfx/ComputeDispatcher.hpp"

namespace game::terrain
{
	namespace
	{
		struct GpuVec2 final
		{
			float x{};
			float y{};
		};

		struct GpuBoundaryEdge final
		{
			std::uint32_t a{};
			std::uint32_t b{};
		};

		struct GpuCounters final
		{
			std::uint32_t vertex_count{};
			std::uint32_t index_count{};
			std::uint32_t edge_count{};
		};

		constexpr GLuint image_binding = 0;
		constexpr GLuint boundary_vertices_binding = 1;
		constexpr GLuint horizontal_edge_ids_binding = 2;
		constexpr GLuint vertical_edge_ids_binding = 3;
		constexpr GLuint boundary_vertex_counter_binding = 4;
		constexpr GLuint mesh_vertices_binding = 4;
		constexpr GLuint mesh_indices_binding = 5;
		constexpr GLuint boundary_edges_binding = 6;
		constexpr GLuint counters_binding = 7;

		[[nodiscard]] vec2 to_vec2(const GpuVec2& value)
		{
			return { value.x, value.y };
		}

		[[nodiscard]] b2Vec2 to_b2(const vec2& value)
		{
			return { value.x, value.y };
		}

		[[nodiscard]] vec2 scale_vec2(const vec2& value, const float scalar)
		{
			return { value.x * scalar, value.y * scalar };
		}

		[[nodiscard]] float dot(const vec2& a, const vec2& b)
		{
			return a.x * b.x + a.y * b.y;
		}

		[[nodiscard]] float length_squared(const vec2& value)
		{
			return dot(value, value);
		}

		[[nodiscard]] float distance(const vec2& a, const vec2& b)
		{
			return std::sqrt(length_squared({ a.x - b.x, a.y - b.y }));
		}

		[[nodiscard]] float distance_to_segment(const vec2& point, const vec2& a, const vec2& b)
		{
			const vec2 ab{ b.x - a.x, b.y - a.y };
			const float ab_length_sq = length_squared(ab);

			if (ab_length_sq <= std::numeric_limits<float>::epsilon()) return distance(point, a);

			const vec2 ap{ point.x - a.x, point.y - a.y };
			const float t = std::clamp(dot(ap, ab) / ab_length_sq, 0.0f, 1.0f);
			const vec2 closest{ a.x + ab.x * t, a.y + ab.y * t };
			return distance(point, closest);
		}

		[[nodiscard]] std::array<float, 4> to_gl_color(const sf::Color& color)
		{
			return {
				static_cast<float>(color.r) / 255.0f,
				static_cast<float>(color.g) / 255.0f,
				static_cast<float>(color.b) / 255.0f,
				static_cast<float>(color.a) / 255.0f,
			};
		}

		[[nodiscard]] float clamp01(const float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		[[nodiscard]] sf::Color lerp_color(const sf::Color& a, const sf::Color& b, const float t)
		{
			const auto blend = clamp01(t);
			auto channel = [blend](const std::uint8_t lhs, const std::uint8_t rhs)
			{
				return static_cast<std::uint8_t>(std::lround(std::lerp(static_cast<float>(lhs), static_cast<float>(rhs), blend)));
			};

			return {
				channel(a.r, b.r),
				channel(a.g, b.g),
				channel(a.b, b.b),
				channel(a.a, b.a)
			};
		}

		[[nodiscard]] float signed_area(const std::span<const vec2> points)
		{
			if (points.size() < 3) return 0.0f;

			float area = 0.0f;
			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const auto& a = points[i];
				const auto& b = points[(i + 1) % points.size()];
				area += a.x * b.y - b.x * a.y;
			}

			return area * 0.5f;
		}

		[[nodiscard]] float polyline_length(const std::span<const vec2> points)
		{
			if (points.size() < 2) return 0.0f;

			float total = 0.0f;
			for (std::size_t i = 1; i < points.size(); ++i)
			{
				total += distance(points[i - 1], points[i]);
			}

			return total;
		}

		[[nodiscard]] std::uint32_t chunk_cell_count(const ChunkSettings& settings)
		{
			return (settings.field_size.x - 1u) * (settings.field_size.y - 1u);
		}

		[[nodiscard]] std::uint32_t max_boundary_vertex_count(const ChunkSettings& settings)
		{
			const auto width = settings.field_size.x;
			const auto height = settings.field_size.y;
			return height * (width - 1u) + width * (height - 1u);
		}
	}

	TerrainChunk::TerrainChunk(const b2WorldId world_id, ChunkSettings settings) : settings_{ std::move(settings) }, world_id_{ world_id }
	{
		terrain_shader_ = gfx::Shader::from_compute_file("assets/shaders/terrain_gen.comp");
		edge_shader_ = gfx::Shader::from_compute_file("assets/shaders/chunk_edges_gen.comp");
		mesh_shader_ = gfx::Shader::from_compute_file("assets/shaders/chunk_mesh_gen.comp");

		glCreateTextures(GL_TEXTURE_2D, 1, &field_texture_handle_);
		glTextureStorage2D(
			field_texture_handle_,
			1,
			GL_RGBA32F,
			static_cast<GLsizei>(settings_.field_size.x),
			static_cast<GLsizei>(settings_.field_size.y));

		glTextureParameteri(field_texture_handle_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(field_texture_handle_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(field_texture_handle_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(field_texture_handle_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		boundary_vertices_buffer_.resize<GpuVec2>(max_boundary_vertex_count(settings_));
		horizontal_edge_ids_buffer_.resize<std::int32_t>(settings_.field_size.y * (settings_.field_size.x - 1u));
		vertical_edge_ids_buffer_.resize<std::int32_t>(settings_.field_size.y * settings_.field_size.x);
		boundary_vertex_counter_buffer_.resize<std::uint32_t>(1);
		mesh_vertices_buffer_.resize<GpuVec2>(chunk_cell_count(settings_) * 6u);
		mesh_indices_buffer_.resize<std::uint32_t>(chunk_cell_count(settings_) * 6u);
		boundary_edges_buffer_.resize<GpuBoundaryEdge>(chunk_cell_count(settings_) * 2u);
		counters_buffer_.resize<GpuCounters>(1);

		chunk_min_ = settings_.world_origin;
		chunk_max_ = {
			settings_.world_origin.x + settings_.world_size.x,
			settings_.world_origin.y + settings_.world_size.y
		};

		const auto half_cell = scale_vec2(cell_size(), 0.5f);
		display_min_ = { chunk_min_.x - half_cell.x, chunk_min_.y - half_cell.y };
		display_max_ = { chunk_max_.x + half_cell.x, chunk_max_.y + half_cell.y };

		build_chunk(generate_best_chunk());
	}

	TerrainChunk::~TerrainChunk()
	{
		if (b2Body_IsValid(terrain_body_))
		{
			b2DestroyBody(terrain_body_);
		}

		if (field_texture_handle_ != 0)
		{
			glDeleteTextures(1, &field_texture_handle_);
		}
	}

	void TerrainChunk::render_debug(sf::RenderTarget& target) const
	{
		for (const auto& outline : edge_debug_lines_)
		{
			target.draw(outline);
		}

		for (const auto& outline : collider_debug_lines_)
		{
			target.draw(outline);
		}
	}

	const gfx::Mesh& TerrainChunk::mesh() const
	{
		return mesh_;
	}

	vec2 TerrainChunk::player_spawn() const
	{
		return player_spawn_;
	}

	vec2 TerrainChunk::display_min() const
	{
		return display_min_;
	}

	vec2 TerrainChunk::display_max() const
	{
		return display_max_;
	}

	TerrainChunk::PipelineResult TerrainChunk::generate_best_chunk()
	{
		std::vector<Candidate> candidates;
		candidates.reserve(8);

		for (std::size_t i = 0; i < 8; ++i)
		{
			const auto fi = static_cast<float>(i);
			const vec2 noise_min{ 2.5f + fi * 4.37f, 1.35f + fi * 2.91f };
			const vec2 span{ 1.20f + 0.12f * static_cast<float>(i % 2), 0.95f + 0.08f * static_cast<float>((i + 1) % 3) };

			candidates.push_back({
				.noise_min = noise_min,
				.noise_max = { noise_min.x + span.x, noise_min.y + span.y },
				.time = fi * 6.0f,
				.iso = 0.50f + 0.02f * static_cast<float>(i % 3)
			});
		}

		std::optional<PipelineResult> best_result;
		float best_score = -std::numeric_limits<float>::infinity();
		const float chunk_area = (chunk_max_.x - chunk_min_.x) * (chunk_max_.y - chunk_min_.y);

		for (const auto& candidate : candidates)
		{
			auto result = run_candidate(candidate);
			if (result.primary_contour.empty()) continue;

			const float coverage = result.contour_score / std::max(chunk_area, 0.001f);
			float score = result.contour_score;
			score -= std::abs(coverage - 0.35f) * chunk_area * 0.5f;
			score -= static_cast<float>(std::max<std::size_t>(result.collider_loops.size() + result.collider_paths.size(), 1u) - 1u) * 1.0f;

			if (coverage < 0.04f) score -= chunk_area;

			if (score > best_score)
			{
				best_score = score;
				best_result = std::move(result);
			}
		}

		if (!best_result)
		{
			throw std::runtime_error("Failed to generate a usable terrain chunk from the compute shaders");
		}

		return *best_result;
	}

	TerrainChunk::PipelineResult TerrainChunk::run_candidate(const Candidate& candidate)
	{
		reset_buffers();

		const auto groups = gfx::ComputeDispatcher::groups_for(settings_.field_size, 16, 16);
		const auto terrain_cell_size = cell_size();

		const std::array passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_shader_,
				.groups = groups,
				.configure = [this, &candidate](const gfx::Shader& shader)
				{
					glBindImageTexture(image_binding, field_texture_handle_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
					shader.set_uniform("uTime", candidate.time);
					shader.set_uniform("uWorldMin", candidate.noise_min);
					shader.set_uniform("uWorldMax", candidate.noise_max);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &edge_shader_,
				.groups = groups,
				.configure = [this, &candidate, terrain_cell_size](const gfx::Shader& shader)
				{
					glBindImageTexture(image_binding, field_texture_handle_, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
					boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
					horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
					vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
					boundary_vertex_counter_buffer_.bind_base(boundary_vertex_counter_binding);
					shader.set_uniform("uIso", candidate.iso);
					shader.set_uniform("uOrigin", settings_.world_origin);
					shader.set_uniform("uCellSize", terrain_cell_size);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &mesh_shader_,
				.groups = groups,
				.configure = [this, &candidate, terrain_cell_size](const gfx::Shader& shader)
				{
					glBindImageTexture(image_binding, field_texture_handle_, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
					boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
					horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
					vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
					mesh_vertices_buffer_.bind_base(mesh_vertices_binding);
					mesh_indices_buffer_.bind_base(mesh_indices_binding);
					boundary_edges_buffer_.bind_base(boundary_edges_binding);
					counters_buffer_.bind_base(counters_binding);
					shader.set_uniform("uIso", candidate.iso);
					shader.set_uniform("uOrigin", settings_.world_origin);
					shader.set_uniform("uCellSize", terrain_cell_size);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		gfx::ComputeDispatcher::run(passes);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		PipelineResult result{};

		const auto boundary_vertex_count = boundary_vertex_counter_buffer_.read_one<std::uint32_t>();
		const auto counters = counters_buffer_.read_one<GpuCounters>();

		if (boundary_vertex_count == 0 || counters.vertex_count == 0 || counters.index_count == 0 || counters.edge_count == 0)
		{
			return result;
		}

		const auto gpu_boundary_vertices = boundary_vertices_buffer_.read<GpuVec2>(boundary_vertex_count);
		const auto gpu_mesh_vertices = mesh_vertices_buffer_.read<GpuVec2>(counters.vertex_count);
		const auto mesh_indices = mesh_indices_buffer_.read<std::uint32_t>(counters.index_count);
		const auto gpu_boundary_edges = boundary_edges_buffer_.read<GpuBoundaryEdge>(counters.edge_count);

		std::vector<vec2> boundary_vertices;
		boundary_vertices.reserve(gpu_boundary_vertices.size());
		for (const auto& vertex : gpu_boundary_vertices)
		{
			boundary_vertices.push_back(to_vec2(vertex));
		}

		result.mesh_vertices.reserve(gpu_mesh_vertices.size());
		for (const auto& vertex : gpu_mesh_vertices)
		{
			result.mesh_vertices.push_back(to_vec2(vertex));
		}

		result.mesh_indices = mesh_indices;

		std::vector<BoundaryEdge> boundary_edges;
		boundary_edges.reserve(gpu_boundary_edges.size());
		for (const auto& edge : gpu_boundary_edges)
		{
			boundary_edges.push_back({ edge.a, edge.b });
		}

		const auto extracted = extract_contours(boundary_vertices, boundary_edges);
		result.loops = extracted.loops;
		result.open_paths = extracted.open_paths;

		const float min_segment_length = std::min(terrain_cell_size.x, terrain_cell_size.y) * 0.03f;
		const float collinear_epsilon = std::min(terrain_cell_size.x, terrain_cell_size.y) * 0.05f;
		const float min_loop_area = terrain_cell_size.x * terrain_cell_size.y * 0.12f;
		const float min_path_length = std::min(terrain_cell_size.x, terrain_cell_size.y) * 1.5f;
		float primary_score = 0.0f;

		for (const auto& loop : result.loops)
		{
			auto simplified = simplify_contour(loop, true, min_segment_length, collinear_epsilon);
			if (simplified.size() < 4) continue;

			auto area = signed_area(simplified);
			if (area < 0.0f)
			{
				std::reverse(simplified.begin(), simplified.end());
				area = -area;
			}

			if (area < min_loop_area) continue;

			result.collider_loops.push_back(simplified);
			result.contour_score += area;

			if (area > primary_score)
			{
				primary_score = area;
				result.primary_contour = simplified;
			}
		}

		for (const auto& path : result.open_paths)
		{
			auto simplified = simplify_contour(path, false, min_segment_length, collinear_epsilon);
			if (simplified.size() < 2) continue;

			const auto length = polyline_length(simplified);
			if (length < min_path_length) continue;

			const auto path_score = length * std::max(terrain_cell_size.x, terrain_cell_size.y);
			result.collider_paths.push_back(simplified);
			result.contour_score += path_score;

			if (path_score > primary_score)
			{
				primary_score = path_score;
				result.primary_contour = simplified;
			}
		}

		return result;
	}

	TerrainChunk::ExtractedContours TerrainChunk::extract_contours(const std::vector<vec2>& boundary_vertices, const std::vector<BoundaryEdge>& edges) const
	{
		ExtractedContours contours{};

		std::unordered_map<std::uint32_t, std::vector<std::size_t>> outgoing_edges;
		outgoing_edges.reserve(edges.size());

		std::vector<std::uint32_t> incoming_count(boundary_vertices.size(), 0);
		std::vector<std::uint32_t> outgoing_count(boundary_vertices.size(), 0);

		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if (edges[i].a >= boundary_vertices.size() || edges[i].b >= boundary_vertices.size()) continue;
			outgoing_edges[edges[i].a].push_back(i);
			++outgoing_count[edges[i].a];
			++incoming_count[edges[i].b];
		}

		std::vector visited(edges.size(), false);

		auto consume_path = [&](const std::size_t start_edge_index)
		{
			if (visited[start_edge_index]) return;

			std::vector<vec2> contour;
			contour.reserve(16);

			auto current_edge_index = start_edge_index;
			const auto start_vertex = edges[start_edge_index].a;
			contour.push_back(boundary_vertices[start_vertex]);
			bool closed = false;

			while (true)
			{
				if (visited[current_edge_index]) break;

				visited[current_edge_index] = true;
				const auto& edge = edges[current_edge_index];
				contour.push_back(boundary_vertices[edge.b]);

				if (edge.b == start_vertex)
				{
					closed = true;
					break;
				}

				const auto next = outgoing_edges.find(edge.b);
				if (next == outgoing_edges.end()) break;

				const auto next_edge = std::ranges::find_if(next->second, [&visited](const std::size_t candidate_edge)
				{
					return !visited[candidate_edge];
				});

				if (next_edge == next->second.end()) break;

				current_edge_index = *next_edge;
			}

			if (closed)
			{
				if (!contour.empty()) contour.pop_back();
				if (contour.size() >= 3) contours.loops.push_back(std::move(contour));
			}
			else if (contour.size() >= 2)
			{
				contours.open_paths.push_back(std::move(contour));
			}
		};

		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if (visited[i]) continue;
			if (edges[i].a >= boundary_vertices.size() || edges[i].b >= boundary_vertices.size()) continue;

			const auto start_vertex = edges[i].a;
			if (incoming_count[start_vertex] != 1u || outgoing_count[start_vertex] != 1u)
			{
				consume_path(i);
			}
		}

		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if (visited[i]) continue;
			if (edges[i].a >= boundary_vertices.size() || edges[i].b >= boundary_vertices.size()) continue;
			consume_path(i);
		}

		return contours;
	}

	std::vector<vec2> TerrainChunk::simplify_contour(const std::vector<vec2>& contour, const bool closed, const float min_segment_length, const float collinear_epsilon) const
	{
		const auto min_points = closed ? 4u : 2u;
		if (contour.size() < min_points) return contour;

		auto simplified = contour;
		bool changed = true;
		const float min_segment_length_sq = min_segment_length * min_segment_length;
		constexpr float combine_dot_threshold = 0.9985f;

		while (changed && simplified.size() >= min_points)
		{
			changed = false;
			std::vector<vec2> next;
			next.reserve(simplified.size());

			for (std::size_t i = 0; i < simplified.size(); ++i)
			{
				if (!closed && (i == 0 || i + 1 == simplified.size()))
				{
					next.push_back(simplified[i]);
					continue;
				}

				const auto& prev = simplified[(i + simplified.size() - 1) % simplified.size()];
				const auto& current = simplified[i];
				const auto& following = simplified[(i + 1) % simplified.size()];

				const vec2 first_edge{ current.x - prev.x, current.y - prev.y };
				const vec2 second_edge{ following.x - current.x, following.y - current.y };
				const float first_length_sq = length_squared(first_edge);
				const float second_length_sq = length_squared(second_edge);

				if (first_length_sq <= min_segment_length_sq || second_length_sq <= min_segment_length_sq)
				{
					if (simplified.size() <= min_points)
					{
						next.push_back(current);
						continue;
					}

					changed = true;
					continue;
				}

				const float alignment = dot(first_edge, second_edge) / std::sqrt(first_length_sq * second_length_sq);
				const bool nearly_collinear = alignment >= combine_dot_threshold && distance_to_segment(current, prev, following) <= collinear_epsilon;

				if (nearly_collinear && simplified.size() > min_points)
				{
					changed = true;
					continue;
				}

				next.push_back(current);
			}

			if (next.size() < min_points || next.size() == simplified.size()) break;
			simplified = std::move(next);
		}

		return simplified;
	}

	vec2 TerrainChunk::calculate_spawn(const std::vector<vec2>& contour) const
	{
		if (contour.empty()) return { 0.0f, 0.0f };

		float top = -std::numeric_limits<float>::infinity();
		for (const auto& point : contour)
		{
			top = std::max(top, point.y);
		}

		float x_sum = 0.0f;
		std::size_t x_count = 0;
		const float band = cell_size().y * 4.0f;

		for (const auto& point : contour)
		{
			if (top - point.y <= band)
			{
				x_sum += point.x;
				++x_count;
			}
		}

		const float spawn_x = x_count > 0 ? x_sum / static_cast<float>(x_count) : contour.front().x;
		return { spawn_x, top + 1.75f };
	}

	vec2 TerrainChunk::cell_size() const
	{
		return {
			settings_.world_size.x / static_cast<float>(std::max(settings_.field_size.x - 1u, 1u)),
			settings_.world_size.y / static_cast<float>(std::max(settings_.field_size.y - 1u, 1u))
		};
	}

	void TerrainChunk::reset_buffers()
	{
		const std::vector horizontal_ids(settings_.field_size.y * (settings_.field_size.x - 1u), -1);
		const std::vector vertical_ids(settings_.field_size.y * settings_.field_size.x, -1);

		horizontal_edge_ids_buffer_.write(std::span{ horizontal_ids.data(), horizontal_ids.size() });
		vertical_edge_ids_buffer_.write(std::span{ vertical_ids.data(), vertical_ids.size() });

		const auto zero_counter = std::uint32_t{ 0 };
		boundary_vertex_counter_buffer_.write(std::span{ &zero_counter, 1u });

		const auto zero_counters = GpuCounters{};
		counters_buffer_.write(std::span{ &zero_counters, 1u });
	}

	void TerrainChunk::build_chunk(const PipelineResult& result)
	{
		build_mesh(result.mesh_vertices, result.mesh_indices);
		build_debug_lines(result.loops, result.open_paths, result.collider_loops, result.collider_paths);
		create_colliders(result.collider_loops, result.collider_paths);
		player_spawn_ = calculate_spawn(result.primary_contour);
	}

	void TerrainChunk::build_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices)
	{
		std::vector<gfx::Vertex> mesh_vertices;
		mesh_vertices.reserve(vertices.size());

		const auto height = std::max(chunk_max_.y - chunk_min_.y, 0.001f);

		for (const auto& point : vertices)
		{
			const auto gradient = clamp01((point.y - chunk_min_.y) / height);
			auto color = lerp_color(0x5A4129_rgb, 0xD6B27B_rgb, gradient);
			color.a = 210;

			mesh_vertices.push_back({
				.position = point,
				.color = to_gl_color(color)
			});
		}

		mesh_.set_data(mesh_vertices, indices);
	}

	void TerrainChunk::build_debug_lines(const std::vector<std::vector<vec2>>& loops, const std::vector<std::vector<vec2>>& open_paths, const std::vector<std::vector<vec2>>& collider_loops, const std::vector<std::vector<vec2>>& collider_paths)
	{
		edge_debug_lines_.clear();
		edge_debug_lines_.reserve(loops.size() + open_paths.size());

		for (const auto& loop : loops)
		{
			if (loop.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, loop.size() + 1u };
			for (std::size_t i = 0; i < loop.size(); ++i)
			{
				line_strip[i] = { loop[i], sf::Color(255, 183, 107, 175), {} };
			}
			line_strip[loop.size()] = { loop.front(), sf::Color(255, 183, 107, 175), {} };
			edge_debug_lines_.push_back(std::move(line_strip));
		}

		for (const auto& path : open_paths)
		{
			if (path.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, path.size() };
			for (std::size_t i = 0; i < path.size(); ++i)
			{
				line_strip[i] = { path[i], sf::Color(255, 183, 107, 175), {} };
			}
			edge_debug_lines_.push_back(std::move(line_strip));
		}

		collider_debug_lines_.clear();
		collider_debug_lines_.reserve(collider_loops.size() + collider_paths.size());

		for (const auto& loop : collider_loops)
		{
			if (loop.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, loop.size() + 1u };
			for (std::size_t i = 0; i < loop.size(); ++i)
			{
				line_strip[i] = { loop[i], sf::Color(123, 229, 214), {} };
			}
			line_strip[loop.size()] = { loop.front(), sf::Color(123, 229, 214), {} };
			collider_debug_lines_.push_back(std::move(line_strip));
		}

		for (const auto& path : collider_paths)
		{
			if (path.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, path.size() };
			for (std::size_t i = 0; i < path.size(); ++i)
			{
				line_strip[i] = { path[i], sf::Color(123, 229, 214), {} };
			}
			collider_debug_lines_.push_back(std::move(line_strip));
		}
	}

	void TerrainChunk::create_colliders(const std::vector<std::vector<vec2>>& collider_loops, const std::vector<std::vector<vec2>>& collider_paths)
	{
		if (b2Body_IsValid(terrain_body_))
		{
			b2DestroyBody(terrain_body_);
			terrain_body_ = b2_nullBodyId;
		}

		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;
		body_def.name = "terrain_chunk";

		terrain_body_ = b2CreateBody(world_id_, &body_def);

		b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
		material.friction = 0.9f;
		material.restitution = 0.05f;

		b2ShapeDef segment_shape_def = b2DefaultShapeDef();
		segment_shape_def.material.friction = material.friction;
		segment_shape_def.material.restitution = material.restitution;

		std::size_t created_count = 0;

		for (const auto& collider_loop : collider_loops)
		{
			if (collider_loop.size() < 4) continue;

			std::vector<b2Vec2> points;
			points.reserve(collider_loop.size());
			for (const auto& point : collider_loop)
			{
				points.push_back(to_b2(point));
			}

			b2ChainDef chain_def = b2DefaultChainDef();
			chain_def.points = points.data();
			chain_def.count = static_cast<int>(points.size());
			chain_def.materials = &material;
			chain_def.materialCount = 1;
			chain_def.isLoop = true;

			b2CreateChain(terrain_body_, &chain_def);
			++created_count;
		}

		for (const auto& collider_path : collider_paths)
		{
			if (collider_path.size() < 2) continue;

			for (std::size_t i = 1; i < collider_path.size(); ++i)
			{
				const b2Segment segment{ to_b2(collider_path[i - 1]), to_b2(collider_path[i]) };
				b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
				++created_count;
			}
		}

		if (created_count == 0)
		{
			throw std::runtime_error("Failed to create any terrain colliders from the generated contours");
		}
	}
}
