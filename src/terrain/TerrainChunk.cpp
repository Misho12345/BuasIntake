#include "pch.hpp"
#include "TerrainChunk.hpp"

namespace game::terrain
{
	namespace
	{
		vec2 compute_chunk_min(const ChunkSettings& settings)
		{
			return {
				settings.world_center.x +
					(static_cast<float>(settings.chunk_coord.x) - 0.5f * static_cast<float>(settings.chunk_grid_size.x)) * settings.chunk_size.x,
				settings.world_center.y +
					(static_cast<float>(settings.chunk_coord.y) - 0.5f * static_cast<float>(settings.chunk_grid_size.y)) * settings.chunk_size.y
			};
		}

		float clamp01(const float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		sf::Color lerp_color(const sf::Color& a, const sf::Color& b, const float t)
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

		vec2 cell_size(const ChunkSettings& settings)
		{
			return {
				settings.chunk_size.x / static_cast<float>(std::max(settings.field_size.x - 1u, 1u)),
				settings.chunk_size.y / static_cast<float>(std::max(settings.field_size.y - 1u, 1u))
			};
		}

		uvec2 padded_field_size(const ChunkSettings& settings)
		{
			return {
				settings.field_size.x + settings.field_padding.x * 2u,
				settings.field_size.y + settings.field_padding.y * 2u
			};
		}

		vec2 field_origin(const ChunkSettings& settings)
		{
			const auto terrain_cell_size = cell_size(settings);
			const auto min = compute_chunk_min(settings);
			return {
				min.x - terrain_cell_size.x * static_cast<float>(settings.field_padding.x),
				min.y - terrain_cell_size.y * static_cast<float>(settings.field_padding.y)
			};
		}

		float bilerp(const float a, const float b, const float c, const float d, const float tx, const float ty)
		{
			const float ab = std::lerp(a, b, tx);
			const float cd = std::lerp(c, d, tx);
			return std::lerp(ab, cd, ty);
		}

		float sample_wetness(const std::span<const TerrainChunk::FieldSample> field_samples,
			const ChunkSettings& settings, const vec2 world_position)
		{
			if (field_samples.empty()) return 0.0f;

			const auto size = padded_field_size(settings);
			const auto terrain_cell_size = cell_size(settings);
			const auto origin = field_origin(settings);

			const float gx = (world_position.x - origin.x) / terrain_cell_size.x;
			const float gy = (world_position.y - origin.y) / terrain_cell_size.y;

			const float clamped_x = std::clamp(gx, 0.0f, static_cast<float>(size.x - 1u));
			const float clamped_y = std::clamp(gy, 0.0f, static_cast<float>(size.y - 1u));

			const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
			const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
			const auto x1 = std::min(x0 + 1u, size.x - 1u);
			const auto y1 = std::min(y0 + 1u, size.y - 1u);

			const float tx = clamped_x - static_cast<float>(x0);
			const float ty = clamped_y - static_cast<float>(y0);

			auto wetness_at = [&](const std::uint32_t x, const std::uint32_t y)
			{
				return field_samples[static_cast<std::size_t>(y) * size.x + x].wetness;
			};

			return bilerp(
				wetness_at(x0, y0),
				wetness_at(x1, y0),
				wetness_at(x0, y1),
				wetness_at(x1, y1),
				tx,
				ty);
		}
	}

	TerrainChunk::TerrainChunk(const b2WorldId world_id, const ChunkSettings& settings) :
		settings_{ settings },
		generator_{ settings_ },
		collider_{ world_id, ColliderKind::Terrain, false },
		water_collider_{ world_id, ColliderKind::Water, true }
	{
		chunk_min_ = compute_chunk_min(settings_);
		chunk_max_ = {
			chunk_min_.x + settings_.chunk_size.x,
			chunk_min_.y + settings_.chunk_size.y
		};

		const auto terrain_cell_size = cell_size(settings_);
		const vec2 padding_extent{
			terrain_cell_size.x * static_cast<float>(settings_.field_padding.x),
			terrain_cell_size.y * static_cast<float>(settings_.field_padding.y)
		};
		display_min_ = { chunk_min_.x - padding_extent.x, chunk_min_.y - padding_extent.y };
		display_max_ = { chunk_max_.x + padding_extent.x, chunk_max_.y + padding_extent.y };

		build_chunk_border();
	}

