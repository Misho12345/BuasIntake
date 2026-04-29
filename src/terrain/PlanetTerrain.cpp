#include "pch.hpp"
#include "PlanetTerrain.hpp"

#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
	namespace
	{
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

		float wetness_strength(const float distance, const float max_distance)
		{
			if (max_distance <= 1e-6f || distance >= max_distance) return 0.0f;

			const float saturated_band = max_distance * 0.25f;
			if (distance <= saturated_band) return 1.0f;

			const float normalized = std::clamp(
				1.0f - (distance - saturated_band) / std::max(max_distance - saturated_band, 1e-6f),
				0.0f,
				1.0f);
			return normalized * normalized * (3.0f - 2.0f * normalized);
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

		float radial_depth_at(const vec2 point, const vec2 center)
		{
			return radial_distance(point, center);
		}

		float plant_density_radius_for(const PlanetTerrain::PlantFamily family)
		{
			switch (family)
			{
				case PlanetTerrain::PlantFamily::Grass: return 6.0f;
				case PlanetTerrain::PlantFamily::Flowers: return 7.0f;
				case PlanetTerrain::PlantFamily::Bush: return 9.0f;
				case PlanetTerrain::PlantFamily::Tree: return 18.0f;
			}

			return 6.0f;
		}

		std::uint32_t plant_density_cap_for(const PlanetTerrain::PlantFamily family)
		{
			switch (family)
			{
				case PlanetTerrain::PlantFamily::Grass: return 5u;
				case PlanetTerrain::PlantFamily::Flowers: return 3u;
				case PlanetTerrain::PlantFamily::Bush: return 2u;
				case PlanetTerrain::PlantFamily::Tree: return 1u;
			}

			return 2u;
		}

		float hash01(const float x, const float y, const std::uint32_t seed)
		{
			const float value = std::sin(x * 12.9898f + y * 78.233f + static_cast<float>(seed) * 0.013f) * 43758.5453f;
			return value - std::floor(value);
		}

		float smooth01(const float value)
		{
			const float t = std::clamp(value, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		float value_noise(const vec2 point, const std::uint32_t seed)
		{
			const float ix = std::floor(point.x);
			const float iy = std::floor(point.y);
			const float fx = smooth01(point.x - ix);
			const float fy = smooth01(point.y - iy);

			const float a = hash01(ix, iy, seed);
			const float b = hash01(ix + 1.0f, iy, seed);
			const float c = hash01(ix, iy + 1.0f, seed);
			const float d = hash01(ix + 1.0f, iy + 1.0f, seed);
			return std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
		}

		vec2 perlin_gradient(const int x, const int y, const std::uint32_t seed)
		{
			const float angle = hash01(static_cast<float>(x), static_cast<float>(y), seed) * tau;
			return { std::cos(angle), std::sin(angle) };
		}

		float perlin_noise(const vec2 point, const std::uint32_t seed)
		{
			const int x0 = static_cast<int>(std::floor(point.x));
			const int y0 = static_cast<int>(std::floor(point.y));
			const int x1 = x0 + 1;
			const int y1 = y0 + 1;

			const float fx = smooth01(point.x - static_cast<float>(x0));
			const float fy = smooth01(point.y - static_cast<float>(y0));

			const auto g00 = perlin_gradient(x0, y0, seed);
			const auto g10 = perlin_gradient(x1, y0, seed);
			const auto g01 = perlin_gradient(x0, y1, seed);
			const auto g11 = perlin_gradient(x1, y1, seed);

			const vec2 d00{ point.x - static_cast<float>(x0), point.y - static_cast<float>(y0) };
			const vec2 d10{ point.x - static_cast<float>(x1), point.y - static_cast<float>(y0) };
			const vec2 d01{ point.x - static_cast<float>(x0), point.y - static_cast<float>(y1) };
			const vec2 d11{ point.x - static_cast<float>(x1), point.y - static_cast<float>(y1) };

			const float n00 = g00.x * d00.x + g00.y * d00.y;
			const float n10 = g10.x * d10.x + g10.y * d10.y;
			const float n01 = g01.x * d01.x + g01.y * d01.y;
			const float n11 = g11.x * d11.x + g11.y * d11.y;

			return std::lerp(std::lerp(n00, n10, fx), std::lerp(n01, n11, fx), fy);
		}

		float perlin_fbm(vec2 point, const std::uint32_t seed)
		{
			float value = 0.0f;
			float amplitude = 0.5f;

			for (int i = 0; i < 5; ++i)
			{
				value += perlin_noise(point, seed + static_cast<std::uint32_t>(i * 131u)) * amplitude;
				point = { point.x * 2.04f - 4.8f, point.y * 2.04f + 9.2f };
				amplitude *= 0.5f;
			}

			return value;
		}

		float fbm_noise(vec2 point, const std::uint32_t seed)
		{
			float value = 0.0f;
			float amplitude = 0.5f;

			for (int i = 0; i < 5; ++i)
			{
				value += value_noise(point, seed + static_cast<std::uint32_t>(i * 101u)) * amplitude;
				point = { point.x * 2.03f + 7.1f, point.y * 2.03f - 5.4f };
				amplitude *= 0.5f;
			}

			return value;
		}

		bool is_exposed_to_air(const PlanetTerrain::FieldSample& sample, const int solid_neighbors)
		{
			return is_solid(sample) && solid_neighbors >= 2 && solid_neighbors < 8;
		}

		sf::IntRect tile_rect(const std::uint8_t column, const std::uint8_t row, const int tile_size)
		{
			return {
				{ static_cast<int>(column) * tile_size, static_cast<int>(row) * tile_size },
				{ tile_size, tile_size }
			};
		}

		sf::Text make_counter_text(const sf::Font& font, const std::uint32_t value)
		{
			sf::Text text{ font, std::format("{}", value), 18u };
			text.setFillColor(sf::Color(238, 244, 229, 255));
			text.setOutlineColor(sf::Color(8, 12, 16, 220));
			text.setOutlineThickness(1.5f);
			return text;
		}

		struct WaterCandidate final
		{
			ivec2 coord{ 0, 0 };
			float radial{ 0.0f };
			float click_distance_sq{ 0.0f };
		};

		struct PlantCandidate final
		{
			ivec2 coord{ 0, 0 };
			float anchor_distance_sq{ 0.0f };
			float radial{ 0.0f };
		};

		struct WaterCandidateCompare final
		{
			bool operator()(const WaterCandidate& lhs, const WaterCandidate& rhs) const
			{
				if (std::abs(lhs.radial - rhs.radial) > 1e-5f) return lhs.radial > rhs.radial;
				return lhs.click_distance_sq > rhs.click_distance_sq;
			}
		};

		struct WetnessNode final
		{
			ivec2 coord{ 0, 0 };
			float distance{ 0.0f };
		};

		struct WetnessNodeCompare final
		{
			bool operator()(const WetnessNode& lhs, const WetnessNode& rhs) const
			{
				return lhs.distance > rhs.distance;
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
		load_overlay_assets();

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

		base_chunk_settings_.planet_radius = planet_radius;

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

	void PlanetTerrain::draw_overlays(sf::RenderTarget& target, const sf::View& view) const
	{
		if (!overlay_assets_ready_) return;
		draw_resource_overlays(target, view);
		draw_plant_overlays(target, view);
	}

	void PlanetTerrain::draw_resource_overlays(sf::RenderTarget& target, const sf::View& view) const
	{
		const vec2 view_center{ view.getCenter().x, view.getCenter().y };
		const vec2 view_size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
		const float visible_radius = std::sqrt(view_size.x * view_size.x + view_size.y * view_size.y) * 0.5f + 4.0f;
		const float visible_radius_sq = visible_radius * visible_radius;
		int resource_lod_stride = 1;
		const float max_view_extent = std::max(view_size.x, view_size.y);
		if (max_view_extent > 150.0f) resource_lod_stride = 8;
		else if (max_view_extent > 115.0f) resource_lod_stride = 5;
		else if (max_view_extent > 85.0f) resource_lod_stride = 3;

		for (const auto& resource : resource_nodes_)
		{
			if (!is_valid_global_sample(resource.coord)) continue;
			const auto& sample = global_field_[global_field_index(resource.coord)];
			if (!is_solid(sample)) continue;
			if (resource_lod_stride > 1)
			{
				const std::uint32_t lod_hash =
					static_cast<std::uint32_t>(resource.coord.x * 73856093) ^
					static_cast<std::uint32_t>(resource.coord.y * 19349663);
				if (lod_hash % static_cast<std::uint32_t>(resource_lod_stride) != 0u) continue;
			}

			const vec2 world_position = global_sample_world_position(resource.coord);
			const vec2 delta = subtract_vec2(world_position, view_center);
			if (delta.x * delta.x + delta.y * delta.y > visible_radius_sq) continue;
			if (resource.kind == ResourceKind::DeadPlant)
			{
				draw_radial_sprite(
					target,
					dead_plant_texture_,
					tile_rect(7u, resource.variant % 16u, 32),
					world_position,
					1.75f,
					0.0f,
					0.25f);
				continue;
			}

			const sf::Texture* texture = &rock_node_texture_;
			if (resource.kind == ResourceKind::IronOre) texture = &iron_ore_texture_;
			else if (resource.kind == ResourceKind::BronzeOre) texture = &bronze_ore_texture_;
			else if (resource.kind == ResourceKind::GoldOre) texture = &gold_ore_texture_;
			else if (resource.kind == ResourceKind::DiamondOre) texture = &diamond_ore_texture_;

			const std::uint8_t row = resource.cave_variant ? resource.variant % 16u : static_cast<std::uint8_t>(16u + resource.variant % 16u);
			const std::uint8_t column = resource.cave_variant ? 7u : 0u;
			draw_radial_sprite(target, *texture, tile_rect(column, row, 32), world_position, 1.55f, 0.0f, 0.25f);
		}
	}

	void PlanetTerrain::draw_plant_overlays(sf::RenderTarget& target, const sf::View& view) const
	{
		const vec2 view_center{ view.getCenter().x, view.getCenter().y };
		const vec2 view_size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
		const float visible_radius = std::sqrt(view_size.x * view_size.x + view_size.y * view_size.y) * 0.5f + 4.0f;
		const float visible_radius_sq = visible_radius * visible_radius;

		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Empty) continue;

			const ivec2 coord{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			if (!is_valid_global_sample(coord)) continue;
			if (!is_solid(global_field_[global_field_index(coord)])) continue;

			const vec2 world_position = global_sample_world_position(coord);
			const vec2 delta = subtract_vec2(world_position, view_center);
			if (delta.x * delta.x + delta.y * delta.y > visible_radius_sq) continue;

			const auto spec = plant_visual_spec(plant);
			if (spec.texture == nullptr) continue;

			const vec2 anchor_world = plant.anchor_world.lengthSquared() > 1e-6f ? plant.anchor_world : world_position;
			draw_radial_sprite(
				target,
				*spec.texture,
				tile_rect(spec.column, plant.variant % 16u, spec.tile_size),
				anchor_world,
				spec.height,
				spec.angle_offset);
		}
	}

	void PlanetTerrain::draw_radial_sprite(
		sf::RenderTarget& target,
		const sf::Texture& texture,
		const sf::IntRect texture_rect,
		const vec2 world_position,
		const float world_height,
		const float angle_offset,
		const float radial_offset) const
	{
		sf::Sprite sprite{ texture, texture_rect };
		const auto bounds = sprite.getLocalBounds();
		const sf::Vector2f bottom_center{ bounds.position.x + bounds.size.x * 0.5f, bounds.position.y + bounds.size.y };
		sprite.setOrigin(bottom_center);
		const float scale = world_height / std::max(bounds.size.y, 1.0f);
		sprite.setScale({ scale, scale });

		const vec2 up = normalize_vec2(subtract_vec2(world_position, base_chunk_settings_.world_center));
		sprite.setRotation(sf::radians(angle_from_up_direction(up) + angle_offset));
		sprite.setPosition({
			world_position.x + up.x * radial_offset,
			world_position.y + up.y * radial_offset
		});
		target.draw(sprite);
	}

	PlanetTerrain::PlantVisualSpec PlanetTerrain::plant_visual_spec(const PlantSample& plant) const
	{
		PlantVisualSpec spec{
			.texture = &live_plant_texture_
		};

		const float sprout_fraction = std::clamp((plant.age - 2.0f) / 2.0f, 0.0f, 1.0f);
		if (plant.stage == PlantStage::Sprout)
		{
			spec.column = static_cast<std::uint8_t>(2u + std::min(3, static_cast<int>(std::floor(sprout_fraction * 4.0f))));
			switch (plant.family)
			{
				case PlantFamily::Grass:
					spec.texture = &grass_plant_texture_;
					spec.height = 0.9f;
					break;
				case PlantFamily::Flowers:
					spec.texture = &flowers_plant_texture_;
					spec.height = 1.0f;
					break;
				case PlantFamily::Bush:
					spec.texture = &bushes_plant_texture_;
					spec.height = 1.4f;
					break;
				case PlantFamily::Tree:
					spec.texture = &trees_plant_texture_;
					spec.tile_size = 64;
					spec.height = 2.6f;
					break;
			}

			return spec;
		}

		if (plant.stage == PlantStage::Mature)
		{
			spec.column = 7u;
			switch (plant.family)
			{
				case PlantFamily::Grass:
					spec.texture = &grass_plant_texture_;
					spec.height = 1.35f;
					break;
				case PlantFamily::Flowers:
					spec.texture = &flowers_plant_texture_;
					spec.height = 1.55f;
					break;
				case PlantFamily::Bush:
					spec.texture = &bushes_plant_texture_;
					spec.height = 2.4f;
					break;
				case PlantFamily::Tree:
					spec.texture = &trees_plant_texture_;
					spec.tile_size = 64;
					spec.height = 6.8f;
					break;
			}
		}

		return spec;
	}

	void PlanetTerrain::draw_resource_ui(sf::RenderTarget& target) const
	{
		if (!overlay_assets_ready_) return;

		const auto target_size = target.getSize();
		const float panel_width = 260.0f;
		const float panel_height = 48.0f;
		const sf::Vector2f panel_position{ static_cast<float>(target_size.x) - panel_width - 22.0f, 22.0f };

		sf::RectangleShape panel{ { panel_width, panel_height } };
		panel.setPosition(panel_position);
		panel.setFillColor(sf::Color(13, 20, 28, 228));
		panel.setOutlineColor(sf::Color(63, 82, 97, 236));
		panel.setOutlineThickness(2.0f);
		target.draw(panel);

		struct CounterEntry final
		{
			sf::IntRect icon{};
			std::uint32_t value{ 0u };
			bool seed{ false };
		};

		const std::array entries{
			CounterEntry{ tile_rect(0u, 0u, 64), inventory_.rocks, false },
			CounterEntry{ tile_rect(2u, 0u, 64), inventory_.ingots, false },
			CounterEntry{ tile_rect(4u, 0u, 64), inventory_.diamonds, false },
			CounterEntry{ tile_rect(7u, 0u, 32), inventory_.seeds, true }
		};

		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			const sf::Vector2f icon_position{ panel_position.x + 14.0f + static_cast<float>(i) * 61.0f, panel_position.y + 9.0f };
			const auto& texture = entries[i].seed ? live_plant_texture_ : processed_resource_texture_;
			sf::Sprite icon{ texture, entries[i].icon };
			const auto icon_bounds = icon.getLocalBounds();
			icon.setOrigin(icon_bounds.getCenter());
			icon.setScale({ 0.42f, 0.42f });
			icon.setPosition({ icon_position.x + 14.0f, icon_position.y + 15.0f });
			target.draw(icon);

			auto text = make_counter_text(ui_font_, entries[i].value);
			text.setPosition({ icon_position.x + 25.0f, icon_position.y + 5.0f });
			target.draw(text);
		}
	}

	void PlanetTerrain::update(const float dt)
	{
		flush_pending_ground_brush_changes();
		advance_plants(dt);
	}

	void PlanetTerrain::flush_pending_ground_brush_changes()
	{
		if (pending_ground_brush_changed_coords_.empty() || pending_ground_brush_dirty_chunks_.empty()) return;

		if (pending_ground_brush_requires_wetness_rebuild_)
		{
			recompute_wetness_around(pending_ground_brush_changed_coords_, pending_ground_brush_dirty_chunks_);
		}

		rebuild_dirty_chunks(pending_ground_brush_dirty_chunks_);
		pending_ground_brush_changed_coords_.clear();
		std::fill(pending_ground_brush_dirty_chunks_.begin(), pending_ground_brush_dirty_chunks_.end(), false);
		pending_ground_brush_requires_wetness_rebuild_ = false;
	}

	void PlanetTerrain::advance_plants(const float dt)
	{
		static constexpr float seed_to_sprout_time = 2.0f;
		static constexpr float sprout_to_mature_time = 4.0f;

		if (plant_samples_.empty() || dt <= 0.0f) return;
		if (active_plants_dirty_) compact_active_plants();

		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Empty) continue;

			plant.age += dt;
			if (plant.stage == PlantStage::Mature)
			{
				plant.spread_age += dt;
				continue;
			}

			if (plant.stage == PlantStage::Seeded && plant.age >= seed_to_sprout_time)
			{
				plant.stage = PlantStage::Sprout;
			}
			else if (plant.stage == PlantStage::Sprout && plant.age >= sprout_to_mature_time)
			{
				plant.stage = PlantStage::Mature;
			}
		}

		spread_plants();
	}

	bool PlanetTerrain::is_surface_exposed_sample(const ivec2 coord) const
	{
		const auto anchor = surface_anchor_world(coord);
		if (!anchor.has_value()) return false;
		if (normalized_depth(*anchor) > 0.12f) return false;
		return is_surface_exposed_world(*anchor, std::min(base_chunk_settings_.chunk_size.x, base_chunk_settings_.chunk_size.y) * 0.22f);
	}

	bool PlanetTerrain::is_surface_exposed_world(const vec2 world_position, const float clearance_distance) const
	{
		if (normalized_depth(world_position) > 0.12f) return false;
		const vec2 up = normalize_vec2(subtract_vec2(world_position, base_chunk_settings_.world_center));
		const float step_size = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 0.35f, 0.05f);
		const vec2 start = add_vec2(world_position, scale_vec2(up, step_size));

		for (float distance = 0.0f; distance <= clearance_distance; distance += step_size)
		{
			const vec2 probe_world = add_vec2(start, scale_vec2(up, distance));
			const auto probe_coord = world_to_global_sample(probe_world);
			if (!is_valid_global_sample(probe_coord)) return true;
			if (is_solid(global_field_[global_field_index(probe_coord)])) return false;
		}

		return true;
	}

	std::optional<vec2> PlanetTerrain::surface_anchor_world(const ivec2 coord) const
	{
		if (!is_valid_global_sample(coord)) return std::nullopt;
		if (!is_solid(global_field_[global_field_index(coord)])) return std::nullopt;

		const vec2 center = global_sample_world_position(coord);
		const vec2 up = normalize_vec2(subtract_vec2(center, base_chunk_settings_.world_center));
		const float step_size = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 0.12f, 0.02f);
		const float max_distance = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 3.0f, 0.35f);

		vec2 last_solid = center;
		for (float distance = step_size; distance <= max_distance; distance += step_size)
		{
			const vec2 probe = add_vec2(center, scale_vec2(up, distance));
			const auto probe_coord = world_to_global_sample(probe);
			if (!is_valid_global_sample(probe_coord))
			{
				vec2 low = last_solid;
				vec2 high = probe;
				for (int iteration = 0; iteration < 8; ++iteration)
				{
					const vec2 mid = lerp_vec2(low, high, 0.5f);
					const auto mid_coord = world_to_global_sample(mid);
					if (is_valid_global_sample(mid_coord) && is_solid(global_field_[global_field_index(mid_coord)])) low = mid;
					else high = mid;
				}
				return lerp_vec2(low, high, 0.5f);
			}
			if (!is_solid(global_field_[global_field_index(probe_coord)]))
			{
				vec2 low = last_solid;
				vec2 high = probe;
				for (int iteration = 0; iteration < 8; ++iteration)
				{
					const vec2 mid = lerp_vec2(low, high, 0.5f);
					const auto mid_coord = world_to_global_sample(mid);
					if (is_valid_global_sample(mid_coord) && is_solid(global_field_[global_field_index(mid_coord)])) low = mid;
					else high = mid;
				}
				return lerp_vec2(low, high, 0.5f);
			}
			last_solid = probe;
		}

		return std::nullopt;
	}

	float PlanetTerrain::surface_alignment_at(const ivec2 coord) const
	{
		const auto anchor = surface_anchor_world(coord);
		if (!anchor.has_value()) return 0.0f;

		const vec2 up = normalize_vec2(subtract_vec2(*anchor, base_chunk_settings_.world_center));
		const vec2 tangent{ up.y, -up.x };
		const float tangent_step = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 2.0f, 0.12f);

		auto sample_anchor = [this](const vec2 point) -> std::optional<vec2>
		{
			return surface_anchor_world(world_to_global_sample(point));
		};

		auto left_anchor = sample_anchor(add_vec2(*anchor, scale_vec2(tangent, -tangent_step)));
		auto right_anchor = sample_anchor(add_vec2(*anchor, scale_vec2(tangent, tangent_step)));
		if (!left_anchor.has_value() || !right_anchor.has_value()) return 0.0f;

		const vec2 tangent_vector = subtract_vec2(*right_anchor, *left_anchor);
		if (tangent_vector.lengthSquared() <= 1e-6f) return 1.0f;

		vec2 normal = normalize_vec2(vec2{ -tangent_vector.y, tangent_vector.x }, up);
		if (normal.dot(up) < 0.0f) normal = scale_vec2(normal, -1.0f);
		return std::clamp(normal.dot(up), 0.0f, 1.0f);
	}

	bool PlanetTerrain::is_surface_suitable_for_plant(const ivec2 coord) const
	{
		if (!is_surface_exposed_sample(coord)) return false;
		return surface_alignment_at(coord) >= 0.93f;
	}

	bool PlanetTerrain::has_resource_at(const ivec2 coord) const
	{
		return std::ranges::any_of(resource_nodes_, [coord](const ResourceNode& node)
		{
			return node.coord.x == coord.x && node.coord.y == coord.y;
		});
	}

	std::uint32_t PlanetTerrain::nearby_plant_count(const ivec2 coord, const float radius_samples) const
	{
		const float radius_sq = radius_samples * radius_samples;
		std::uint32_t count = 0u;
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			if (plant_samples_[index].stage == PlantStage::Empty) continue;
			const ivec2 other_coord{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			const float dx = static_cast<float>(other_coord.x - coord.x);
			const float dy = static_cast<float>(other_coord.y - coord.y);
			if (dx * dx + dy * dy <= radius_sq) ++count;
		}
		return count;
	}

	std::uint32_t PlanetTerrain::mature_tree_count() const
	{
		std::uint32_t count = 0u;
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Mature && plant.family == PlantFamily::Tree) ++count;
		}
		return count;
	}

	bool PlanetTerrain::can_place_tree_near(const ivec2 coord, const int min_spacing_samples) const
	{
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage != PlantStage::Mature || plant.family != PlantFamily::Tree) continue;

			const ivec2 other_coord{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			const int dx = other_coord.x - coord.x;
			const int dy = other_coord.y - coord.y;
			if (dx * dx + dy * dy < min_spacing_samples * min_spacing_samples) return false;
		}

		return true;
	}

	std::optional<ivec2> PlanetTerrain::find_plantable_seed_coord(const vec2 world_position) const
	{
		if (global_field_.empty() || plant_samples_.empty()) return std::nullopt;

		const ivec2 center = world_to_global_sample(world_position);
		const float min_cell_size = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y), 0.01f);
		const int search_radius = std::clamp(static_cast<int>(std::ceil(1.25f / min_cell_size)), 3, 8);

		std::optional<PlantCandidate> best_candidate;
		for (int y = center.y - search_radius; y <= center.y + search_radius; ++y)
		{
			for (int x = center.x - search_radius; x <= center.x + search_radius; ++x)
			{
				const ivec2 coord{ x, y };
				if (!is_valid_global_sample(coord)) continue;

				const auto index = global_field_index(coord);
				const auto& sample = global_field_[index];
				if (!is_solid(sample) || has_water(sample) || sample.wetness < seed_plantable_wetness_threshold) continue;
				if (plant_samples_[index].stage != PlantStage::Empty) continue;
				if (has_resource_at(coord)) continue;
				if (!is_surface_suitable_for_plant(coord)) continue;

				const auto family = choose_plant_family(coord);
				if (nearby_plant_count(coord, plant_density_radius_for(family)) >=
					plant_density_cap_for(family)) continue;

				const auto anchor = surface_anchor_world(coord);
				if (!anchor.has_value()) continue;

				const vec2 anchor_delta = subtract_vec2(*anchor, world_position);
				const PlantCandidate candidate{
					.coord = coord,
					.anchor_distance_sq = anchor_delta.x * anchor_delta.x + anchor_delta.y * anchor_delta.y,
					.radial = radial_distance(*anchor, base_chunk_settings_.world_center)
				};

				if (!best_candidate.has_value() ||
					candidate.anchor_distance_sq < best_candidate->anchor_distance_sq ||
					(std::abs(candidate.anchor_distance_sq - best_candidate->anchor_distance_sq) <= 1e-5f &&
						candidate.radial > best_candidate->radial))
				{
					best_candidate = candidate;
				}
			}
		}

		if (!best_candidate.has_value()) return std::nullopt;
		return best_candidate->coord;
	}

	PlanetTerrain::PlantFamily PlanetTerrain::choose_plant_family(const ivec2 coord) const
	{
		const float roll = hash01(static_cast<float>(coord.x), static_cast<float>(coord.y), base_chunk_settings_.seed + 6001u);
		const std::uint32_t active_count = static_cast<std::uint32_t>(std::max<std::size_t>(active_plant_indices_.size(), 1u));
		const float tree_ratio = static_cast<float>(mature_tree_count()) / static_cast<float>(active_count);

		if (roll > 0.982f && tree_ratio < 0.05f && can_place_tree_near(coord, 28)) return PlantFamily::Tree;
		if (roll > 0.84f) return PlantFamily::Bush;
		if (roll > 0.70f) return PlantFamily::Flowers;
		return PlantFamily::Grass;
	}

	std::uint8_t PlanetTerrain::choose_plant_variant(const ivec2 coord, const PlantFamily family) const
	{
		const float random_value = hash01(static_cast<float>(coord.x), static_cast<float>(coord.y), base_chunk_settings_.seed + 911u);
		const std::uint8_t base_variant = static_cast<std::uint8_t>(std::clamp(static_cast<int>(random_value * 16.0f), 0, 15));
		if (family != PlantFamily::Tree) return base_variant;

		static constexpr std::array<std::uint8_t, 4> tree_variants{ 1u, 2u, 4u, 7u };
		return tree_variants[base_variant % tree_variants.size()];
	}

	void PlanetTerrain::spread_plants()
	{
		if (active_plant_indices_.empty()) return;

		struct NewPlant final
		{
			std::size_t index{ 0u };
			PlantSample plant{};
		};

		std::vector<NewPlant> spawned_plants;
		spawned_plants.reserve(16u);

		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			auto& plant = plant_samples_[index];
			const float spread_interval =
				plant.family == PlantFamily::Tree ? 36.0f :
				plant.family == PlantFamily::Bush ? 20.0f :
				plant.family == PlantFamily::Flowers ? 14.0f : 10.0f;
			if (plant.stage != PlantStage::Mature || plant.spread_age < spread_interval) continue;
			plant.spread_age = 0.0f;

			const ivec2 origin{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			const int radius = plant.family == PlantFamily::Tree ? 8 : plant.family == PlantFamily::Bush ? 6 : 4;
			const int start_offset = static_cast<int>(hash01(static_cast<float>(origin.x), static_cast<float>(origin.y), base_chunk_settings_.seed + 7103u) * 32.0f);

			static constexpr std::array offsets{
				ivec2{ 1, 0 }, ivec2{ 2, 0 }, ivec2{ 3, 1 }, ivec2{ 2, 2 }, ivec2{ 1, 3 }, ivec2{ 0, 2 },
				ivec2{ -1, 3 }, ivec2{ -2, 2 }, ivec2{ -3, 1 }, ivec2{ -2, 0 }, ivec2{ -3, -1 }, ivec2{ -2, -2 },
				ivec2{ -1, -3 }, ivec2{ 0, -2 }, ivec2{ 1, -3 }, ivec2{ 2, -2 }, ivec2{ 3, -1 }, ivec2{ 4, 1 },
				ivec2{ 4, -1 }, ivec2{ -4, 1 }, ivec2{ -4, -1 }, ivec2{ 1, 4 }, ivec2{ -1, 4 }, ivec2{ 1, -4 },
				ivec2{ -1, -4 }, ivec2{ 5, 0 }, ivec2{ -5, 0 }, ivec2{ 0, 5 }, ivec2{ 0, -5 }, ivec2{ 6, 2 },
				ivec2{ -6, 2 }, ivec2{ 2, -6 }
			};

			for (std::size_t attempt = 0; attempt < offsets.size(); ++attempt)
			{
				const auto offset = offsets[(attempt + start_offset) % offsets.size()];
				if (offset.x * offset.x + offset.y * offset.y > radius * radius) continue;

				const ivec2 coord{ origin.x + offset.x, origin.y + offset.y };
				if (!is_valid_global_sample(coord)) continue;
				if (!is_surface_suitable_for_plant(coord)) continue;
				const auto target_index = global_field_index(coord);
				const auto& sample = global_field_[target_index];
				if (!is_solid(sample) || has_water(sample) || sample.wetness < seed_plantable_wetness_threshold) continue;
				if (plant_samples_[target_index].stage != PlantStage::Empty) continue;
				if (has_resource_at(coord)) continue;

				const auto family = choose_plant_family(coord);
				if (family == PlantFamily::Tree && !can_place_tree_near(coord, 28)) continue;
				if (nearby_plant_count(coord, plant_density_radius_for(family)) >=
					plant_density_cap_for(family)) continue;

				const auto variant = choose_plant_variant(coord, family);
				const auto anchor = surface_anchor_world(coord);
				if (!anchor.has_value()) continue;
				spawned_plants.push_back({
					.index = target_index,
					.plant = {
						.stage = PlantStage::Seeded,
						.family = family,
						.age = 0.0f,
						.spread_age = 0.0f,
						.variant = variant,
						.anchor_world = *anchor
					}
				});
				break;
			}
		}

		for (const auto& spawned : spawned_plants)
		{
			if (spawned.index >= plant_samples_.size()) continue;
			if (plant_samples_[spawned.index].stage != PlantStage::Empty) continue;
			plant_samples_[spawned.index] = spawned.plant;
			active_plant_indices_.push_back(spawned.index);
		}
	}

	bool PlanetTerrain::try_harvest_resource(const vec2 world_position)
	{
		if (resource_nodes_.empty()) return false;

		const float harvest_radius_sq = 1.75f * 1.75f;
		std::optional<std::size_t> best_index;
		float best_distance_sq = harvest_radius_sq;

		for (std::size_t i = 0; i < resource_nodes_.size(); ++i)
		{
			const auto& resource = resource_nodes_[i];
			if (!is_valid_global_sample(resource.coord)) continue;
			const auto& sample = global_field_[global_field_index(resource.coord)];
			if (!is_solid(sample)) continue;

			const vec2 delta = subtract_vec2(global_sample_world_position(resource.coord), world_position);
			const float distance_sq = delta.x * delta.x + delta.y * delta.y;
			if (distance_sq >= best_distance_sq) continue;

			best_index = i;
			best_distance_sq = distance_sq;
		}

		if (!best_index.has_value()) return false;

		const auto kind = resource_nodes_[*best_index].kind;
		switch (kind)
		{
			case ResourceKind::Rock:
				++inventory_.rocks;
				break;
			case ResourceKind::DiamondOre:
				++inventory_.diamonds;
				break;
			case ResourceKind::DeadPlant:
				inventory_.seeds += 2u;
				break;
			case ResourceKind::IronOre:
			case ResourceKind::BronzeOre:
			case ResourceKind::GoldOre:
				++inventory_.ingots;
				break;
		}

		resource_nodes_.erase(resource_nodes_.begin() + static_cast<std::ptrdiff_t>(*best_index));
		return true;
	}

	bool PlanetTerrain::plant_seed(const vec2 world_position)
	{
		if (inventory_.seeds == 0u || global_field_.empty() || plant_samples_.empty()) return false;

		const auto seed_coord = find_plantable_seed_coord(world_position);
		if (!seed_coord.has_value()) return false;

		const auto coord = *seed_coord;
		const auto index = global_field_index(coord);
		const auto family = choose_plant_family(coord);
		const auto variant = choose_plant_variant(coord, family);
		const auto anchor = surface_anchor_world(coord);
		if (!anchor.has_value()) return false;
		plant_samples_[index] = {
			.stage = PlantStage::Seeded,
			.family = family,
			.age = 0.0f,
			.spread_age = 0.0f,
			.variant = variant,
			.anchor_world = *anchor
		};
		active_plant_indices_.push_back(index);
		--inventory_.seeds;
		return true;
	}

	void PlanetTerrain::queue_edit(const TerrainEdit& edit)
	{
		pending_edits_.push_back(edit);
	}

	void PlanetTerrain::apply_pending_edits()
	{
		if (pending_edits_.empty() || global_field_.empty()) return;

		std::vector<bool> dirty_chunks(chunks_.size(), false);
		std::vector<ivec2> changed_coords;
		bool any_changes = false;
		for (const auto& edit : pending_edits_)
		{
			any_changes = apply_terrain_edit_to_global_field(edit, dirty_chunks, changed_coords).changed || any_changes;
		}

		pending_edits_.clear();
		if (any_changes)
		{
			recompute_wetness_around(changed_coords, dirty_chunks);
			rebuild_dirty_chunks(dirty_chunks);
		}
	}

	std::uint32_t PlanetTerrain::apply_ground_brush(const TerrainEdit& edit, const std::uint32_t unit_budget)
	{
		if (global_field_.empty() || unit_budget == 0u) return 0u;
		if (pending_ground_brush_dirty_chunks_.empty()) pending_ground_brush_dirty_chunks_.assign(chunks_.size(), false);

		std::vector<ivec2> changed_coords;
		const auto result = apply_terrain_edit_to_global_field(edit, pending_ground_brush_dirty_chunks_, changed_coords, unit_budget);
		if (result.changed)
		{
			pending_ground_brush_changed_coords_.insert(
				pending_ground_brush_changed_coords_.end(),
				changed_coords.begin(),
				changed_coords.end());
			pending_ground_brush_requires_wetness_rebuild_ =
				pending_ground_brush_requires_wetness_rebuild_ || result.changed;
		}

		return result.units;
	}

	std::uint32_t PlanetTerrain::place_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		std::uint32_t existing_volume = 0u;
		const auto plan = build_targeted_water_plan(world_position, volume_cap, false, &existing_volume);
		if (!plan.has_value()) return 0u;

		static_cast<void>(apply_water_plan_and_rebuild(*plan));

		return plan->wet_sample_count > existing_volume ? plan->wet_sample_count - existing_volume : 0u;
	}

	std::uint32_t PlanetTerrain::pickup_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		std::uint32_t existing_volume = 0u;
		const auto plan = build_targeted_water_plan(world_position, volume_cap, true, &existing_volume);
		if (!plan.has_value()) return 0u;

		static_cast<void>(apply_water_plan_and_rebuild(*plan));

		return existing_volume > plan->wet_sample_count ? existing_volume - plan->wet_sample_count : 0u;
	}

	std::optional<PlanetTerrain::WaterPreviewMesh> PlanetTerrain::build_water_preview_mesh(const vec2 world_position,
		const std::uint32_t volume_cap) const
	{
		const auto plan = build_targeted_water_plan(world_position, volume_cap, false);
		if (!plan.has_value()) return std::nullopt;

		auto patch = build_water_preview_patch(*plan);
		if (!patch.has_value()) return std::nullopt;

		apply_water_preview_plan(*plan, *patch);
		return render_water_preview_patch(*patch);
	}

	std::optional<PlanetTerrain::WaterPreviewPatch> PlanetTerrain::build_water_preview_patch(const WaterPlan& plan) const
	{
		std::vector<ivec2> relevant_coords = plan.dried_component;
		relevant_coords.reserve(relevant_coords.size() + plan.affected_samples.size());
		for (const auto& sample : plan.affected_samples)
		{
			if (sample.water > 1e-4f) relevant_coords.push_back(sample.coord);
		}

		if (relevant_coords.empty()) return std::nullopt;

		WaterPreviewPatch patch{};
		patch.min_coord = relevant_coords.front();
		patch.max_coord = relevant_coords.front();
		for (const auto coord : relevant_coords)
		{
			patch.min_coord.x = std::min(patch.min_coord.x, coord.x);
			patch.min_coord.y = std::min(patch.min_coord.y, coord.y);
			patch.max_coord.x = std::max(patch.max_coord.x, coord.x);
			patch.max_coord.y = std::max(patch.max_coord.y, coord.y);
		}

		patch.min_coord.x = std::max(patch.min_coord.x - 1, 0);
		patch.min_coord.y = std::max(patch.min_coord.y - 1, 0);
		patch.max_coord.x = std::min(patch.max_coord.x + 1, static_cast<int>(global_field_size_.x) - 1);
		patch.max_coord.y = std::min(patch.max_coord.y + 1, static_cast<int>(global_field_size_.y) - 1);

		if (patch.max_coord.x == patch.min_coord.x)
		{
			if (patch.max_coord.x + 1 < static_cast<int>(global_field_size_.x)) ++patch.max_coord.x;
			else if (patch.min_coord.x > 0) --patch.min_coord.x;
		}

		if (patch.max_coord.y == patch.min_coord.y)
		{
			if (patch.max_coord.y + 1 < static_cast<int>(global_field_size_.y)) ++patch.max_coord.y;
			else if (patch.min_coord.y > 0) --patch.min_coord.y;
		}

		patch.size = {
			static_cast<std::uint32_t>(patch.max_coord.x - patch.min_coord.x + 1),
			static_cast<std::uint32_t>(patch.max_coord.y - patch.min_coord.y + 1)
		};
		if (patch.size.x < 2u || patch.size.y < 2u) return std::nullopt;

		const auto patch_origin = global_sample_world_position(patch.min_coord);
		const vec2 patch_world_size{
			terrain_cell_size_.x * static_cast<float>(patch.size.x - 1u),
			terrain_cell_size_.y * static_cast<float>(patch.size.y - 1u)
		};

		patch.settings.field_size = patch.size;
		patch.settings.field_padding = { 0u, 0u };
		patch.settings.chunk_coord = { 0, 0 };
		patch.settings.chunk_grid_size = { 1, 1 };
		patch.settings.chunk_size = patch_world_size;
		patch.settings.world_center = {
			patch_origin.x + patch_world_size.x * 0.5f,
			patch_origin.y + patch_world_size.y * 0.5f
		};
		patch.settings.seed = base_chunk_settings_.seed;
		patch.settings.planet_radius = base_chunk_settings_.planet_radius;

		const auto sample_count = static_cast<std::size_t>(patch.size.x) * static_cast<std::size_t>(patch.size.y);
		patch.current_samples.resize(sample_count);
		patch.future_samples.resize(sample_count);

		for (int y = patch.min_coord.y; y <= patch.max_coord.y; ++y)
		{
			for (int x = patch.min_coord.x; x <= patch.max_coord.x; ++x)
			{
				const auto index = patch_index(x - patch.min_coord.x, y - patch.min_coord.y, patch.size.x);
				const auto& sample = global_field_[global_field_index({ x, y })];
				patch.current_samples[index] = sample;
				patch.future_samples[index] = sample;
			}
		}

		return patch;
	}

	void PlanetTerrain::apply_water_preview_plan(const WaterPlan& plan, WaterPreviewPatch& patch) const
	{
		for (const auto& coord : plan.dried_component)
		{
			if (coord.x < patch.min_coord.x || coord.x > patch.max_coord.x ||
				coord.y < patch.min_coord.y || coord.y > patch.max_coord.y) continue;

			auto& sample = patch.future_samples[patch_index(
				coord.x - patch.min_coord.x,
				coord.y - patch.min_coord.y,
				patch.size.x)];
			sample.water = dry_water_density(sample);
		}

		for (const auto& sample : plan.affected_samples)
		{
			if (sample.coord.x < patch.min_coord.x || sample.coord.x > patch.max_coord.x ||
				sample.coord.y < patch.min_coord.y || sample.coord.y > patch.max_coord.y) continue;

			patch.future_samples[patch_index(
				sample.coord.x - patch.min_coord.x,
				sample.coord.y - patch.min_coord.y,
				patch.size.x)].water = sample.water;
		}
	}

	PlanetTerrain::WaterPreviewMesh PlanetTerrain::render_water_preview_patch(const WaterPreviewPatch& patch) const
	{
		TerrainGenerator current_generator{ patch.settings };
		current_generator.upload_field(patch.current_samples);
		current_generator.dispatch_surface_rebuild(TerrainGenerator::water_channel_index, 0.0f);
		const auto current_mesh = current_generator.readback();

		TerrainGenerator future_generator{ patch.settings };
		future_generator.upload_field(patch.future_samples);
		future_generator.dispatch_surface_rebuild(TerrainGenerator::water_channel_index, 0.0f);
		const auto future_mesh = future_generator.readback();

		return WaterPreviewMesh{
			.current_vertices = current_mesh.mesh_vertices,
			.current_indices = current_mesh.mesh_indices,
			.future_vertices = future_mesh.mesh_vertices,
			.future_indices = future_mesh.mesh_indices
		};
	}

	std::size_t PlanetTerrain::patch_index(const int x, const int y, const std::uint32_t width)
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
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
						normalized_channel(sample.wetness, 0.0f, 1.0f),
						static_cast<std::uint8_t>(sample.water > 0.0f || sample.terrain >= 0.0f ? 255u : 0u)
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

	void PlanetTerrain::load_overlay_assets()
	{
		if (overlay_assets_ready_) return;

		auto load_texture = [](sf::Texture& texture, const char* path)
		{
			if (!texture.loadFromFile(path))
			{
				throw std::runtime_error(std::format("Failed to load {}", path));
			}

			texture.setSmooth(false);
		};

		load_texture(rock_node_texture_, "assets/images/ores/rock.png");
		load_texture(iron_ore_texture_, "assets/images/ores/iron_ore.png");
		load_texture(bronze_ore_texture_, "assets/images/ores/bronze_ore.png");
		load_texture(gold_ore_texture_, "assets/images/ores/gold_ore.png");
		load_texture(diamond_ore_texture_, "assets/images/ores/diamond_ore.png");
		load_texture(processed_resource_texture_, "assets/images/ores/processed_ores.png");
		load_texture(live_plant_texture_, "assets/images/vegetation/ground_plants.png");
		load_texture(grass_plant_texture_, "assets/images/vegetation/grass.png");
		load_texture(flowers_plant_texture_, "assets/images/vegetation/flowers.png");
		load_texture(bushes_plant_texture_, "assets/images/vegetation/bushes.png");
		load_texture(trees_plant_texture_, "assets/images/vegetation/trees.png");
		load_texture(dead_plant_texture_, "assets/images/vegetation/ground_plants_dead.png");

		if (!ui_font_.openFromFile("assets/fonts/arial.ttf"))
		{
			throw std::runtime_error("Failed to load assets/fonts/arial.ttf");
		}

		overlay_assets_ready_ = true;
	}

	void PlanetTerrain::generate_caves_resources_and_plants()
	{
		if (global_field_.empty()) return;

		plant_samples_.assign(global_field_.size(), {});
		active_plant_indices_.clear();
		active_plants_dirty_ = false;
		resource_nodes_.clear();

		std::vector<bool> dirty_chunks(chunks_.size(), true);
		std::vector<ivec2> changed_coords;
		changed_coords.reserve(global_field_.size() / 12u);

		carve_noise_caves(changed_coords);
		widen_caves(changed_coords);
		smooth_cave_terrain(changed_coords);
		seed_cave_ponds(dirty_chunks, changed_coords);
		finalize_generated_field(dirty_chunks, changed_coords);
	}

	void PlanetTerrain::carve_noise_caves(std::vector<ivec2>& changed_coords)
	{
		for (int y = 0; y < static_cast<int>(global_field_size_.y); ++y)
		{
			for (int x = 0; x < static_cast<int>(global_field_size_.x); ++x)
			{
				const ivec2 coord{ x, y };
				auto& sample = global_field_[global_field_index(coord)];
				if (!is_solid(sample)) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < 0.11f || depth > constants::hard_rock_depth_threshold - 0.05f) continue;

				const float depth_factor = std::clamp(
					(depth - 0.11f) / std::max(constants::hard_rock_depth_threshold - 0.16f, 0.01f),
					0.0f,
					1.0f);
				const float tunnel_a = std::abs(perlin_noise(scale_vec2(world, 0.086f), base_chunk_settings_.seed + 53u));
				const float tunnel_b = std::abs(perlin_noise(add_vec2(scale_vec2(world, 0.128f), { 11.0f, -6.0f }), base_chunk_settings_.seed + 311u));
				const float chamber = perlin_fbm(add_vec2(scale_vec2(world, 0.051f), { -8.0f, 15.0f }), base_chunk_settings_.seed + 977u);
				const float pocket = perlin_fbm(add_vec2(scale_vec2(world, 0.093f), { 17.0f, -13.0f }), base_chunk_settings_.seed + 1901u);

				const bool carve_tunnel = tunnel_a < std::lerp(0.05f, 0.08f, depth_factor) && tunnel_b < 0.12f && chamber > -0.03f;
				const bool carve_chamber = chamber > std::lerp(0.18f, 0.10f, depth_factor) && tunnel_a < 0.22f;
				const bool carve_pocket = pocket > 0.24f && chamber > 0.02f && tunnel_b < 0.16f;
				if (!carve_tunnel && !carve_chamber && !carve_pocket) continue;

				sample.terrain = std::min(sample.terrain, -0.85f);
				sample.water = dry_water_density(sample);
				sample.wetness = 0.0f;
				changed_coords.push_back(coord);
			}
		}
	}

	void PlanetTerrain::widen_caves(std::vector<ivec2>& changed_coords)
	{
		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				auto& sample = global_field_[global_field_index(coord)];
				if (!is_solid(sample)) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < 0.11f || depth > constants::hard_rock_depth_threshold - 0.05f) continue;

				int open_neighbors = 0;
				for (int oy = -1; oy <= 1; ++oy)
				{
					for (int ox = -1; ox <= 1; ++ox)
					{
						if (ox == 0 && oy == 0) continue;
						const ivec2 neighbor{ x + ox, y + oy };
						if (!is_valid_global_sample(neighbor)) continue;
						if (!is_solid(global_field_[global_field_index(neighbor)])) ++open_neighbors;
					}
				}

				if (open_neighbors < 5) continue;
				const float widen_roll = perlin_fbm(add_vec2(scale_vec2(world, 0.072f), { 5.0f, -17.0f }), base_chunk_settings_.seed + 2801u);
				if (widen_roll < 0.14f) continue;

				sample.terrain = std::min(sample.terrain, -0.85f);
				sample.water = dry_water_density(sample);
				sample.wetness = 0.0f;
				changed_coords.push_back(coord);
			}
		}
	}

	void PlanetTerrain::smooth_cave_terrain(std::vector<ivec2>& changed_coords)
	{
		for (int smooth_pass = 0; smooth_pass < 2; ++smooth_pass)
		{
			std::vector<float> smoothed_terrain(global_field_.size(), 0.0f);
			for (std::size_t i = 0; i < global_field_.size(); ++i)
			{
				smoothed_terrain[i] = global_field_[i].terrain;
			}

			for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
			{
				for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
				{
					const ivec2 coord{ x, y };
					const vec2 world = global_sample_world_position(coord);
					const float depth = normalized_depth(world);
					if (depth < 0.11f || depth > constants::hard_rock_depth_threshold - 0.05f) continue;

					const int solid_neighbors = solid_neighbor_count(coord);
					if (solid_neighbors <= 1 || solid_neighbors >= 7) continue;

					float total = 0.0f;
					float weight = 0.0f;
					for (int oy = -1; oy <= 1; ++oy)
					{
						for (int ox = -1; ox <= 1; ++ox)
						{
							const ivec2 neighbor{ x + ox, y + oy };
							const float neighbor_weight = (ox == 0 && oy == 0) ? 2.0f : 1.0f;
							total += global_field_[global_field_index(neighbor)].terrain * neighbor_weight;
							weight += neighbor_weight;
						}
					}

					const std::size_t index = global_field_index(coord);
					smoothed_terrain[index] = std::lerp(global_field_[index].terrain, total / std::max(weight, 1e-4f), 0.35f);
				}
			}

			for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
			{
				for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
				{
					const ivec2 coord{ x, y };
					const std::size_t index = global_field_index(coord);
					if (std::abs(global_field_[index].terrain - smoothed_terrain[index]) <= 1e-5f) continue;
					global_field_[index].terrain = smoothed_terrain[index];
					global_field_[index].water = dry_water_density(global_field_[index]);
					changed_coords.push_back(coord);
				}
			}
		}
	}

	void PlanetTerrain::seed_cave_ponds(std::vector<bool>& dirty_chunks, std::vector<ivec2>& changed_coords)
	{
		static constexpr std::array cardinal_offsets{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 }
		};

		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				const auto& sample = global_field_[global_field_index(coord)];
				if (is_solid(sample) || has_water(sample)) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < 0.16f || depth > constants::hard_rock_depth_threshold - 0.06f) continue;
				if (solid_neighbor_count(coord) < 5) continue;

				const float sample_radial = radial_distance(world, base_chunk_settings_.world_center);
				bool local_basin = true;
				for (const auto& offset : cardinal_offsets)
				{
					const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
					if (!is_valid_global_sample(neighbor)) continue;
					const auto& neighbor_sample = global_field_[global_field_index(neighbor)];
					if (is_solid(neighbor_sample)) continue;

					const float neighbor_radial = radial_distance(global_sample_world_position(neighbor), base_chunk_settings_.world_center);
					if (neighbor_radial + std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 0.22f < sample_radial)
					{
						local_basin = false;
						break;
					}
				}
				if (!local_basin) continue;

				const float pond_noise = perlin_fbm(add_vec2(scale_vec2(world, 0.058f), { -14.0f, 5.0f }), base_chunk_settings_.seed + 1709u);
				const float pond_bias = perlin_fbm(add_vec2(scale_vec2(world, 0.034f), { 22.0f, -8.0f }), base_chunk_settings_.seed + 2609u);
				if (pond_noise < 0.06f || pond_bias < -0.02f) continue;

				const float size_factor = std::clamp((pond_noise + pond_bias + 0.2f) * 0.5f, 0.0f, 1.0f);
				const std::uint32_t desired_count = static_cast<std::uint32_t>(std::lround(std::lerp(18.0f, 56.0f, size_factor)));
				const vec2 cave_up = normalize_vec2(subtract_vec2(world, base_chunk_settings_.world_center));
				const vec2 pour_origin = subtract_vec2(world, scale_vec2(cave_up, 1.4f));
				const auto anchor = find_water_anchor(pour_origin);
				if (!anchor.has_value()) continue;

				ivec2 plan_start = *anchor;
				const auto existing_volume = water_volume_at_anchor(*anchor, &plan_start);
				const auto desired_total = std::min<std::uint64_t>(
					static_cast<std::uint64_t>(existing_volume) + static_cast<std::uint64_t>(desired_count),
					std::numeric_limits<std::uint32_t>::max());
				const auto pond_plan = build_water_plan(plan_start, static_cast<std::uint32_t>(desired_total));
				if (!pond_plan.has_value() || pond_plan->wet_sample_count < existing_volume + 12u) continue;

				if (apply_water_plan(*pond_plan, dirty_chunks, changed_coords))
				{
					y += 1;
					x += 1;
				}
			}
		}
	}

	void PlanetTerrain::finalize_generated_field(std::vector<bool>& dirty_chunks, const std::vector<ivec2>& changed_coords)
	{
		recompute_wetness_around(changed_coords, dirty_chunks);
		generate_resource_nodes();
		rebuild_dirty_chunks(dirty_chunks);
	}

	void PlanetTerrain::compact_active_plants()
	{
		std::erase_if(active_plant_indices_, [this](const std::size_t index)
		{
			return index >= plant_samples_.size() || plant_samples_[index].stage == PlantStage::Empty;
		});
		active_plants_dirty_ = false;
	}

	void PlanetTerrain::generate_resource_nodes()
	{
		resource_nodes_.clear();
		resource_nodes_.reserve(12000u);
		std::unordered_set<std::uint64_t> occupied_samples;

		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				const auto& sample = global_field_[global_field_index(coord)];
				const int neighbors = solid_neighbor_count(coord);
				if (!is_exposed_to_air(sample, neighbors)) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				const bool cave = depth > 0.16f && depth < constants::hard_rock_depth_threshold - 0.04f;
				const float roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 1701u);
				const float density = cave ? 0.19f : 0.028f;
				if (roll > density) continue;

				ResourceKind kind = ResourceKind::Rock;
				const float ore_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 2309u);
				if (cave && depth > 0.48f && ore_roll > 0.82f) kind = ResourceKind::DiamondOre;
				else if (cave && depth > 0.38f && ore_roll > 0.64f) kind = ResourceKind::GoldOre;
				else if (cave && depth > 0.24f && ore_roll > 0.46f) kind = ResourceKind::BronzeOre;
				else if (cave && ore_roll > 0.20f) kind = ResourceKind::IronOre;

				resource_nodes_.push_back({
					.kind = kind,
					.coord = coord,
					.variant = static_cast<std::uint8_t>(std::clamp(static_cast<int>(hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 19u) * 16.0f), 0, 15)),
					.cave_variant = cave
				});
				occupied_samples.insert(sample_key(coord));
			}
		}

		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				const auto& sample = global_field_[global_field_index(coord)];
				if (!is_exposed_to_air(sample, solid_neighbor_count(coord))) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < 0.16f || depth > constants::hard_rock_depth_threshold - 0.04f) continue;
				const bool cave = true;
				if (!cave) continue;
				const float roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 4073u);
				const float density = sample.wetness > 0.10f ? 0.72f : 0.54f;
				if (roll > density) continue;

				if (occupied_samples.contains(sample_key(coord))) continue;

				resource_nodes_.push_back({
					.kind = ResourceKind::DeadPlant,
					.coord = coord,
					.variant = static_cast<std::uint8_t>(std::clamp(static_cast<int>(roll * 211.0f) % 16, 0, 15)),
					.cave_variant = true
				});
				occupied_samples.insert(sample_key(coord));
			}
		}
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
			sample.wetness = 0.0f;
			sample.padding = 0.0f;
		}

		generate_caves_resources_and_plants();
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

	float PlanetTerrain::normalized_depth(const vec2 world_position) const
	{
		const float surface_radius = std::max(base_chunk_settings_.planet_radius, 1e-4f);
		const float radius = radial_distance(world_position, base_chunk_settings_.world_center);
		return std::clamp(1.0f - radius / surface_radius, 0.0f, 1.0f);
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

	bool PlanetTerrain::has_protective_water_neighbor(const ivec2 coord) const
	{
		if (!is_valid_global_sample(coord)) return false;

		const float sample_radial = radial_distance(global_sample_world_position(coord), base_chunk_settings_.world_center);
		const float radial_tolerance = std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 0.45f;
		static constexpr int search_radius = 2;

		for (int y = -search_radius; y <= search_radius; ++y)
		{
			for (int x = -search_radius; x <= search_radius; ++x)
			{
				if (x == 0 && y == 0) continue;

				const ivec2 neighbor{ coord.x + x, coord.y + y };
				if (!is_valid_global_sample(neighbor)) continue;

				const auto& neighbor_sample = global_field_[global_field_index(neighbor)];
				if (!has_water(neighbor_sample)) continue;

				const float neighbor_radial = radial_distance(global_sample_world_position(neighbor), base_chunk_settings_.world_center);
				if (neighbor_radial + radial_tolerance >= sample_radial) return true;
			}
		}

		return false;
	}

	bool PlanetTerrain::is_dig_protected(const ivec2 coord) const
	{
		if (!is_valid_global_sample(coord)) return false;
		const auto& sample = global_field_[global_field_index(coord)];
		return has_water(sample) ||
			has_protective_water_neighbor(coord) ||
			normalized_depth(global_sample_world_position(coord)) >= constants::hard_rock_depth_threshold;
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

	std::uint32_t PlanetTerrain::water_volume_at_anchor(const ivec2 anchor, ivec2* plan_start) const
	{
		if (plan_start != nullptr) *plan_start = anchor;
		if (!is_valid_global_sample(anchor)) return 0u;
		if (!has_water(global_field_[global_field_index(anchor)])) return 0u;

		auto component = collect_water_component(anchor);
		if (component.empty()) return 0u;

		ivec2 lowest_coord = component.front();
		float lowest_radial = radial_distance(global_sample_world_position(lowest_coord), base_chunk_settings_.world_center);
		for (const auto coord : component)
		{
			const float radial = radial_distance(global_sample_world_position(coord), base_chunk_settings_.world_center);
			if (radial > lowest_radial + 1e-5f) continue;
			if (std::abs(radial - lowest_radial) <= 1e-5f &&
				(coord.y > lowest_coord.y || (coord.y == lowest_coord.y && coord.x >= lowest_coord.x))) continue;

			lowest_coord = coord;
			lowest_radial = radial;
		}

		if (plan_start != nullptr) *plan_start = lowest_coord;
		return static_cast<std::uint32_t>(component.size());
	}

	std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_targeted_water_plan(
		const vec2 world_position,
		const std::uint32_t volume_cap,
		const bool pickup,
		std::uint32_t* const existing_volume) const
	{
		if (existing_volume != nullptr) *existing_volume = 0u;
		if (global_field_.empty() || volume_cap == 0u) return std::nullopt;

		const auto anchor = pickup ? find_water_sample(world_position) : find_water_anchor(world_position);
		if (!anchor.has_value()) return std::nullopt;

		ivec2 plan_start = *anchor;
		const auto current_volume = water_volume_at_anchor(*anchor, &plan_start);
		if (existing_volume != nullptr) *existing_volume = current_volume;
		if (pickup && current_volume == 0u) return std::nullopt;

		const auto desired_total = pickup
			? (current_volume > volume_cap ? current_volume - volume_cap : 0u)
			: static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(current_volume) + static_cast<std::uint64_t>(volume_cap),
				std::numeric_limits<std::uint32_t>::max()));

		return build_water_plan(plan_start, desired_total);
	}

	bool PlanetTerrain::apply_water_plan_and_rebuild(const WaterPlan& plan)
	{
		std::vector<bool> dirty_chunks(chunks_.size(), false);
		std::vector<ivec2> changed_coords;
		const bool changed = apply_water_plan(plan, dirty_chunks, changed_coords);
		if (!changed) return false;

		recompute_wetness_around(changed_coords, dirty_chunks);
		rebuild_dirty_chunks(dirty_chunks);
		return true;
	}

	PlanetTerrain::TerrainEditResult PlanetTerrain::apply_terrain_edit_to_global_field(const TerrainEdit& edit,
		std::vector<bool>& dirty_chunks, std::vector<ivec2>& changed_coords, const std::uint32_t unit_budget)
	{
		const float radius = std::max(edit.position_radius_strength.z, 0.0f);
		if (radius <= 0.0f || unit_budget == 0u) return {};

		const vec2 edit_center{
			edit.position_radius_strength.x,
			edit.position_radius_strength.y
		};

		if (!circle_overlaps_rect(edit_center, radius, grid_min_, grid_max_)) return {};

		const float signed_strength = edit.position_radius_strength.w;
		const float falloff_exponent = std::max(edit.shape.x, 0.001f);
		const bool digging = signed_strength < 0.0f;
		bool requires_wetness_rebuild = false;
		std::vector<TerrainEditCandidate> candidates;
		collect_terrain_edit_candidates(
			edit_center,
			radius,
			signed_strength,
			falloff_exponent,
			digging,
			candidates,
			requires_wetness_rebuild);

		if (candidates.empty()) return {};

		std::ranges::sort(candidates, [](const TerrainEditCandidate& lhs, const TerrainEditCandidate& rhs)
		{
			if (std::abs(lhs.falloff - rhs.falloff) > 1e-6f) return lhs.falloff > rhs.falloff;
			return lhs.distance_to_center < rhs.distance_to_center;
		});

		TerrainEditResult result{};
		result.requires_wetness_rebuild = requires_wetness_rebuild;
		result.candidates = static_cast<std::uint32_t>(std::min<std::size_t>(candidates.size(), std::numeric_limits<std::uint32_t>::max()));
		apply_terrain_edit_candidates(candidates, signed_strength, unit_budget, dirty_chunks, changed_coords, result);

		return result;
	}

	void PlanetTerrain::collect_terrain_edit_candidates(
		const vec2 edit_center,
		const float radius,
		const float signed_strength,
		const float falloff_exponent,
		const bool digging,
		std::vector<TerrainEditCandidate>& candidates,
		bool& requires_wetness_rebuild) const
	{
		const auto min_x = static_cast<int>(std::floor((edit_center.x - radius - global_field_origin_.x) / terrain_cell_size_.x));
		const auto min_y = static_cast<int>(std::floor((edit_center.y - radius - global_field_origin_.y) / terrain_cell_size_.y));
		const auto max_x = static_cast<int>(std::ceil((edit_center.x + radius - global_field_origin_.x) / terrain_cell_size_.x));
		const auto max_y = static_cast<int>(std::ceil((edit_center.y + radius - global_field_origin_.y) / terrain_cell_size_.y));

		const int clamped_min_x = std::clamp(min_x, 0, static_cast<int>(global_field_size_.x) - 1);
		const int clamped_min_y = std::clamp(min_y, 0, static_cast<int>(global_field_size_.y) - 1);
		const int clamped_max_x = std::clamp(max_x, 0, static_cast<int>(global_field_size_.x) - 1);
		const int clamped_max_y = std::clamp(max_y, 0, static_cast<int>(global_field_size_.y) - 1);

		candidates.reserve(static_cast<std::size_t>((clamped_max_x - clamped_min_x + 1) * (clamped_max_y - clamped_min_y + 1)));

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
				if (falloff <= 1e-6f) continue;

				const auto& sample = global_field_[global_field_index(coord)];
				const bool had_water = has_water(sample);
				const bool had_wetness = sample.wetness > 1e-4f;
				const bool had_water_adjacent = has_water_neighbor(coord);
				const float next_terrain = sample.terrain + signed_strength * falloff;
				float next_water = sample.water;
				if (next_terrain >= 0.0f || !had_water)
				{
					next_water = dry_water_density(FieldSample{
						.terrain = next_terrain,
						.water = sample.water,
						.wetness = sample.wetness,
						.padding = sample.padding
					});
				}

				const bool local_changed = std::abs(next_terrain - sample.terrain) > 1e-6f ||
					std::abs(next_water - sample.water) > 1e-6f;
				if (!local_changed) continue;

				candidates.push_back({
					.coord = coord,
					.falloff = falloff,
					.distance_to_center = distance_to_center
				});

				if (had_water || had_wetness || had_water_adjacent) requires_wetness_rebuild = true;
			}
		}
	}

	void PlanetTerrain::apply_terrain_edit_candidates(
		const std::vector<TerrainEditCandidate>& candidates,
		const float signed_strength,
		const std::uint32_t unit_budget,
		std::vector<bool>& dirty_chunks,
		std::vector<ivec2>& changed_coords,
		TerrainEditResult& result)
	{
		std::unordered_set<std::uint64_t> cleared_keys;
		const auto apply_count = std::min<std::size_t>(candidates.size(), unit_budget);

		for (std::size_t i = 0; i < apply_count; ++i)
		{
			const auto coord = candidates[i].coord;
			const auto sample_index = global_field_index(coord);
			auto& sample = global_field_[sample_index];
			const bool had_water = has_water(sample);
			const bool was_solid = is_solid(sample);
			const float next_terrain = sample.terrain + signed_strength * candidates[i].falloff;
			bool local_changed = std::abs(next_terrain - sample.terrain) > 1e-6f;
			sample.terrain = next_terrain;

			if (sample.terrain >= 0.0f || !had_water)
			{
				const float next_water = dry_water_density(sample);
				local_changed = std::abs(next_water - sample.water) > 1e-6f || local_changed;
				sample.water = next_water;
			}

			if (!local_changed) continue;

			result.changed = true;
			++result.units;
			changed_coords.push_back(coord);
			mark_chunks_covering_global_sample(coord, dirty_chunks);

			if (was_solid && !is_solid(sample) && sample_index < plant_samples_.size())
			{
				plant_samples_[sample_index] = {};
				active_plants_dirty_ = true;
				cleared_keys.insert(sample_key(coord));
				++result.cleared_samples;
			}
		}

		remove_resource_nodes(cleared_keys);
	}

	void PlanetTerrain::remove_resource_nodes(const std::unordered_set<std::uint64_t>& cleared_keys)
	{
		if (cleared_keys.empty()) return;

		resource_nodes_.erase(
			std::remove_if(resource_nodes_.begin(), resource_nodes_.end(), [&cleared_keys](const ResourceNode& node)
			{
				return cleared_keys.contains(sample_key(node.coord));
			}),
			resource_nodes_.end());
	}

	std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_water_plan(const ivec2 start_coord,
		const std::uint32_t desired_wet_sample_count) const
	{
		if (!is_valid_global_sample(start_coord)) return std::nullopt;
		if (is_solid(global_field_[global_field_index(start_coord)])) return std::nullopt;

		static constexpr std::array neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 }
		};
		const float cell_extent = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
		const float smoothing_margin = cell_extent * 7.5f;

		WaterPlan plan{};
		plan.dried_component = collect_water_component(start_coord);

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

		while (!frontier.empty() && selected_samples.size() < desired_wet_sample_count)
		{
			const auto current = frontier.top();
			frontier.pop();
			selected_samples.push_back(current);

			for (const auto& offset : neighbors)
			{
				push_candidate({ current.coord.x + offset.x, current.coord.y + offset.y });
			}
		}

		plan.wet_sample_count = static_cast<std::uint32_t>(selected_samples.size());
		if (selected_samples.empty()) return plan;

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
			const auto& field_sample = global_field_[global_field_index(selected.coord)];
			const float base_water_depth = surface_level - selected.radial;
			const int neighbor_solids = solid_neighbor_count(selected.coord);
			const float terrain_fit_depth = std::max(-field_sample.terrain + cell_extent * 0.02f, 0.0f);
			const float contact_blend = base_water_depth > 0.0f ?
				std::clamp((static_cast<float>(neighbor_solids) - 4.0f) / 3.0f, 0.0f, 1.0f) :
				0.0f;
			const float contact_support = std::lerp(base_water_depth, terrain_fit_depth, contact_blend);

			plan.affected_samples.push_back({
				.coord = selected.coord,
				.water = std::max(base_water_depth, contact_support)
			});
		}

		return plan;
	}

	bool PlanetTerrain::apply_water_plan(const WaterPlan& plan, std::vector<bool>& dirty_chunks,
		std::vector<ivec2>& changed_coords)
	{
		bool changed = false;
		for (const auto& coord : plan.dried_component)
		{
			auto& sample = global_field_[global_field_index(coord)];
			const float next_water = dry_water_density(sample);
			if (std::abs(sample.water - next_water) <= 1e-6f) continue;

			sample.water = next_water;
			changed = true;
			changed_coords.push_back(coord);
			mark_chunks_covering_global_sample(coord, dirty_chunks);
		}

		for (const auto& entry : plan.affected_samples)
		{
			auto& sample = global_field_[global_field_index(entry.coord)];
			if (std::abs(sample.water - entry.water) <= 1e-6f) continue;

			sample.water = entry.water;
			changed = true;
			changed_coords.push_back(entry.coord);
			mark_chunks_covering_global_sample(entry.coord, dirty_chunks);
		}

		return changed;
	}

	void PlanetTerrain::recompute_wetness_around(const std::vector<ivec2>& changed_coords, std::vector<bool>& dirty_chunks)
	{
		if (changed_coords.empty() || global_field_.empty()) return;

		const float min_cell_extent = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
		static constexpr float max_wetness_radius_cells = 180.0f;

		ivec2 changed_min = changed_coords.front();
		ivec2 changed_max = changed_coords.front();
		for (const auto coord : changed_coords)
		{
			changed_min.x = std::min(changed_min.x, coord.x);
			changed_min.y = std::min(changed_min.y, coord.y);
			changed_max.x = std::max(changed_max.x, coord.x);
			changed_max.y = std::max(changed_max.y, coord.y);
		}

		const int discovery_radius_cells = static_cast<int>(std::ceil(max_wetness_radius_cells));
		const auto changed_bounds = clamp_sample_bounds(changed_min, changed_max);
		const auto discovery_bounds = expand_sample_bounds(changed_bounds, discovery_radius_cells);
		const auto components = collect_wetness_components(discovery_bounds, min_cell_extent, discovery_radius_cells);

		SampleBounds affected_bounds = expand_sample_bounds(changed_bounds, 1);
		for (const auto& component : components)
		{
			affected_bounds = merge_sample_bounds(
				affected_bounds,
				expand_sample_bounds(component.water_bounds, component.radius_cells));
		}

		const int affected_width = affected_bounds.max.x - affected_bounds.min.x + 1;
		const int affected_height = affected_bounds.max.y - affected_bounds.min.y + 1;
		std::vector<float> best_wetness(
			static_cast<std::size_t>(affected_width) * static_cast<std::size_t>(affected_height),
			0.0f);

		for (const auto& component : components)
		{
			apply_wetness_component(component, affected_bounds, best_wetness);
		}

		write_back_wetness(affected_bounds, best_wetness, dirty_chunks);
	}

	PlanetTerrain::SampleBounds PlanetTerrain::clamp_sample_bounds(const ivec2 min_coord, const ivec2 max_coord) const
	{
		return {
			.min = {
				std::clamp(min_coord.x, 0, static_cast<int>(global_field_size_.x) - 1),
				std::clamp(min_coord.y, 0, static_cast<int>(global_field_size_.y) - 1)
			},
			.max = {
				std::clamp(max_coord.x, 0, static_cast<int>(global_field_size_.x) - 1),
				std::clamp(max_coord.y, 0, static_cast<int>(global_field_size_.y) - 1)
			}
		};
	}

	PlanetTerrain::SampleBounds PlanetTerrain::expand_sample_bounds(const SampleBounds& bounds, const int radius_cells) const
	{
		return clamp_sample_bounds(
			{ bounds.min.x - radius_cells, bounds.min.y - radius_cells },
			{ bounds.max.x + radius_cells, bounds.max.y + radius_cells });
	}

	PlanetTerrain::SampleBounds PlanetTerrain::merge_sample_bounds(const SampleBounds& lhs, const SampleBounds& rhs)
	{
		return {
			.min = { std::min(lhs.min.x, rhs.min.x), std::min(lhs.min.y, rhs.min.y) },
			.max = { std::max(lhs.max.x, rhs.max.x), std::max(lhs.max.y, rhs.max.y) }
		};
	}

	bool PlanetTerrain::sample_bounds_intersect(const SampleBounds& lhs, const SampleBounds& rhs)
	{
		return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x &&
			lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y;
	}

	std::vector<PlanetTerrain::WetnessComponent> PlanetTerrain::collect_wetness_components(
		const SampleBounds& discovery_bounds,
		const float min_cell_extent,
		const int max_wetness_radius_cells) const
	{
		static constexpr float base_wetness_radius_cells = 16.0f;
		static constexpr float pond_radius_scale = 5.75f;

		auto component_wetness_distance = [min_cell_extent, max_wetness_radius_cells](const std::size_t water_sample_count)
		{
			const float equivalent_radius_cells = std::sqrt(
				static_cast<float>(water_sample_count) / std::numbers::pi_v<float>);
			const float radius_cells = std::clamp(
				base_wetness_radius_cells + equivalent_radius_cells * pond_radius_scale,
				1.0f,
				static_cast<float>(max_wetness_radius_cells));
			return std::max(radius_cells * min_cell_extent, min_cell_extent);
		};

		std::unordered_set<std::uint64_t> visited_water;
		std::vector<WetnessComponent> components;

		for (int y = discovery_bounds.min.y; y <= discovery_bounds.max.y; ++y)
		{
			for (int x = discovery_bounds.min.x; x <= discovery_bounds.max.x; ++x)
			{
				const ivec2 coord{ x, y };
				if (!has_water(global_field_[global_field_index(coord)])) continue;

				const auto key = sample_key(coord);
				if (!visited_water.insert(key).second) continue;

				auto water_cells = collect_water_component(coord);
				for (const auto water_coord : water_cells)
				{
					visited_water.insert(sample_key(water_coord));
				}

				if (water_cells.empty()) continue;

				SampleBounds water_bounds{
					.min = water_cells.front(),
					.max = water_cells.front()
				};
				for (const auto water_coord : water_cells)
				{
					water_bounds.min.x = std::min(water_bounds.min.x, water_coord.x);
					water_bounds.min.y = std::min(water_bounds.min.y, water_coord.y);
					water_bounds.max.x = std::max(water_bounds.max.x, water_coord.x);
					water_bounds.max.y = std::max(water_bounds.max.y, water_coord.y);
				}

				const float max_distance = component_wetness_distance(water_cells.size());
				const int radius_cells = std::max(
					1,
					static_cast<int>(std::ceil(max_distance / std::max(min_cell_extent, 1e-6f))));
				const auto component_bounds = expand_sample_bounds(water_bounds, radius_cells);
				if (!sample_bounds_intersect(component_bounds, discovery_bounds)) continue;

				components.push_back({
					.water_cells = std::move(water_cells),
					.water_bounds = water_bounds,
					.max_distance = max_distance,
					.radius_cells = radius_cells
				});
			}
		}

		return components;
	}

	void PlanetTerrain::apply_wetness_component(
		const WetnessComponent& component,
		const SampleBounds& affected_bounds,
		std::vector<float>& best_wetness) const
	{
		auto bounds_contains = [](const SampleBounds& bounds, const ivec2 coord)
		{
			return coord.x >= bounds.min.x && coord.y >= bounds.min.y &&
				coord.x <= bounds.max.x && coord.y <= bounds.max.y;
		};

		auto neighbor_distance = [this](const ivec2 offset)
		{
			const float dx = terrain_cell_size_.x * static_cast<float>(offset.x);
			const float dy = terrain_cell_size_.y * static_cast<float>(offset.y);
			return std::sqrt(dx * dx + dy * dy);
		};

		const auto propagation_bounds = expand_sample_bounds(component.water_bounds, component.radius_cells);
		const int propagation_width = propagation_bounds.max.x - propagation_bounds.min.x + 1;
		const int propagation_height = propagation_bounds.max.y - propagation_bounds.min.y + 1;
		std::vector<float> best_distances(
			static_cast<std::size_t>(propagation_width) * static_cast<std::size_t>(propagation_height),
			std::numeric_limits<float>::infinity());

		auto propagation_index = [propagation_bounds, propagation_width](const ivec2 coord)
		{
			return static_cast<std::size_t>(coord.y - propagation_bounds.min.y) *
				static_cast<std::size_t>(propagation_width) +
				static_cast<std::size_t>(coord.x - propagation_bounds.min.x);
		};

		const int affected_width = affected_bounds.max.x - affected_bounds.min.x + 1;
		auto affected_index = [affected_bounds, affected_width](const ivec2 coord)
		{
			return static_cast<std::size_t>(coord.y - affected_bounds.min.y) *
				static_cast<std::size_t>(affected_width) +
				static_cast<std::size_t>(coord.x - affected_bounds.min.x);
		};

		static constexpr std::array wetness_neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 },
			ivec2{ 1, 1 },
			ivec2{ 1, -1 },
			ivec2{ -1, 1 },
			ivec2{ -1, -1 }
		};

		std::priority_queue<WetnessNode, std::vector<WetnessNode>, WetnessNodeCompare> frontier;

		auto try_push = [&](const ivec2 coord, const float distance)
		{
			if (!bounds_contains(propagation_bounds, coord) || !bounds_contains(affected_bounds, coord) ||
				!is_valid_global_sample(coord)) return;
			if (!is_solid(global_field_[global_field_index(coord)])) return;

			auto& best_distance = best_distances[propagation_index(coord)];
			if (distance + 1e-5f >= best_distance || distance > component.max_distance) return;

			best_distance = distance;
			frontier.push(WetnessNode{ coord, distance });
		};

		for (const auto water_coord : component.water_cells)
		{
			for (const auto& offset : wetness_neighbors)
			{
				try_push({ water_coord.x + offset.x, water_coord.y + offset.y }, neighbor_distance(offset));
			}
		}

		while (!frontier.empty())
		{
			const auto [coord, distance] = frontier.top();
			frontier.pop();

			if (distance > best_distances[propagation_index(coord)] + 1e-5f) continue;

			best_wetness[affected_index(coord)] = std::max(
				best_wetness[affected_index(coord)],
				wetness_strength(distance, component.max_distance));

			for (const auto& offset : wetness_neighbors)
			{
				try_push(
					{ coord.x + offset.x, coord.y + offset.y },
					distance + neighbor_distance(offset));
			}
		}
	}

	void PlanetTerrain::write_back_wetness(
		const SampleBounds& affected_bounds,
		const std::vector<float>& best_wetness,
		std::vector<bool>& dirty_chunks)
	{
		const int affected_width = affected_bounds.max.x - affected_bounds.min.x + 1;
		auto affected_index = [affected_bounds, affected_width](const ivec2 coord)
		{
			return static_cast<std::size_t>(coord.y - affected_bounds.min.y) *
				static_cast<std::size_t>(affected_width) +
				static_cast<std::size_t>(coord.x - affected_bounds.min.x);
		};

		for (int y = affected_bounds.min.y; y <= affected_bounds.max.y; ++y)
		{
			for (int x = affected_bounds.min.x; x <= affected_bounds.max.x; ++x)
			{
				const ivec2 coord{ x, y };
				auto& sample = global_field_[global_field_index(coord)];
				const float next_wetness = is_solid(sample) ? best_wetness[affected_index(coord)] : 0.0f;

				if (std::abs(sample.wetness - next_wetness) <= 1e-6f) continue;

				sample.wetness = next_wetness;
				mark_chunks_covering_global_sample(coord, dirty_chunks);
			}
		}
	}

	vec2 PlanetTerrain::chunk_size() const { return base_chunk_settings_.chunk_size; }
	vec2 PlanetTerrain::terrain_cell_size() const { return terrain_cell_size_; }
	vec2 PlanetTerrain::planet_center() const { return base_chunk_settings_.world_center; }

	float PlanetTerrain::wetness_at(const vec2 world_position) const
	{
		if (global_field_.empty()) return 0.0f;

		const auto coord = world_to_global_sample(world_position);
		const auto& sample = global_field_[global_field_index(coord)];
		if (!is_solid(sample)) return 0.0f;
		return std::clamp(sample.wetness, 0.0f, 1.0f);
	}

	bool PlanetTerrain::is_seed_plantable(const vec2 world_position) const
	{
		return find_plantable_seed_coord(world_position).has_value();
	}

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
