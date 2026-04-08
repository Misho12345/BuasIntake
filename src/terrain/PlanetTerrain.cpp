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

			vec2 normalize_vec2(const vec2& value, const vec2 fallback = { 0.0f, 1.0f })
			{
				const float length_sq = value.x * value.x + value.y * value.y;
				if (length_sq <= 1e-8f) return fallback;

				const float inverse_length = 1.0f / std::sqrt(length_sq);
				return { value.x * inverse_length, value.y * inverse_length };
			}

			vec2 add_vec2(const vec2& a, const vec2& b)
			{
				return { a.x + b.x, a.y + b.y };
		}

		vec2 cell_size(const ChunkSettings& settings)
		{
			return {
				settings.chunk_size.x / static_cast<float>(std::max(settings.field_size.x - 1u, 1u)),
				settings.chunk_size.y / static_cast<float>(std::max(settings.field_size.y - 1u, 1u))
			};
		}

		vec2 subtract_vec2(const vec2& a, const vec2& b)
		{
			return { a.x - b.x, a.y - b.y };
		}

		bool circle_overlaps_rect(const vec2 center, const float radius, const vec2 rect_min, const vec2 rect_max)
		{
			const float closest_x = std::clamp(center.x, rect_min.x, rect_max.x);
			const float closest_y = std::clamp(center.y, rect_min.y, rect_max.y);
			const float dx = center.x - closest_x;
			const float dy = center.y - closest_y;
			return dx * dx + dy * dy <= radius * radius;
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

	void PlanetTerrain::queue_edit(const TerrainEdit& edit)
	{
		pending_edits_.push_back(edit);
	}

	void PlanetTerrain::apply_pending_edits()
	{
		if (!pending_edits_.empty())
		{
			const auto total_chunk_count = chunk_count();
			const auto terrain_cell_size = cell_size(base_chunk_settings_);
			const vec2 padding_extent{
				terrain_cell_size.x * static_cast<float>(base_chunk_settings_.field_padding.x),
				terrain_cell_size.y * static_cast<float>(base_chunk_settings_.field_padding.y)
			};
			std::vector<std::vector<TerrainEdit>> edits_per_chunk(chunks_.size());

			for (const auto& edit : pending_edits_)
			{
				const vec2 center{
					edit.position_radius_strength.x,
					edit.position_radius_strength.y
				};
				const float radius = std::max(edit.position_radius_strength.z, 0.0f);

				if (radius <= 0.0f) continue;
				if (!circle_overlaps_rect(center, radius, grid_min_, grid_max_)) continue;

				const vec2 min_bounds{
					center.x - radius - padding_extent.x,
					center.y - radius - padding_extent.y
				};
				const vec2 max_bounds{
					center.x + radius + padding_extent.x,
					center.y + radius + padding_extent.y
				};
				const auto min_chunk = chunk_index_from_world(min_bounds);
				const auto max_chunk = chunk_index_from_world(max_bounds);

				for (int y = min_chunk.y; y <= max_chunk.y; ++y)
				{
					for (int x = min_chunk.x; x <= max_chunk.x; ++x)
					{
						auto& chunk = chunks_[flat_index({ x, y }, total_chunk_count)];
						if (!circle_overlaps_rect(center, radius, chunk.display_min(), chunk.display_max())) continue;
						edits_per_chunk[flat_index({ x, y }, total_chunk_count)].push_back(edit);
					}
				}
			}

			pending_edits_.clear();

			for (std::size_t i = 0; i < chunks_.size(); ++i)
			{
				if (edits_per_chunk[i].empty()) continue;
				chunks_[i].queue_edits(edits_per_chunk[i]);
			}
		}

		for (auto& chunk : chunks_)
		{
			chunk.update_pending_work();
		}
	}

	void PlanetTerrain::update_active_colliders(const vec2 world_position)
	{
		const float movement_threshold = 0.5f * std::min(base_chunk_settings_.chunk_size.x, base_chunk_settings_.chunk_size.y);
		if (active_chunk_initialized_)
		{
			const float dx = world_position.x - active_collider_center_.x;
			const float dy = world_position.y - active_collider_center_.y;
			if (dx * dx + dy * dy < movement_threshold * movement_threshold)
			{
				return;
			}
		}

		active_chunk_initialized_ = true;
		active_collider_center_ = world_position;

		const auto total_chunk_count = chunk_count();
		const auto terrain_chunk_size = base_chunk_settings_.chunk_size;
		const float active_radius = std::max(terrain_chunk_size.x, terrain_chunk_size.y);

		for (int y = 0; y < total_chunk_count.y; ++y)
		{
			for (int x = 0; x < total_chunk_count.x; ++x)
			{
				auto& chunk = chunks_[flat_index({ x, y }, total_chunk_count)];
				const auto is_active = circle_overlaps_rect(
					world_position,
					active_radius,
					chunk.display_min(),
					chunk.display_max());

				chunk.set_collision_enabled(is_active);
			}
		}
	}

	vec2 PlanetTerrain::display_min() const { return display_min_; }
	vec2 PlanetTerrain::display_max() const { return display_max_; }
	vec2 PlanetTerrain::chunk_size() const { return base_chunk_settings_.chunk_size; }
	vec2 PlanetTerrain::planet_center() const { return base_chunk_settings_.world_center; }

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
			const vec2 hit_point{
				ray_result.point.x,
				ray_result.point.y
			};
			const vec2 radial_up = normalize_vec2(subtract_vec2(hit_point, terrain_world_center));
			return {
				hit_point.x + radial_up.x * height_offset,
				hit_point.y + radial_up.y * height_offset
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
