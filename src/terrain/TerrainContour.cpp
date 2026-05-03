#include "pch.hpp"
#include "TerrainContour.hpp"

#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
	namespace
	{
		float distance(const vec2& a, const vec2& b)
		{
			return vec2{ a.x - b.x, a.y - b.y }.length();
		}

		float distance_to_segment(const vec2& point, const vec2& a, const vec2& b)
		{
			const vec2 ab{ b.x - a.x, b.y - a.y };
			const float ab_length_sq = ab.lengthSquared();

			if (ab_length_sq <= std::numeric_limits<float>::epsilon()) return distance(point, a);

			const vec2 ap{ point.x - a.x, point.y - a.y };
			const float t = std::clamp(ap.dot(ab) / ab_length_sq, 0.0f, 1.0f);
			const vec2 closest{ a.x + ab.x * t, a.y + ab.y * t };
			return distance(point, closest);
		}

		float signed_area(const std::span<const vec2> points)
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

		float polyline_length(const std::span<const vec2> points)
		{
			if (points.size() < 2) return 0.0f;

			float total = 0.0f;
			for (std::size_t i = 1; i < points.size(); ++i)
			{
				total += distance(points[i - 1], points[i]);
			}

			return total;
		}

		bool touches_chunk_boundary(const std::span<const vec2> points, const ChunkSettings& settings, const float epsilon)
		{
			const auto terrain_cell_size = cell_size(settings);
			const auto min = chunk_min(settings);
			const auto max = chunk_max(settings);
			const vec2 padded_min{
				min.x - terrain_cell_size.x * static_cast<float>(settings.field_padding.x),
				min.y - terrain_cell_size.y * static_cast<float>(settings.field_padding.y)
			};
			const vec2 padded_max{
				max.x + terrain_cell_size.x * static_cast<float>(settings.field_padding.x),
				max.y + terrain_cell_size.y * static_cast<float>(settings.field_padding.y)
			};

			for (const auto& point : points)
			{
				if (std::abs(point.x - min.x) <= epsilon || std::abs(point.x - max.x) <= epsilon ||
					std::abs(point.y - min.y) <= epsilon || std::abs(point.y - max.y) <= epsilon ||
					std::abs(point.x - padded_min.x) <= epsilon || std::abs(point.x - padded_max.x) <= epsilon ||
					std::abs(point.y - padded_min.y) <= epsilon || std::abs(point.y - padded_max.y) <= epsilon)
				{
					return true;
				}
			}

			return false;
		}
	}

	TerrainContour::ExtractedContours TerrainContour::extract_contours(const std::vector<vec2>& boundary_vertices, const std::vector<TerrainGenerator::BoundaryEdge>& edges)
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

				const auto& [a, b] = edges[current_edge_index];
				contour.push_back(boundary_vertices[b]);

				if (b == start_vertex)
				{
					closed = true;
					break;
				}

				const auto next = outgoing_edges.find(b);
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

			if (const auto start_vertex = edges[i].a;
				incoming_count[start_vertex] != 1u || outgoing_count[start_vertex] != 1u)
				consume_path(i);
		}

		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if (visited[i]) continue;
			if (edges[i].a >= boundary_vertices.size() || edges[i].b >= boundary_vertices.size()) continue;
			consume_path(i);
		}

		return contours;
	}

	std::vector<vec2> TerrainContour::simplify_contour(const std::vector<vec2>& contour, const bool closed, const float min_segment_length, const float collinear_epsilon)
	{
		const auto min_points = closed ? 4u : 2u;
		if (contour.size() < min_points) return contour;

		auto simplified = contour;
		bool changed = true;
		const float min_segment_length_sq = min_segment_length * min_segment_length;
		static constexpr float combine_dot_threshold = 0.9985f;

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
				const float first_length_sq = first_edge.lengthSquared();
				const float second_length_sq = second_edge.lengthSquared();

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

				const float alignment = first_edge.dot(second_edge) / std::sqrt(first_length_sq * second_length_sq);

				if (const bool nearly_collinear = alignment >= combine_dot_threshold &&
							distance_to_segment(current, prev, following) <= collinear_epsilon;
					nearly_collinear && simplified.size() > min_points)
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

	TerrainContour::ScoredResult TerrainContour::score_and_filter(TerrainGenerator::RawPipelineResult raw, const ChunkSettings& settings)
	{
		ScoredResult result{};
		result.mesh_vertices = std::move(raw.mesh_vertices);
		result.mesh_indices = std::move(raw.mesh_indices);

		if (raw.boundary_vertices.empty() || raw.boundary_edges.empty()) return result;

		const auto [loops, open_paths] = extract_contours(raw.boundary_vertices, raw.boundary_edges);

		const auto terrain_cell_size = cell_size(settings);
		const float min_segment_length = std::min(terrain_cell_size.x, terrain_cell_size.y) * 0.10f;
		const float collinear_epsilon = std::min(terrain_cell_size.x, terrain_cell_size.y) * 0.18f;
		const float min_loop_area = terrain_cell_size.x * terrain_cell_size.y * 0.12f;
		const float min_path_length = std::min(terrain_cell_size.x, terrain_cell_size.y) * 1.5f;
		const float boundary_epsilon = std::max(terrain_cell_size.x, terrain_cell_size.y) * 0.1f;
		float primary_score = 0.0f;

		for (const auto& loop : loops)
		{
			auto simplified = simplify_contour(loop, true, min_segment_length, collinear_epsilon);
			if (simplified.size() < 4) continue;

			auto area = signed_area(simplified);
			if (area < 0.0f)
			{
				std::ranges::reverse(simplified);
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

		for (const auto& path : open_paths)
		{
			auto simplified = simplify_contour(path, false, min_segment_length, collinear_epsilon);
			if (simplified.size() < 2) continue;

			const auto length = polyline_length(simplified);
			if (length < min_path_length && !touches_chunk_boundary(simplified, settings, boundary_epsilon)) continue;

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
}