	void TerrainChunk::draw_gl(const sf::View& view) const
	{
		renderable_.draw(mesh_, view);
	}

	void TerrainChunk::draw_water_gl(const sf::View& view) const
	{
		water_renderable_.draw(water_mesh_, view);
	}

	void TerrainChunk::render_debug(sf::RenderTarget& target) const
	{
		target.draw(chunk_border_);

		for (const auto& outline : edge_debug_lines_)
		{
			target.draw(outline);
		}

		for (const auto& outline : collider_debug_lines_)
		{
			target.draw(outline);
		}
	}

	void TerrainChunk::dispatch_generation()
	{
		if (generation_dispatched_ || generation_finalized_) return;
		generator_.dispatch();
		generation_dispatched_ = true;
	}

	void TerrainChunk::finalize_generation()
	{
		if (generation_finalized_) return;
		if (!generation_dispatched_) dispatch_generation();

		const auto field_samples = generator_.read_field();
		build_chunk(generate_chunk(), {}, field_samples);
		generation_dispatched_ = false;
		generation_finalized_ = true;
	}

	void TerrainChunk::rebuild_from_field(const std::span<const FieldSample> field_samples)
	{
		generator_.upload_field(field_samples);
		generator_.dispatch_surface_rebuild(TerrainGenerator::terrain_channel_index, 0.0f);
		const auto terrain_result = TerrainContour::score_and_filter(generator_.readback(), settings_);

		generator_.dispatch_surface_rebuild(TerrainGenerator::water_channel_index, 0.0f);
		const auto water_result = TerrainContour::score_and_filter(generator_.readback(), settings_);

		build_chunk(terrain_result, water_result, field_samples);
	}

	std::vector<TerrainChunk::FieldSample> TerrainChunk::readback_field() const
	{
		return generator_.read_field();
	}

	const gfx::Mesh& TerrainChunk::mesh() const { return mesh_; }
	ivec2 TerrainChunk::chunk_coord() const { return settings_.chunk_coord; }

	vec2 TerrainChunk::player_spawn() const { return player_spawn_; }
	vec2 TerrainChunk::chunk_min() const { return chunk_min_; }
	vec2 TerrainChunk::chunk_max() const { return chunk_max_; }
	vec2 TerrainChunk::display_min() const { return display_min_; }
	vec2 TerrainChunk::display_max() const { return display_max_; }
	bool TerrainChunk::has_collider() const { return collider_.has_body(); }
	void TerrainChunk::set_collision_enabled(const bool enabled)
	{
		collision_enabled_ = enabled;
		collider_.set_enabled(enabled);
		water_collider_.set_enabled(enabled);
	}

	TerrainContour::ScoredResult TerrainChunk::generate_chunk()
	{
		return TerrainContour::score_and_filter(generator_.readback(), settings_);
	}

	void TerrainChunk::build_chunk_border()
	{
		chunk_border_.setPosition(chunk_min_);
		chunk_border_.setSize({ settings_.chunk_size.x, settings_.chunk_size.y });
		chunk_border_.setFillColor(sf::Color::Transparent);
		chunk_border_.setOutlineColor(0xFF3B30_rgb);
		chunk_border_.setOutlineThickness(0.07f);
	}

