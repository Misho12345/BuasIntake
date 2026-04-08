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

		vec2 subtract_vec2(const vec2& a, const vec2& b)
		{
			return { a.x - b.x, a.y - b.y };
		}

		float radial_distance(const vec2& point, const vec2& center)
		{
			const vec2 offset = subtract_vec2(point, center);
			return std::sqrt(offset.x * offset.x + offset.y * offset.y);
		}

		bool circle_overlaps_rect(const vec2 center, const float radius, const vec2 rect_min, const vec2 rect_max)
		{
			const float closest_x = std::clamp(center.x, rect_min.x, rect_max.x);
			const float closest_y = std::clamp(center.y, rect_min.y, rect_max.y);
			const float dx = center.x - closest_x;
			const float dy = center.y - closest_y;
			return dx * dx + dy * dy <= radius * radius;
		}

		bool has_water(const PlanetTerrain::FieldSample& sample)
		{
			return sample.water > 1e-4f;
		}

		bool is_solid(const PlanetTerrain::FieldSample& sample)
		{
			return sample.terrain >= 0.0f;
		}

		float dry_water_density(const PlanetTerrain::FieldSample& sample)
		{
			return -std::abs(sample.terrain);
		}

		std::uint8_t normalized_channel(const float value, const float min_value, const float max_value)
		{
			if (std::abs(max_value - min_value) <= 1e-6f) return 127u;
			const float normalized = std::clamp((value - min_value) / (max_value - min_value), 0.0f, 1.0f);
			return static_cast<std::uint8_t>(std::lround(normalized * 255.0f));
		}

		std::uint64_t sample_key(const ivec2 coord)
		{
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u) |
				static_cast<std::uint32_t>(coord.y);
		}

		struct WaterCandidate final
		{
			ivec2 coord{ 0, 0 };
			float radial{ 0.0f };
			float click_distance_sq{ 0.0f };
		};

		struct WaterCandidateCompare final
		{
			bool operator()(const WaterCandidate& lhs, const WaterCandidate& rhs) const
			{
				if (std::abs(lhs.radial - rhs.radial) > 1e-5f) return lhs.radial > rhs.radial;
				return lhs.click_distance_sq > rhs.click_distance_sq;
			}
		};
	}

	PlanetTerrain::PlanetTerrain(const b2WorldId world_id) :
		world_id_{ world_id }
	{
		const auto total_chunk_count = chunk_count();
		base_chunk_settings_.chunk_grid_size = total_chunk_count;
		const auto terrain_chunk_size = base_chunk_settings_.chunk_size;
		const auto terrain_world_center = base_chunk_settings_.world_center;
		terrain_cell_size_ = cell_size(base_chunk_settings_);

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

		initialize_global_field();
	}

	uvec2 PlanetTerrain::padded_field_size(const ChunkSettings& settings)
	{
		return {
			settings.field_size.x + settings.field_padding.x * 2u,
			settings.field_size.y + settings.field_padding.y * 2u
		};
	}

	ivec2 PlanetTerrain::chunk_sample_stride(const ChunkSettings& settings)
	{
		return {
			static_cast<std::int32_t>(std::max(settings.field_size.x, 1u) - 1u),
			static_cast<std::int32_t>(std::max(settings.field_size.y, 1u) - 1u)
		};
	}

	vec2 PlanetTerrain::cell_size(const ChunkSettings& settings)
	{
		return {
			settings.chunk_size.x / static_cast<float>(std::max(settings.field_size.x - 1u, 1u)),
			settings.chunk_size.y / static_cast<float>(std::max(settings.field_size.y - 1u, 1u))
		};
	}

	void PlanetTerrain::draw_gl(const sf::View& view) const
	{
		for (const auto& chunk : chunks_)
		{
			chunk.draw_gl(view);
		}
	}

	void PlanetTerrain::draw_water_gl(const sf::View& view) const
	{
		for (const auto& chunk : chunks_)
		{
			chunk.draw_water_gl(view);
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
		if (pending_edits_.empty() || global_field_.empty()) return;

		std::vector<bool> dirty_chunks(chunks_.size(), false);
		bool any_changes = false;
		for (const auto& edit : pending_edits_)
		{
			any_changes = apply_terrain_edit_to_global_field(edit, dirty_chunks) || any_changes;
		}

		pending_edits_.clear();
		if (any_changes) rebuild_dirty_chunks(dirty_chunks);
	}

	bool PlanetTerrain::place_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		if (global_field_.empty() || volume_cap == 0u) return false;

		const auto anchor = find_water_anchor(world_position);
		if (!anchor.has_value()) return false;

		std::vector<bool> dirty_chunks(chunks_.size(), false);
		const bool changed = fill_water_basin(*anchor, volume_cap, dirty_chunks);
		if (changed) rebuild_dirty_chunks(dirty_chunks);
		return changed;
	}

	bool PlanetTerrain::pickup_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		if (global_field_.empty() || volume_cap == 0u) return false;

		const auto anchor = find_water_sample(world_position);
		if (!anchor.has_value()) return false;

		std::vector<bool> dirty_chunks(chunks_.size(), false);
		const bool changed = remove_water_volume(*anchor, volume_cap, dirty_chunks);
		if (changed) rebuild_dirty_chunks(dirty_chunks);
		return changed;
	}

	std::optional<fs::path> PlanetTerrain::save_chunk_field_image(const vec2 world_position) const
	{
		if (global_field_.empty()) return std::nullopt;

		const auto chunk_coord = chunk_index_from_world(world_position);
		const auto field_samples = extract_chunk_field(chunk_coord);
		const auto padded_size = padded_field_size(base_chunk_settings_);
		if (field_samples.empty() || padded_size.x == 0 || padded_size.y == 0) return std::nullopt;

		float terrain_min = std::numeric_limits<float>::infinity();
		float terrain_max = -std::numeric_limits<float>::infinity();
		float water_min = std::numeric_limits<float>::infinity();
		float water_max = -std::numeric_limits<float>::infinity();

		for (const auto& sample : field_samples)
		{
			terrain_min = std::min(terrain_min, sample.terrain);
			terrain_max = std::max(terrain_max, sample.terrain);
			water_min = std::min(water_min, sample.water);
			water_max = std::max(water_max, sample.water);
		}

		sf::Image image({ padded_size.x, padded_size.y }, sf::Color::Black);
		for (std::uint32_t y = 0; y < padded_size.y; ++y)
		{
			for (std::uint32_t x = 0; x < padded_size.x; ++x)
			{
				const auto& sample = field_samples[static_cast<std::size_t>(y) * padded_size.x + x];
				image.setPixel(
					{ x, padded_size.y - 1u - y },
					{
						normalized_channel(sample.terrain, terrain_min, terrain_max),
						normalized_channel(sample.water, water_min, water_max),
						static_cast<std::uint8_t>(sample.terrain >= 0.0f ? 255u : 0u),
						static_cast<std::uint8_t>(sample.water > 0.0f ? 255u : 0u)
					});
			}
		}

		const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		const fs::path output_dir = fs::path{ "chunk_png_save" };
		fs::create_directories(output_dir);

		const auto base_name = std::format(
			"chunk_{}_{}_{}",
			chunk_coord.x,
			chunk_coord.y,
			timestamp);
		const fs::path image_path = output_dir / (base_name + ".png");

		if (!image.saveToFile(image_path.string())) return std::nullopt;

		return image_path;
	}

	void PlanetTerrain::initialize_global_field()
	{
		const auto total_chunk_count = chunk_count();
		const auto padded_size = padded_field_size(base_chunk_settings_);
		const auto stride = chunk_sample_stride(base_chunk_settings_);
		const auto padding = base_chunk_settings_.field_padding;

		global_field_size_ = {
			static_cast<std::uint32_t>((total_chunk_count.x - 1) * stride.x + static_cast<int>(padded_size.x)),
			static_cast<std::uint32_t>((total_chunk_count.y - 1) * stride.y + static_cast<int>(padded_size.y))
		};
		global_field_origin_ = {
			grid_min_.x - terrain_cell_size_.x * static_cast<float>(padding.x),
			grid_min_.y - terrain_cell_size_.y * static_cast<float>(padding.y)
		};

		global_field_.assign(static_cast<std::size_t>(global_field_size_.x) * static_cast<std::size_t>(global_field_size_.y), {});

		for (const auto& chunk : chunks_)
		{
			const auto field = chunk.readback_field();
			if (field.empty()) continue;

			const ivec2 chunk_base{
				chunk.chunk_coord().x * stride.x,
				chunk.chunk_coord().y * stride.y
			};

			for (std::uint32_t y = 0; y < padded_size.y; ++y)
			{
				for (std::uint32_t x = 0; x < padded_size.x; ++x)
				{
					const ivec2 global_coord{
						chunk_base.x + static_cast<int>(x),
						chunk_base.y + static_cast<int>(y)
					};

					global_field_[global_field_index(global_coord)] = field[static_cast<std::size_t>(y) * padded_size.x + x];
				}
			}
		}

		for (auto& sample : global_field_)
		{
			sample.water = dry_water_density(sample);
		}
	}

	bool PlanetTerrain::is_valid_global_sample(const ivec2 coord) const
	{
		return coord.x >= 0 && coord.y >= 0 &&
			coord.x < static_cast<int>(global_field_size_.x) &&
			coord.y < static_cast<int>(global_field_size_.y);
	}

	std::size_t PlanetTerrain::global_field_index(const ivec2 coord) const
	{
		return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(global_field_size_.x) + static_cast<std::size_t>(coord.x);
	}

	vec2 PlanetTerrain::global_sample_world_position(const ivec2 coord) const
	{
		return {
			global_field_origin_.x + static_cast<float>(coord.x) * terrain_cell_size_.x,
			global_field_origin_.y + static_cast<float>(coord.y) * terrain_cell_size_.y
		};
	}

	ivec2 PlanetTerrain::world_to_global_sample(const vec2 world_position) const
	{
		const float gx = (world_position.x - global_field_origin_.x) / terrain_cell_size_.x;
		const float gy = (world_position.y - global_field_origin_.y) / terrain_cell_size_.y;

		return {
			std::clamp(static_cast<int>(std::lround(gx)), 0, static_cast<int>(global_field_size_.x) - 1),
			std::clamp(static_cast<int>(std::lround(gy)), 0, static_cast<int>(global_field_size_.y) - 1)
		};
	}

	std::vector<PlanetTerrain::FieldSample> PlanetTerrain::extract_chunk_field(const ivec2 chunk_coord) const
	{
		const auto padded_size = padded_field_size(base_chunk_settings_);
		const auto stride = chunk_sample_stride(base_chunk_settings_);
		const ivec2 chunk_base{ chunk_coord.x * stride.x, chunk_coord.y * stride.y };

		std::vector<FieldSample> field_samples(static_cast<std::size_t>(padded_size.x) * static_cast<std::size_t>(padded_size.y));
		for (std::uint32_t y = 0; y < padded_size.y; ++y)
		{
			for (std::uint32_t x = 0; x < padded_size.x; ++x)
			{
				const ivec2 global_coord{
					chunk_base.x + static_cast<int>(x),
					chunk_base.y + static_cast<int>(y)
				};
				field_samples[static_cast<std::size_t>(y) * padded_size.x + x] = global_field_[global_field_index(global_coord)];
			}
		}

		return field_samples;
	}

	void PlanetTerrain::rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks)
	{
		for (std::size_t i = 0; i < chunks_.size(); ++i)
		{
			if (!dirty_chunks[i]) continue;
			chunks_[i].rebuild_from_field(extract_chunk_field(chunks_[i].chunk_coord()));
		}
	}

	void PlanetTerrain::mark_chunks_covering_global_sample(const ivec2 coord, std::vector<bool>& dirty_chunks) const
	{
		const auto total_chunk_count = chunk_count();
		const auto padded_size = padded_field_size(base_chunk_settings_);
		const auto stride = chunk_sample_stride(base_chunk_settings_);

		const int base_x = coord.x / std::max(stride.x, 1);
		const int base_y = coord.y / std::max(stride.y, 1);

		for (int chunk_y = std::max(0, base_y - 1); chunk_y <= std::min(total_chunk_count.y - 1, base_y + 1); ++chunk_y)
		{
			for (int chunk_x = std::max(0, base_x - 1); chunk_x <= std::min(total_chunk_count.x - 1, base_x + 1); ++chunk_x)
			{
				const int local_x = coord.x - chunk_x * stride.x;
				const int local_y = coord.y - chunk_y * stride.y;
				if (local_x < 0 || local_y < 0) continue;
				if (local_x >= static_cast<int>(padded_size.x) || local_y >= static_cast<int>(padded_size.y)) continue;

				dirty_chunks[flat_index({ chunk_x, chunk_y }, total_chunk_count)] = true;
			}
		}
	}

	int PlanetTerrain::solid_neighbor_count(const ivec2 coord) const
	{
		int count = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				if (x == 0 && y == 0) continue;

				const ivec2 neighbor{ coord.x + x, coord.y + y };
				if (!is_valid_global_sample(neighbor)) continue;
				if (is_solid(global_field_[global_field_index(neighbor)])) ++count;
			}
		}

		return count;
	}

	bool PlanetTerrain::has_water_neighbor(const ivec2 coord) const
	{
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				if (x == 0 && y == 0) continue;

				const ivec2 neighbor{ coord.x + x, coord.y + y };
				if (!is_valid_global_sample(neighbor)) continue;
				if (has_water(global_field_[global_field_index(neighbor)])) return true;
			}
		}

		return false;
	}

	bool PlanetTerrain::is_dig_protected(const ivec2 coord) const
	{
		if (!is_valid_global_sample(coord)) return false;
		const auto& sample = global_field_[global_field_index(coord)];
		return has_water(sample) || has_water_neighbor(coord);
	}

	std::optional<ivec2> PlanetTerrain::find_water_anchor(const vec2 world_position) const
	{
		if (const auto existing_water = find_water_sample(world_position); existing_water.has_value())
		{
			return existing_water;
		}

		const ivec2 center = world_to_global_sample(world_position);
		static constexpr int search_radius = 8;

		std::optional<WaterCandidate> best_strong_candidate;
		std::optional<WaterCandidate> best_fallback_candidate;

		for (int y = center.y - search_radius; y <= center.y + search_radius; ++y)
		{
			for (int x = center.x - search_radius; x <= center.x + search_radius; ++x)
			{
				const ivec2 coord{ x, y };
				if (!is_valid_global_sample(coord)) continue;

				const auto& sample = global_field_[global_field_index(coord)];
				if (is_solid(sample)) continue;

				const vec2 sample_world = global_sample_world_position(coord);
				const vec2 click_delta = subtract_vec2(sample_world, world_position);
				const float click_distance_sq = click_delta.x * click_delta.x + click_delta.y * click_delta.y;
				const WaterCandidate candidate{
					coord,
					radial_distance(sample_world, base_chunk_settings_.world_center),
					click_distance_sq
				};

				const int solid_neighbors = solid_neighbor_count(coord);
				const bool strong_candidate = solid_neighbors >= 2 || has_water_neighbor(coord);
				const bool fallback_candidate = solid_neighbors >= 1;

				auto should_replace = [](const std::optional<WaterCandidate>& current, const WaterCandidate& next)
				{
					if (!current.has_value()) return true;
					if (std::abs(next.radial - current->radial) > 1e-5f) return next.radial < current->radial;
					return next.click_distance_sq < current->click_distance_sq;
				};

				if (strong_candidate && should_replace(best_strong_candidate, candidate)) best_strong_candidate = candidate;
				if (fallback_candidate && should_replace(best_fallback_candidate, candidate)) best_fallback_candidate = candidate;
			}
		}

		if (best_strong_candidate.has_value()) return best_strong_candidate->coord;
		if (best_fallback_candidate.has_value()) return best_fallback_candidate->coord;
		return std::nullopt;
	}

	std::optional<ivec2> PlanetTerrain::find_water_sample(const vec2 world_position) const
	{
		const ivec2 center = world_to_global_sample(world_position);
		static constexpr int search_radius = 8;

		std::optional<WaterCandidate> best_candidate;
		for (int y = center.y - search_radius; y <= center.y + search_radius; ++y)
		{
			for (int x = center.x - search_radius; x <= center.x + search_radius; ++x)
			{
				const ivec2 coord{ x, y };
				if (!is_valid_global_sample(coord)) continue;

				const auto& sample = global_field_[global_field_index(coord)];
				if (!has_water(sample)) continue;

				const vec2 sample_world = global_sample_world_position(coord);
				const vec2 click_delta = subtract_vec2(sample_world, world_position);
				const float click_distance_sq = click_delta.x * click_delta.x + click_delta.y * click_delta.y;

				const WaterCandidate candidate{
					coord,
					radial_distance(sample_world, base_chunk_settings_.world_center),
					click_distance_sq
				};

				if (!best_candidate.has_value() ||
					candidate.click_distance_sq < best_candidate->click_distance_sq ||
					(std::abs(candidate.click_distance_sq - best_candidate->click_distance_sq) <= 1e-5f && candidate.radial < best_candidate->radial))
				{
					best_candidate = candidate;
				}
			}
		}

		if (!best_candidate.has_value()) return std::nullopt;
		return best_candidate->coord;
	}

	std::vector<ivec2> PlanetTerrain::collect_water_component(const ivec2 start_coord) const
	{
		if (!is_valid_global_sample(start_coord)) return {};
		if (!has_water(global_field_[global_field_index(start_coord)])) return {};

		static constexpr std::array neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 }
		};

		std::queue<ivec2> frontier;
		std::unordered_set<std::uint64_t> visited;
		std::vector<ivec2> component;

		frontier.push(start_coord);
		visited.insert(sample_key(start_coord));

		while (!frontier.empty())
		{
			const auto coord = frontier.front();
			frontier.pop();
			component.push_back(coord);

			for (const auto& offset : neighbors)
			{
				const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
				if (!is_valid_global_sample(neighbor)) continue;
				if (!has_water(global_field_[global_field_index(neighbor)])) continue;

				const auto key = sample_key(neighbor);
				if (!visited.insert(key).second) continue;

				frontier.push(neighbor);
			}
		}

		return component;
	}

	bool PlanetTerrain::apply_terrain_edit_to_global_field(const TerrainEdit& edit, std::vector<bool>& dirty_chunks)
	{
		const float radius = std::max(edit.position_radius_strength.z, 0.0f);
		if (radius <= 0.0f) return false;

		const vec2 edit_center{
			edit.position_radius_strength.x,
			edit.position_radius_strength.y
		};

		if (!circle_overlaps_rect(edit_center, radius, grid_min_, grid_max_)) return false;

		const auto min_x = static_cast<int>(std::floor((edit_center.x - radius - global_field_origin_.x) / terrain_cell_size_.x));
		const auto min_y = static_cast<int>(std::floor((edit_center.y - radius - global_field_origin_.y) / terrain_cell_size_.y));
		const auto max_x = static_cast<int>(std::ceil((edit_center.x + radius - global_field_origin_.x) / terrain_cell_size_.x));
		const auto max_y = static_cast<int>(std::ceil((edit_center.y + radius - global_field_origin_.y) / terrain_cell_size_.y));

		const int clamped_min_x = std::clamp(min_x, 0, static_cast<int>(global_field_size_.x) - 1);
		const int clamped_min_y = std::clamp(min_y, 0, static_cast<int>(global_field_size_.y) - 1);
		const int clamped_max_x = std::clamp(max_x, 0, static_cast<int>(global_field_size_.x) - 1);
		const int clamped_max_y = std::clamp(max_y, 0, static_cast<int>(global_field_size_.y) - 1);

		const float signed_strength = edit.position_radius_strength.w;
		const float falloff_exponent = std::max(edit.shape.x, 0.001f);
		const bool digging = signed_strength < 0.0f;
		bool changed = false;

		for (int y = clamped_min_y; y <= clamped_max_y; ++y)
		{
			for (int x = clamped_min_x; x <= clamped_max_x; ++x)
			{
				const ivec2 coord{ x, y };
				const vec2 world = global_sample_world_position(coord);
				const vec2 delta = subtract_vec2(world, edit_center);
				const float distance_to_center = std::sqrt(delta.x * delta.x + delta.y * delta.y);
				if (distance_to_center >= radius) continue;

				if (digging && is_dig_protected(coord)) continue;

				const float normalized = 1.0f - distance_to_center / radius;
				const float falloff = std::pow(normalized, falloff_exponent);

				auto& sample = global_field_[global_field_index(coord)];
				const bool had_water = has_water(sample);
				const float next_terrain = sample.terrain + signed_strength * falloff;
				bool local_changed = std::abs(next_terrain - sample.terrain) > 1e-6f;
				sample.terrain = next_terrain;

				if (sample.terrain >= 0.0f)
				{
					const float next_water = dry_water_density(sample);
					local_changed = std::abs(next_water - sample.water) > 1e-6f || local_changed;
					sample.water = next_water;
				}
				else if (!had_water)
				{
					const float next_water = dry_water_density(sample);
					local_changed = std::abs(next_water - sample.water) > 1e-6f || local_changed;
					sample.water = next_water;
				}

				if (!local_changed) continue;

				changed = true;
				mark_chunks_covering_global_sample(coord, dirty_chunks);
			}
		}

		return changed;
	}

	bool PlanetTerrain::fill_water_basin(const ivec2 start_coord, const std::uint32_t volume_cap, std::vector<bool>& dirty_chunks)
	{
		if (!is_valid_global_sample(start_coord)) return false;
		if (is_solid(global_field_[global_field_index(start_coord)])) return false;

		static constexpr std::array neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 }
		};
		const float smoothing_margin = std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 2.5f;

		bool changed = false;
		for (const auto& coord : collect_water_component(start_coord))
		{
			auto& sample = global_field_[global_field_index(coord)];
			const float next_water = dry_water_density(sample);
			if (std::abs(sample.water - next_water) <= 1e-6f) continue;

			sample.water = next_water;
			changed = true;
			mark_chunks_covering_global_sample(coord, dirty_chunks);
		}

		std::priority_queue<WaterCandidate, std::vector<WaterCandidate>, WaterCandidateCompare> frontier;
		std::unordered_set<std::uint64_t> visited;
		std::vector<WaterCandidate> selected_samples;
		std::vector<WaterCandidate> affected_samples;

		auto push_candidate = [&](const ivec2 coord)
		{
			if (!is_valid_global_sample(coord)) return;

			const auto key = sample_key(coord);
			if (!visited.insert(key).second) return;

			const auto& sample = global_field_[global_field_index(coord)];
			if (is_solid(sample)) return;

			const vec2 sample_world = global_sample_world_position(coord);
			frontier.push(WaterCandidate{
				coord,
				radial_distance(sample_world, base_chunk_settings_.world_center),
				0.0f
			});
		};

		push_candidate(start_coord);

		while (!frontier.empty() && selected_samples.size() < volume_cap)
		{
			const auto current = frontier.top();
			frontier.pop();
			selected_samples.push_back(current);

			for (const auto& offset : neighbors)
			{
				push_candidate({ current.coord.x + offset.x, current.coord.y + offset.y });
			}
		}

		if (selected_samples.empty()) return changed;

		const float highest_selected = std::ranges::max(selected_samples, {}, &WaterCandidate::radial).radial;
		const float next_unselected = frontier.empty() ?
			highest_selected + std::min(terrain_cell_size_.x, terrain_cell_size_.y) :
			frontier.top().radial;
		const float surface_level = 0.5f * (highest_selected + next_unselected);

		affected_samples = selected_samples;
		while (!frontier.empty() && frontier.top().radial <= surface_level + smoothing_margin)
		{
			affected_samples.push_back(frontier.top());
			frontier.pop();
		}

		for (const auto& selected : affected_samples)
		{
			auto& sample = global_field_[global_field_index(selected.coord)];
			const float next_water = surface_level - selected.radial;
			if (std::abs(sample.water - next_water) <= 1e-6f) continue;

			sample.water = next_water;
			changed = true;
			mark_chunks_covering_global_sample(selected.coord, dirty_chunks);
		}

		return changed;
	}

	bool PlanetTerrain::remove_water_volume(const ivec2 start_coord, const std::uint32_t volume_cap, std::vector<bool>& dirty_chunks)
	{
		auto component = collect_water_component(start_coord);
		if (component.empty()) return false;

		std::ranges::sort(component, [this](const ivec2& lhs, const ivec2& rhs)
		{
			const float lhs_radial = radial_distance(global_sample_world_position(lhs), base_chunk_settings_.world_center);
			const float rhs_radial = radial_distance(global_sample_world_position(rhs), base_chunk_settings_.world_center);
			if (std::abs(lhs_radial - rhs_radial) > 1e-5f) return lhs_radial > rhs_radial;
			return lhs.y != rhs.y ? lhs.y < rhs.y : lhs.x < rhs.x;
		});

		bool changed = false;
		const auto remove_count = std::min<std::size_t>(component.size(), volume_cap);
		for (std::size_t i = 0; i < remove_count; ++i)
		{
			auto& sample = global_field_[global_field_index(component[i])];
			if (!has_water(sample)) continue;

			sample.water = dry_water_density(sample);
			changed = true;
			mark_chunks_covering_global_sample(component[i], dirty_chunks);
		}

		return changed;
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
