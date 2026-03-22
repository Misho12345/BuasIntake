#include "pch.hpp"
#include "PlanetTerrain.hpp"

namespace game::terrain
{
	namespace
	{
		vec2 scale_vec2(const vec2& value, const float scalar)
		{
			return { value.x * scalar, value.y * scalar };
		}

		vec2 add_vec2(const vec2& a, const vec2& b)
		{
			return { a.x + b.x, a.y + b.y };
		}

		vec2 subtract_vec2(const vec2& a, const vec2& b)
		{
			return { a.x - b.x, a.y - b.y };
		}
	}

	PlanetTerrain::PlanetTerrain(const b2WorldId world_id) :
		world_id_{ world_id }
	{
		const auto total_chunk_count = chunk_count();
		base_chunk_settings_.chunk_grid_size = total_chunk_count;
		const auto terrain_chunk_size = base_chunk_settings_.chunk_size;
		const auto terrain_world_center = base_chunk_settings_.world_center;

		const vec2 total_world_size{
			terrain_chunk_size.x * static_cast<float>(total_chunk_count.x),
			terrain_chunk_size.y * static_cast<float>(total_chunk_count.y)
		};

		grid_min_ = subtract_vec2(terrain_world_center, scale_vec2(total_world_size, 0.5f));
		grid_max_ = add_vec2(grid_min_, total_world_size);

		display_min_ = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
		display_max_ = { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };

		const auto planet_radius = compute_planet_radius();
		chunks_.reserve(static_cast<std::size_t>(total_chunk_count.x * total_chunk_count.y));

		for (int y = 0; y < total_chunk_count.y; ++y)
		{
			for (int x = 0; x < total_chunk_count.x; ++x)
			{
				auto chunk_settings = base_chunk_settings_;
				chunk_settings.chunk_coord = { x, y };
				chunk_settings.chunk_grid_size = total_chunk_count;
				chunk_settings.planet_radius = planet_radius;

				auto& chunk = chunks_.emplace_back(world_id_, chunk_settings);
				display_min_.x = std::min(display_min_.x, chunk.display_min().x);
				display_min_.y = std::min(display_min_.y, chunk.display_min().y);
				display_max_.x = std::max(display_max_.x, chunk.display_max().x);
				display_max_.y = std::max(display_max_.y, chunk.display_max().y);
			}
		}

		if (chunks_.empty())
		{
			display_min_ = grid_min_;
			display_max_ = grid_max_;
			return;
		}

		for (auto& chunk : chunks_)
		{
			chunk.dispatch_generation();
		}

		for (auto& chunk : chunks_)
		{
			chunk.finalize_generation();
		}
	}

	void PlanetTerrain::draw_gl(const sf::View& view) const
	{
		for (const auto& chunk : chunks_)
		{
			chunk.draw_gl(view);
		}
	}

	void PlanetTerrain::render_debug(sf::RenderTarget& target) const
	{
		for (const auto& chunk : chunks_)
		{
			chunk.render_debug(target);
		}
	}

	void PlanetTerrain::update_active_colliders(const vec2 world_position)
	{
		const auto total_chunk_count = chunk_count();
		const auto chunk_index = chunk_index_from_world(world_position);

		if (active_chunk_initialized_ &&
			chunk_index.x == active_chunk_index_.x &&
			chunk_index.y == active_chunk_index_.y)
		{
			return;
		}

		active_chunk_initialized_ = true;
		active_chunk_index_ = chunk_index;

		for (int y = 0; y < total_chunk_count.y; ++y)
		{
			for (int x = 0; x < total_chunk_count.x; ++x)
			{
				const auto is_active =
					std::abs(x - chunk_index.x) <= 1 &&
					std::abs(y - chunk_index.y) <= 1;

				chunks_[flat_index({ x, y }, total_chunk_count)].set_collision_enabled(is_active);
			}
		}
	}

	vec2 PlanetTerrain::display_min() const { return display_min_; }
	vec2 PlanetTerrain::display_max() const { return display_max_; }
	vec2 PlanetTerrain::chunk_size() const { return base_chunk_settings_.chunk_size; }

	vec2 PlanetTerrain::spawn_point_from_top_center(const float height_offset) const
	{
		const auto terrain_world_center = base_chunk_settings_.world_center;
		const vec2 ray_origin{
			terrain_world_center.x,
			display_max_.y + base_chunk_settings_.chunk_size.y * 2.0f
		};
		const vec2 ray_end{
			terrain_world_center.x,
			display_min_.y - base_chunk_settings_.chunk_size.y * 2.0f
		};
		const b2QueryFilter filter = b2DefaultQueryFilter();
		const auto ray_result = b2World_CastRayClosest(
			world_id_,
			{ ray_origin.x, ray_origin.y },
			{ ray_end.x - ray_origin.x, ray_end.y - ray_origin.y },
			filter);

		if (ray_result.hit)
		{
			return {
				ray_result.point.x,
				ray_result.point.y + height_offset
			};
		}

		return {
			terrain_world_center.x,
			display_max_.y + height_offset
		};
	}

	float PlanetTerrain::compute_planet_radius()
	{
		const auto total_chunk_count = chunk_count();
		const ChunkSettings defaults{};
		const auto terrain_chunk_size = defaults.chunk_size;
		const vec2 total_world_size{
			terrain_chunk_size.x * static_cast<float>(total_chunk_count.x),
			terrain_chunk_size.y * static_cast<float>(total_chunk_count.y)
		};

		const auto min_half_extent = std::min(total_world_size.x, total_world_size.y) * 0.5f;
		const auto edge_padding = std::min(terrain_chunk_size.x, terrain_chunk_size.y) * 0.75f;
		return std::max(min_half_extent - edge_padding, 1.0f);
	}

	std::size_t PlanetTerrain::flat_index(const ivec2 chunk_index, const ivec2 chunk_count)
	{
		return static_cast<std::size_t>(chunk_index.y * chunk_count.x + chunk_index.x);
	}

	ivec2 PlanetTerrain::chunk_index_from_world(const vec2 world_position) const
	{
		const auto total_chunk_count = chunk_count();
		const auto local_x = (world_position.x - grid_min_.x) / base_chunk_settings_.chunk_size.x;
		const auto local_y = (world_position.y - grid_min_.y) / base_chunk_settings_.chunk_size.y;

		return {
			std::clamp(static_cast<int>(std::floor(local_x)), 0, total_chunk_count.x - 1),
			std::clamp(static_cast<int>(std::floor(local_y)), 0, total_chunk_count.y - 1)
		};
	}
}