	void TerrainChunk::build_chunk(const TerrainContour::ScoredResult& terrain_result,
		const TerrainContour::ScoredResult& water_result, const std::span<const FieldSample> field_samples)
	{
		build_terrain_mesh(terrain_result.mesh_vertices, terrain_result.mesh_indices, field_samples);
		build_water_mesh(water_result.mesh_vertices, water_result.mesh_indices);
		build_debug_lines(terrain_result.loops, terrain_result.open_paths, terrain_result.collider_loops, terrain_result.collider_paths);
		collider_.build(terrain_result.collider_loops, terrain_result.collider_paths);
		collider_.set_enabled(collision_enabled_);
		water_collider_.build(water_result.collider_loops, water_result.collider_paths);
		water_collider_.set_enabled(collision_enabled_);

		if (!terrain_result.primary_contour.empty())
		{
			player_spawn_ = TerrainContour::calculate_spawn(terrain_result.primary_contour, settings_);
		}
		else
		{
			player_spawn_ = {
				(chunk_min_.x + chunk_max_.x) * 0.5f,
				display_max_.y + 1.75f
			};
		}
	}

	void TerrainChunk::build_terrain_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices,
		const std::span<const FieldSample> field_samples)
	{
		std::vector<sf::Vertex> mesh_vertices;
		mesh_vertices.reserve(vertices.size());

		const auto radius = std::max(settings_.planet_radius, 0.001f);

		for (const auto& point : vertices)
		{
			const vec2 offset{
				point.x - settings_.world_center.x,
				point.y - settings_.world_center.y
			};
			const auto distance_from_center = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			const auto gradient = clamp01(distance_from_center / radius);
			const auto wetness = std::clamp(sample_wetness(field_samples, settings_, point), 0.0f, 1.0f);
			auto color = lerp_color(0x3F2C1C_rgb, 0xD6B27B_rgb, gradient);
			color.a = 210;

			sf::Vertex vertex{};
			vertex.position = { point.x, point.y };
			vertex.color = color;
			vertex.texCoords = { wetness, gradient };
			mesh_vertices.push_back(vertex);
		}

		mesh_.set_data(mesh_vertices, indices);
	}

	void TerrainChunk::build_water_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices)
	{
		if (vertices.empty() || indices.empty())
		{
			const std::vector<sf::Vertex> empty_vertices;
			const std::vector<std::uint32_t> empty_indices;
			water_mesh_.set_data(empty_vertices, empty_indices);
			return;
		}

		std::vector<sf::Vertex> mesh_vertices;
		mesh_vertices.reserve(vertices.size());

		for (const auto& point : vertices)
		{
			sf::Vertex vertex{};
			vertex.position = { point.x, point.y };
			vertex.color = { 232, 248, 255, 196 };
			vertex.texCoords = { 0.0f, 0.0f };
			mesh_vertices.push_back(vertex);
		}

		water_mesh_.set_data(mesh_vertices, indices);
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
				line_strip[i] = {
					.position = loop[i], 
					.color = 0xFFB76BB0_rgba, 
					.texCoords = {}
				};
			}

			line_strip[loop.size()] = {
				.position = loop.front(),
				.color = 0xFFB76BB0_rgba,
				.texCoords = {}
			};

			edge_debug_lines_.push_back(std::move(line_strip));
		}

		for (const auto& path : open_paths)
		{
			if (path.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, path.size() };
			for (std::size_t i = 0; i < path.size(); ++i)
			{
				line_strip[i] = {
					.position = path[i], 
					.color = 0xFFB76BB0_rgba,
					.texCoords = {}
				};
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
				line_strip[i] = {
					.position = loop[i], 
					.color = 0x7BE5D6_rgb,
					.texCoords = {}
				};
			}
			line_strip[loop.size()] = {
				.position = loop.front(), 
				.color = 0x7BE5D6_rgb,
				.texCoords = {}
			};

			collider_debug_lines_.push_back(std::move(line_strip));
		}

		for (const auto& path : collider_paths)
		{
			if (path.size() < 2) continue;

			sf::VertexArray line_strip{ sf::PrimitiveType::LineStrip, path.size() };
			for (std::size_t i = 0; i < path.size(); ++i)
			{
				line_strip[i] = {
					.position = path[i], 
					.color = 0x7BE5D6_rgb,
					.texCoords = {}
				};
			}
			collider_debug_lines_.push_back(std::move(line_strip));
		}
	}
}
