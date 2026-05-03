#include "pch.hpp"
#include "PlanetTerrain.hpp"

#include "gfx/AlphaBlendPass.hpp"
#include "gfx/Projection.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
		namespace
		{
		inline constexpr float cave_generation_min_depth{ 0.14f };
		inline constexpr float cave_generation_max_depth{ 0.54f };
		inline constexpr float cave_resource_min_depth{ 0.18f };
		inline constexpr float cave_resource_max_depth{ 0.56f };

		float radial_distance(const vec2& point, const vec2& center)
		{
			const vec2 offset = subtract_vec2(point, center);
			return std::sqrt(offset.x * offset.x + offset.y * offset.y);
		}

		struct DeadPlantSpriteFamily final
		{
			const char* path{ nullptr };
			int tile_size{ 32 };
			float world_height{ 1.45f };
		};

		inline constexpr std::array<DeadPlantSpriteFamily, PlanetTerrain::dead_plant_family_count> dead_plant_sprite_families{{
			{ "assets/images/vegetation/ground_plants_dead.png", 32, 2.90f },
			{ "assets/images/vegetation/mushrooms_dead.png", 32, 2.00f },
			{ "assets/images/vegetation/ferns_dead.png", 32, 3.00f },
			{ "assets/images/vegetation/broadleaf_plants_dead.png", 32, 3.16f },
			{ "assets/images/vegetation/reeds_dead.png", 32, 3.50f },
			{ "assets/images/vegetation/creepers_dead.png", 32, 3.24f },
			{ "assets/images/vegetation/jungle_roots_dead.png", 32, 3.90f },
			{ "assets/images/vegetation/hanging_vines_dead.png", 32, 3.76f },
			{ "assets/images/vegetation/bushes_dead.png", 32, 3.70f },
			{ "assets/images/vegetation/trees_dead.png", 64, 7.90f }
		}};

		enum class VegetationBatchId : std::size_t
		{
			Live32 = 0,
			Live64 = 1,
			Dead32 = 2,
			Dead64 = 3
		};

		inline constexpr std::size_t vegetation_batch_count{ 4u };

		bool circle_overlaps_rect(const vec2 center, const float radius, const vec2 rect_min, const vec2 rect_max)
		{
			const float closest_x = std::clamp(center.x, rect_min.x, rect_max.x);
			const float closest_y = std::clamp(center.y, rect_min.y, rect_max.y);
			const float dx = center.x - closest_x;
			const float dy = center.y - closest_y;
			return dx * dx + dy * dy <= radius * radius;
		}

		float distance_sq_to_segment(const vec2 point, const vec2 start, const vec2 end)
		{
			const vec2 segment = subtract_vec2(end, start);
			const float segment_length_sq = segment.lengthSquared();
			if (segment_length_sq <= 1e-6f)
			{
				const vec2 delta = subtract_vec2(point, start);
				return delta.lengthSquared();
			}

			const float t = std::clamp(subtract_vec2(point, start).dot(segment) / segment_length_sq, 0.0f, 1.0f);
			const vec2 closest = add_vec2(start, scale_vec2(segment, t));
			const vec2 delta = subtract_vec2(point, closest);
			return delta.lengthSquared();
		}

		bool has_water(const PlanetTerrain::FieldSample& sample)
		{
			return std::min(-sample.terrain, sample.water) > 1e-4f;
		}

		float combined_water_field(const PlanetTerrain::FieldSample& sample)
		{
			return std::min(-sample.terrain, sample.water);
		}

		std::vector<std::vector<vec2>> extract_contour_loops_from_scalar_field(const std::span<const float> values,
			const uvec2 size, const vec2 origin, const vec2 cell_size)
		{
			if (values.empty() || size.x < 2u || size.y < 2u) return {};

			std::vector<int> horizontal_edge_ids(static_cast<std::size_t>(size.y) * static_cast<std::size_t>(size.x - 1u), -1);
			std::vector<int> vertical_edge_ids(static_cast<std::size_t>(size.y) * static_cast<std::size_t>(size.x), -1);
			std::vector<vec2> boundary_vertices;
			std::vector<TerrainGenerator::BoundaryEdge> boundary_edges;

			boundary_vertices.reserve(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
			boundary_edges.reserve(static_cast<std::size_t>(size.x - 1u) * static_cast<std::size_t>(size.y - 1u) * 2u);

			auto sample_value = [&values, size](const std::uint32_t x, const std::uint32_t y)
			{
				return values[static_cast<std::size_t>(y) * size.x + x];
			};

			auto grid_to_world = [origin, cell_size](const float x, const float y)
			{
				return vec2{
					origin.x + x * cell_size.x,
					origin.y + y * cell_size.y
				};
			};

			for (std::uint32_t y = 0; y < size.y; ++y)
			{
				for (std::uint32_t x = 0; x < size.x; ++x)
				{
					if (x + 1u < size.x)
					{
						const float a = sample_value(x, y);
						const float b = sample_value(x + 1u, y);
						const bool ia = a >= 0.0f;
						const bool ib = b >= 0.0f;
						const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x - 1u) + x;
						if (ia != ib)
						{
							float t = 0.5f;
							const float d = b - a;
							if (std::abs(d) > 1e-6f) t = std::clamp(-a / d, 0.0f, 1.0f);
							horizontal_edge_ids[index] = static_cast<int>(boundary_vertices.size());
							boundary_vertices.push_back(grid_to_world(static_cast<float>(x) + t, static_cast<float>(y)));
						}
					}

					if (y + 1u < size.y)
					{
						const float a = sample_value(x, y);
						const float b = sample_value(x, y + 1u);
						const bool ia = a >= 0.0f;
						const bool ib = b >= 0.0f;
						const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x) + x;
						if (ia != ib)
						{
							float t = 0.5f;
							const float d = b - a;
							if (std::abs(d) > 1e-6f) t = std::clamp(-a / d, 0.0f, 1.0f);
							vertical_edge_ids[index] = static_cast<int>(boundary_vertices.size());
							boundary_vertices.push_back(grid_to_world(static_cast<float>(x), static_cast<float>(y) + t));
						}
					}
				}
			}

			auto push_edge = [&boundary_edges](const int a, const int b)
			{
				if (a < 0 || b < 0) return;
				boundary_edges.push_back({
					.a = static_cast<std::uint32_t>(a),
					.b = static_cast<std::uint32_t>(b)
				});
			};

			for (std::uint32_t y = 0; y + 1u < size.y; ++y)
			{
				for (std::uint32_t x = 0; x + 1u < size.x; ++x)
				{
					const float v0 = sample_value(x, y);
					const float v1 = sample_value(x + 1u, y);
					const float v2 = sample_value(x + 1u, y + 1u);
					const float v3 = sample_value(x, y + 1u);
					const bool i0 = v0 >= 0.0f;
					const bool i1 = v1 >= 0.0f;
					const bool i2 = v2 >= 0.0f;
					const bool i3 = v3 >= 0.0f;
					const int mask =
						(i0 ? 1 : 0) |
						(i1 ? 2 : 0) |
						(i2 ? 4 : 0) |
						(i3 ? 8 : 0);
					if (mask == 0 || mask == 15) continue;

					const int e0 = horizontal_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x - 1u) + x];
					const int e1 = vertical_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x) + (x + 1u)];
					const int e2 = horizontal_edge_ids[static_cast<std::size_t>(y + 1u) * static_cast<std::size_t>(size.x - 1u) + x];
					const int e3 = vertical_edge_ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.x) + x];

					const float center = 0.25f * (v0 + v1 + v2 + v3);
					const bool inside_center = center >= 0.0f;

					switch (mask)
					{
						case 1: push_edge(e0, e3); break;
						case 2: push_edge(e1, e0); break;
						case 3: push_edge(e1, e3); break;
						case 4: push_edge(e2, e1); break;
						case 5:
							if (inside_center) { push_edge(e0, e1); push_edge(e2, e3); }
							else { push_edge(e0, e3); push_edge(e2, e1); }
							break;
						case 6: push_edge(e2, e0); break;
						case 7: push_edge(e2, e3); break;
						case 8: push_edge(e3, e2); break;
						case 9: push_edge(e0, e2); break;
						case 10:
							if (inside_center) { push_edge(e3, e0); push_edge(e1, e2); }
							else { push_edge(e3, e2); push_edge(e1, e0); }
							break;
						case 11: push_edge(e1, e2); break;
						case 12: push_edge(e3, e1); break;
						case 13: push_edge(e0, e1); break;
						case 14: push_edge(e3, e0); break;
					}
				}
			}

			return TerrainContour::extract_contours(boundary_vertices, boundary_edges).loops;
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

		bool point_inside_brush_blocker(const vec2 point, const GroundBrushBlocker& blocker,
			const vec2 padding = { 0.0f, 0.0f })
		{
			const vec2 delta = subtract_vec2(point, blocker.center);
			const float local_x = delta.dot(blocker.right);
			const float local_y = delta.dot(blocker.up);
			return
				std::abs(local_x) <= blocker.half_extents.x + padding.x &&
				std::abs(local_y) <= blocker.half_extents.y + padding.y;
		}

		float grass_influence_radius_for(const PlanetTerrain::PlantFamily family)
		{
			switch (family)
			{
				case PlanetTerrain::PlantFamily::Grass: return 1.55f;
				case PlanetTerrain::PlantFamily::Flowers: return 1.35f;
				case PlanetTerrain::PlantFamily::Bush: return 1.50f;
				case PlanetTerrain::PlantFamily::Tree: return 1.70f;
			}

			return 1.0f;
		}

		float grass_influence_strength_for(const PlanetTerrain::PlantFamily family)
		{
			switch (family)
			{
				case PlanetTerrain::PlantFamily::Grass: return 1.0f;
				case PlanetTerrain::PlantFamily::Flowers: return 0.92f;
				case PlanetTerrain::PlantFamily::Bush: return 0.78f;
				case PlanetTerrain::PlantFamily::Tree: return 0.66f;
			}

			return 0.7f;
		}

		bool is_low_cover_family(const PlanetTerrain::PlantFamily family)
		{
			return family == PlanetTerrain::PlantFamily::Grass || family == PlanetTerrain::PlantFamily::Flowers;
		}

		bool is_woody_family(const PlanetTerrain::PlantFamily family)
		{
			return family == PlanetTerrain::PlantFamily::Bush || family == PlanetTerrain::PlantFamily::Tree;
		}

		float hash01(const float x, const float y, const std::uint32_t seed)
		{
			const float value = std::sin(x * 12.9898f + y * 78.233f + static_cast<float>(seed) * 0.013f) * 43758.5453f;
			return value - std::floor(value);
		}

		float fract01(const float value)
		{
			return value - std::floor(value);
		}

		float terrain_hash(vec2 point, const std::uint32_t seed)
		{
			const float seed_offset = static_cast<float>(seed) * 0.0009765625f;
			point = {
				fract01(point.x * 0.1031f + seed_offset),
				fract01(point.y * 0.11369f + seed_offset)
			};
			const vec2 hash_vector{ point.y + 19.19f + seed_offset * 7.0f, point.x + 19.19f + seed_offset * 7.0f };
			const float hash_offset = point.dot(hash_vector);
			point = add_vec2(point, { hash_offset, hash_offset });
			return fract01((point.x + point.y) * (point.x + 13.37f));
		}

		float terrain_noise(const vec2 point, const std::uint32_t seed)
		{
			const vec2 cell{ std::floor(point.x), std::floor(point.y) };
			const vec2 fraction{ fract01(point.x), fract01(point.y) };

			const float a = terrain_hash(cell, seed);
			const float b = terrain_hash(add_vec2(cell, { 1.0f, 0.0f }), seed);
			const float c = terrain_hash(add_vec2(cell, { 0.0f, 1.0f }), seed);
			const float d = terrain_hash(add_vec2(cell, { 1.0f, 1.0f }), seed);

			const vec2 smoothing{
				fraction.x * fraction.x * (3.0f - 2.0f * fraction.x),
				fraction.y * fraction.y * (3.0f - 2.0f * fraction.y)
			};
			return std::lerp(
				std::lerp(a, b, smoothing.x),
				std::lerp(c, d, smoothing.x),
				smoothing.y);
		}

		float terrain_fbm(vec2 point, const std::uint32_t seed)
		{
			float value = 0.0f;
			float amplitude = 0.5f;

			for (int i = 0; i < 7; ++i)
			{
				value += amplitude * terrain_noise(point, seed);
				point = add_vec2(scale_vec2(point, 2.03f), { 11.7f, -8.3f });
				amplitude *= 0.5f;
			}

			return value;
		}

		float terrain_ridged_fbm(vec2 point, const std::uint32_t seed)
		{
			float value = 0.0f;
			float amplitude = 0.55f;

			for (int i = 0; i < 6; ++i)
			{
				float noise_value = terrain_noise(point, seed);
				noise_value = 1.0f - std::abs(noise_value * 2.0f - 1.0f);
				value += noise_value * amplitude;
				point = add_vec2(scale_vec2(point, 2.18f), { -6.4f, 9.1f });
				amplitude *= 0.55f;
			}

			return value;
		}

		float generated_surface_radius(const vec2 direction, const ChunkSettings& settings)
		{
			const vec2 seed_offset = scale_vec2({ 0.0137f, 0.0211f }, static_cast<float>(settings.seed));
			const float macro = terrain_fbm(add_vec2(scale_vec2(direction, 1.85f), add_vec2(seed_offset, { 3.1f, -7.4f })), settings.seed);
			const float medium = terrain_fbm(add_vec2(scale_vec2(direction, 6.20f), { -seed_offset.y - 11.2f, -seed_offset.x + 4.6f }), settings.seed);
			const float ridges = terrain_ridged_fbm(add_vec2(scale_vec2(direction, 11.50f), add_vec2(scale_vec2(seed_offset, 1.3f), { 8.4f, -5.6f })), settings.seed);
			const float micro = terrain_fbm(add_vec2(scale_vec2(direction, 23.0f), add_vec2(scale_vec2(seed_offset, -0.75f), { -4.2f, 12.8f })), settings.seed);

			return settings.planet_radius +
				(macro - 0.5f) * settings.planet_radius * 0.19f +
				(medium - 0.5f) * settings.planet_radius * 0.07f +
				(ridges - 0.45f) * settings.planet_radius * 0.045f +
				(micro - 0.5f) * settings.planet_radius * 0.02f;
		}

		float clamp_terrain_density(const float density, const vec2 world_position, const ChunkSettings& settings)
		{
			const vec2 offset = subtract_vec2(world_position, settings.world_center);
			const float distance_from_center = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			const vec2 direction = distance_from_center > 1e-5f ? scale_vec2(offset, 1.0f / distance_from_center) : vec2{ 0.0f, 1.0f };
			const float base_density = generated_surface_radius(direction, settings) - distance_from_center;
			return std::clamp(density, -1.0f, std::max(1.0f, base_density));
		}

		float smooth01(const float value)
		{
			const float t = std::clamp(value, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
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
				value     += perlin_noise(point, seed + i * 131u) * amplitude;
				point     = { point.x * 2.04f - 4.8f, point.y * 2.04f + 9.2f };
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

		struct OreInstanceGpu final
		{
			vec2 center_world{ 0.0f, 0.0f };
			vec2 up{ 0.0f, -1.0f };
			float world_height{ 1.0f };
			float radial_offset{ 0.0f };
			float texture_layer{ 0.0f };
			float tile_column{ 0.0f };
			float tile_row{ 0.0f };
			float angle_offset{ 0.0f };
		};
	}

	struct PlanetTerrain::OreRenderResources final
	{
		GLuint vao{ 0 };
		GLuint quad_vbo{ 0 };
		GLuint instance_vbo{ 0 };
		GLuint texture_array{ 0 };
		GLsizei instance_count{ 0 };
		bool instances_dirty{ true };
		gfx::Shader shader{};

		~OreRenderResources()
		{
			if (texture_array != 0) glDeleteTextures(1, &texture_array);
			if (instance_vbo != 0) glDeleteBuffers(1, &instance_vbo);
			if (quad_vbo != 0) glDeleteBuffers(1, &quad_vbo);
			if (vao != 0) glDeleteVertexArrays(1, &vao);
		}
	};

	PlanetTerrain::~PlanetTerrain() = default;

	PlanetTerrain::PlanetTerrain(const b2WorldId world_id) :
		world_id_{ world_id }
	{
	}

	Result<void> PlanetTerrain::initialize()
	{
		const auto total_chunk_count = chunk_count();
		base_chunk_settings_.chunk_grid_size = total_chunk_count;
		const auto terrain_chunk_size = base_chunk_settings_.chunk_size;
		const auto terrain_world_center = base_chunk_settings_.world_center;
		terrain_cell_size_ = cell_size(base_chunk_settings_);
		TRY(load_overlay_assets());

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
			return {};
		}

		for (auto& chunk : chunks_)
		{
			auto chunk_initialize_result = chunk.initialize();
			if (!chunk_initialize_result)
			{
				return fail(chunk_initialize_result.error());
			}

			auto dispatch_result = chunk.dispatch_generation();
			if (!dispatch_result)
			{
				return fail(dispatch_result.error());
			}
		}

		for (auto& chunk : chunks_)
		{
			auto finalize_result = chunk.finalize_generation();
			if (!finalize_result)
			{
				return fail(finalize_result.error());
			}
		}

		return initialize_global_field();
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

		draw_resource_ores_gl(view);
		draw_vegetation_gl(view);
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
						spec.height = 1.8f;
						break;
					case PlantFamily::Flowers:
						spec.texture = &flowers_plant_texture_;
						spec.height = 2.0f;
						break;
					case PlantFamily::Bush:
						spec.texture = &bushes_plant_texture_;
						spec.height = 2.8f;
						break;
					case PlantFamily::Tree:
						spec.texture = &trees_plant_texture_;
						spec.tile_size = 64;
						spec.height = 5.2f;
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
						spec.height = 2.70f;
						break;
					case PlantFamily::Flowers:
						spec.texture = &flowers_plant_texture_;
						spec.height = 3.10f;
						break;
					case PlantFamily::Bush:
						spec.texture = &bushes_plant_texture_;
						spec.height = 4.8f;
						break;
					case PlantFamily::Tree:
						spec.texture = &trees_plant_texture_;
						spec.tile_size = 64;
						spec.height = 13.6f;
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

	void PlanetTerrain::update_active_water_colliders(const vec2 player_position)
	{
		if (water_blob_colliders_.empty()) return;

		const float activation_padding = std::max(base_chunk_settings_.chunk_size.x, base_chunk_settings_.chunk_size.y) * 1.5f;
		for (auto& blob : water_blob_colliders_)
		{
			const bool active =
				player_position.x >= blob.bounds_min.x - activation_padding &&
				player_position.x <= blob.bounds_max.x + activation_padding &&
				player_position.y >= blob.bounds_min.y - activation_padding &&
				player_position.y <= blob.bounds_max.y + activation_padding;
			blob.collider.set_water_enabled(active);
		}
	}

	void PlanetTerrain::flush_pending_ground_brush_changes()
	{
		if (pending_ground_brush_changed_coords_.empty() || pending_ground_brush_dirty_chunks_.empty()) return;
		const auto padded_size = padded_field_size(base_chunk_settings_);
		const auto stride = chunk_sample_stride(base_chunk_settings_);
		std::unordered_set<std::uint64_t> cleared_keys;

		for (std::size_t i = 0; i < chunks_.size(); ++i)
		{
			if (!pending_ground_brush_dirty_chunks_[i]) continue;
			if (!chunks_[i].has_pending_gpu_ground_brush()) continue;

			auto synced_field = chunks_[i].finalize_gpu_ground_brush();
			if (!synced_field)
			{
				Log::error("Failed to finalize GPU ground brush on chunk ({}, {}): {}",
					chunks_[i].chunk_coord().x,
					chunks_[i].chunk_coord().y,
					synced_field.error().message);
				continue;
			}

			const ivec2 chunk_base{
				chunks_[i].chunk_coord().x * stride.x,
				chunks_[i].chunk_coord().y * stride.y
			};

			for (std::uint32_t y = 0; y < padded_size.y; ++y)
			{
				for (std::uint32_t x = 0; x < padded_size.x; ++x)
				{
					const ivec2 global_coord{
						chunk_base.x + static_cast<int>(x),
						chunk_base.y + static_cast<int>(y)
					};
					if (!is_valid_global_sample(global_coord)) continue;

					const auto global_index = global_field_index(global_coord);
					const auto& previous_sample = global_field_[global_index];
					const auto& next_sample = (*synced_field)[static_cast<std::size_t>(y) * padded_size.x + x];
					if (is_solid(previous_sample) && !is_solid(next_sample) && global_index < plant_samples_.size())
					{
						if (plant_samples_[global_index].stage != PlantStage::Empty)
						{
							plant_samples_[global_index] = {};
							active_plants_dirty_ = true;
						}
						cleared_keys.insert(sample_key(global_coord));
					}

					global_field_[global_index] = next_sample;
				}
			}
		}

		remove_resource_nodes(cleared_keys);

		if (pending_ground_brush_requires_wetness_rebuild_)
		{
			recompute_wetness_around(pending_ground_brush_changed_coords_, pending_ground_brush_dirty_chunks_);
		}

		if (const auto rebuild_result = rebuild_dirty_chunks(pending_ground_brush_dirty_chunks_); !rebuild_result)
		{
			Log::error(rebuild_result.error());
		}

		pending_ground_brush_changed_coords_.clear();
		std::fill(pending_ground_brush_dirty_chunks_.begin(), pending_ground_brush_dirty_chunks_.end(), false);
		pending_ground_brush_requires_wetness_rebuild_ = false;
	}

	void PlanetTerrain::advance_plants(const float dt)
	{
		static constexpr float seed_to_sprout_time = 1.1f;
		static constexpr float sprout_to_mature_time = 2.1f;

		if (plant_samples_.empty() || dt <= 0.0f) return;
		if (active_plants_dirty_) compact_active_plants();
		bool plants_changed = false;

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
				plants_changed = true;
			}
			else if (plant.stage == PlantStage::Sprout && plant.age >= sprout_to_mature_time)
			{
				plant.stage = PlantStage::Mature;
				plants_changed = true;
			}
		}

		plants_changed = spread_plants() || plants_changed;
		if (!plants_changed) return;

		std::vector<bool> dirty_chunks(chunks_.size(), false);
		recompute_ground_greenness(dirty_chunks);
		if (const auto rebuild_result = rebuild_dirty_chunks(dirty_chunks); !rebuild_result)
		{
			Log::error(rebuild_result.error());
		}
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

	std::optional<PlanetTerrain::SurfaceAttachment> PlanetTerrain::exposed_surface_attachment(const ivec2 coord) const
	{
		if (!is_valid_global_sample(coord)) return std::nullopt;
		if (!is_solid(global_field_[global_field_index(coord)])) return std::nullopt;

		const vec2 center = global_sample_world_position(coord);
		const vec2 radial_up = normalize_vec2(subtract_vec2(center, base_chunk_settings_.world_center));
		vec2 surface_up{ 0.0f, 0.0f };
		int open_neighbors = 0;

		for (int oy = -1; oy <= 1; ++oy)
		{
			for (int ox = -1; ox <= 1; ++ox)
			{
				if (ox == 0 && oy == 0) continue;

				const ivec2 neighbor{ coord.x + ox, coord.y + oy };
				if (!is_valid_global_sample(neighbor)) continue;
				if (is_solid(global_field_[global_field_index(neighbor)])) continue;

				surface_up = add_vec2(
					surface_up,
					normalize_vec2(subtract_vec2(global_sample_world_position(neighbor), center), { 0.0f, 0.0f }));
				++open_neighbors;
			}
		}

		if (open_neighbors == 0) return std::nullopt;
		surface_up = normalize_vec2(surface_up, radial_up);

		const float step_size = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 0.10f, 0.02f);
		const float max_distance = std::max(std::min(terrain_cell_size_.x, terrain_cell_size_.y) * 3.4f, 0.45f);
		vec2 last_solid = center;

		for (float distance = step_size; distance <= max_distance; distance += step_size)
		{
			const vec2 probe = add_vec2(center, scale_vec2(surface_up, distance));
			const auto probe_coord = world_to_global_sample(probe);
			if (!is_valid_global_sample(probe_coord) || !is_solid(global_field_[global_field_index(probe_coord)]))
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

				return SurfaceAttachment{
					.anchor_world = lerp_vec2(low, high, 0.5f),
					.surface_up = surface_up,
					.floor_alignment = std::clamp(surface_up.dot(radial_up), -1.0f, 1.0f)
				};
			}

			last_solid = probe;
		}

		return std::nullopt;
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

	bool PlanetTerrain::is_surface_suitable_for_plant(const ivec2 coord) const
	{
		const auto attachment = exposed_surface_attachment(coord);
		if (!attachment.has_value()) return false;
		if (normalized_depth(attachment->anchor_world) > 0.12f) return false;
		return attachment->floor_alignment >= 0.74f;
	}

	bool PlanetTerrain::has_resource_at(const ivec2 coord) const
	{
		return std::ranges::any_of(resource_nodes_, [coord](const ResourceNode& node)
		{
			return node.coord.x == coord.x && node.coord.y == coord.y;
		});
	}

	std::uint32_t PlanetTerrain::nearby_cover_count(const ivec2 coord, const float radius_samples, const bool woody_cover) const
	{
		const float radius_sq = radius_samples * radius_samples;
		std::uint32_t count = 0u;
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Empty) continue;
			if (woody_cover ? !is_woody_family(plant.family) : !is_low_cover_family(plant.family)) continue;

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

	bool PlanetTerrain::can_place_woody_near(const ivec2 coord, const int min_spacing_samples) const
	{
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Empty) continue;
			if (!is_woody_family(plant.family)) continue;

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
				if (is_low_cover_family(family))
				{
					if (nearby_cover_count(coord, 4.8f, false) >= 4u) continue;
				}
				else
				{
					const int spacing = family == PlantFamily::Tree ? 24 : 12;
					if (!can_place_woody_near(coord, spacing)) continue;
					if (nearby_cover_count(coord, family == PlantFamily::Tree ? 16.0f : 9.0f, true) >=
						(family == PlantFamily::Tree ? 1u : 3u)) continue;
				}

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

		if (roll > 0.86f && tree_ratio < 0.36f && can_place_woody_near(coord, 12)) return PlantFamily::Tree;
		if (roll > 0.76f) return PlantFamily::Bush;
		if (roll > 0.75f) return PlantFamily::Flowers;
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

	bool PlanetTerrain::spread_plants()
	{
		if (active_plant_indices_.empty()) return false;

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
				plant.family == PlantFamily::Tree ? 3.4f :
				plant.family == PlantFamily::Bush ? 4.8f :
				plant.family == PlantFamily::Flowers ? 6.2f : 5.2f;
			if (plant.stage != PlantStage::Mature || plant.spread_age < spread_interval) continue;
			plant.spread_age = 0.0f;

			const ivec2 origin{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			const int radius = plant.family == PlantFamily::Tree ? 15 : plant.family == PlantFamily::Bush ? 8 : 5;
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
				if (is_low_cover_family(family))
				{
					if (nearby_cover_count(coord, 4.8f, false) >= 4u) continue;
				}
				else
				{
					const int spacing = family == PlantFamily::Tree ? 12 : 10;
					if (!can_place_woody_near(coord, spacing)) continue;
					if (nearby_cover_count(coord, family == PlantFamily::Tree ? 11.0f : 8.5f, true) >=
						(family == PlantFamily::Tree ? 4u : 4u)) continue;
				}

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

		return !spawned_plants.empty();
	}

	Result<void> PlanetTerrain::try_harvest_resource(const vec2 world_position)
	{
		if (resource_nodes_.empty()) return fail("No harvestable resources are available");

		const float harvest_radius_sq = 1.75f * 1.75f;
		std::optional<std::size_t> best_index;
		float best_distance_sq = harvest_radius_sq;

		for (std::size_t i = 0; i < resource_nodes_.size(); ++i)
		{
			const auto& resource = resource_nodes_[i];
			if (!is_valid_global_sample(resource.coord)) continue;
			const auto& sample = global_field_[global_field_index(resource.coord)];
			if (!is_solid(sample)) continue;

			const vec2 resource_world = resource.surface_attached ? resource.anchor_world : global_sample_world_position(resource.coord);
			const vec2 delta = subtract_vec2(resource_world, world_position);
			const float distance_sq = delta.x * delta.x + delta.y * delta.y;
			if (distance_sq >= best_distance_sq) continue;

			best_index = i;
			best_distance_sq = distance_sq;
		}

		if (!best_index.has_value()) return fail("No resource is close enough to harvest");

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
		if (ore_render_resources_) ore_render_resources_->instances_dirty = true;
		return {};
	}

	Result<void> PlanetTerrain::plant_seed(const vec2 world_position)
	{
		if (inventory_.seeds == 0u) return fail("Cannot plant seed: inventory is empty");
		if (global_field_.empty() || plant_samples_.empty())
		{
			return fail("Cannot plant seed: terrain field is not initialized");
		}

		const auto seed_coord = find_plantable_seed_coord(world_position);
		if (!seed_coord.has_value()) return fail("No valid planting spot is within reach");

		const auto coord = *seed_coord;
		const auto index = global_field_index(coord);
		const auto family = choose_plant_family(coord);
		const auto variant = choose_plant_variant(coord, family);
		const auto anchor = surface_anchor_world(coord);
		if (!anchor.has_value()) return fail("Failed to resolve a surface anchor for the planted seed");
		plant_samples_[index] = {
			.stage = PlantStage::Seeded,
			.family = family,
			.age = 0.0f,
			.spread_age = 0.0f,
			.variant = variant,
			.anchor_world = *anchor
		};
		active_plant_indices_.push_back(index);
		std::vector<bool> dirty_chunks(chunks_.size(), false);
		recompute_ground_greenness(dirty_chunks);
		if (auto res = rebuild_dirty_chunks(dirty_chunks); !res) return fail(res.error());

		--inventory_.seeds;
		return {};
	}

	void PlanetTerrain::grant_seeds(const std::uint32_t amount)
	{
		inventory_.seeds += amount;
	}

	std::uint32_t PlanetTerrain::apply_ground_brush(const TerrainEdit& edit, const std::uint32_t unit_budget,
		const std::optional<GroundBrushBlocker>& blocker)
	{
		if (global_field_.empty() || unit_budget == 0u) return 0u;
		if (pending_ground_brush_dirty_chunks_.empty()) pending_ground_brush_dirty_chunks_.assign(chunks_.size(), false);

		const vec2 edit_center{
			edit.position_radius_strength.x,
			edit.position_radius_strength.y
		};
		const float radius = std::max(edit.position_radius_strength.z, 0.0f);
		if (radius <= 0.0f) return 0u;
		const auto stride = chunk_sample_stride(base_chunk_settings_);
		std::uint32_t remaining_budget = unit_budget;
		std::uint32_t units = 0u;

		for (std::size_t i = 0; i < chunks_.size(); ++i)
		{
			if (!circle_overlaps_rect(edit_center, radius, chunks_[i].display_min(), chunks_[i].display_max())) continue;

			auto gpu_result = chunks_[i].apply_ground_brush_gpu(edit, remaining_budget, blocker);
			if (!gpu_result)
			{
				Log::error("Failed GPU ground brush on chunk ({}, {}): {}",
					chunks_[i].chunk_coord().x,
					chunks_[i].chunk_coord().y,
					gpu_result.error().message);
				continue;
			}

			if (gpu_result->changed_any == 0u) continue;
			pending_ground_brush_dirty_chunks_[i] = true;
			pending_ground_brush_requires_wetness_rebuild_ =
				pending_ground_brush_requires_wetness_rebuild_ || gpu_result->touched_water_or_wet != 0u;

			const ivec2 chunk_base{
				chunks_[i].chunk_coord().x * stride.x,
				chunks_[i].chunk_coord().y * stride.y
			};
			pending_ground_brush_changed_coords_.push_back({
				chunk_base.x + gpu_result->changed_min.x,
				chunk_base.y + gpu_result->changed_min.y
			});
			pending_ground_brush_changed_coords_.push_back({
				chunk_base.x + gpu_result->changed_max.x,
				chunk_base.y + gpu_result->changed_max.y
			});

			units += edit.position_radius_strength.w < 0.0f ? gpu_result->removed_units : gpu_result->placed_units;
			remaining_budget = remaining_budget > gpu_result->applied_samples ? remaining_budget - gpu_result->applied_samples : 0u;
		}

		return units;
	}

	Result<std::uint32_t> PlanetTerrain::place_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		std::uint32_t existing_volume = 0u;
		const auto plan = build_targeted_water_plan(world_position, volume_cap, false, &existing_volume);
		if (!plan.has_value()) return fail("Could not build a water placement plan at the selected location");

		if (auto res = apply_water_plan_and_rebuild(*plan); !res) return fail(res.error());

		return plan->wet_sample_count > existing_volume ? plan->wet_sample_count - existing_volume : 0u;
	}

	Result<std::uint32_t> PlanetTerrain::pickup_water(const vec2 world_position, const std::uint32_t volume_cap)
	{
		std::uint32_t existing_volume = 0u;
		const auto plan = build_targeted_water_plan(world_position, volume_cap, true, &existing_volume);
		if (!plan.has_value()) return fail("Could not find a water volume to collect from the selected location");

		if (auto res = apply_water_plan_and_rebuild(*plan); !res) return fail(res.error());

		return existing_volume > plan->wet_sample_count ? existing_volume - plan->wet_sample_count : 0u;
	}

	Result<std::optional<PlanetTerrain::WaterPreviewMesh>> PlanetTerrain::build_water_preview_mesh(const vec2 world_position,
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

	Result<PlanetTerrain::WaterPreviewMesh> PlanetTerrain::render_water_preview_patch(const WaterPreviewPatch& patch) const
	{
		TerrainGenerator current_generator{};
		TRY(current_generator.initialize(patch.settings));
		TRY(current_generator.upload_field(patch.current_samples));
		TRY(current_generator.smooth_water_field());
		TRY(current_generator.dispatch_surface_rebuild(TerrainGenerator::water_channel_index, 0.0f));
		auto current_mesh = current_generator.readback();
		if (!current_mesh) return fail(current_mesh.error());

		TerrainGenerator future_generator{};
		TRY(future_generator.initialize(patch.settings));
		TRY(future_generator.upload_field(patch.future_samples));
		TRY(future_generator.smooth_water_field());
		TRY(future_generator.dispatch_surface_rebuild(TerrainGenerator::water_channel_index, 0.0f));
		auto future_mesh = future_generator.readback();
		if (!future_mesh) return fail(future_mesh.error());

		return WaterPreviewMesh{
			.current_vertices = current_mesh->mesh_vertices,
			.current_indices = current_mesh->mesh_indices,
			.future_vertices = future_mesh->mesh_vertices,
			.future_indices = future_mesh->mesh_indices
		};
	}

	std::size_t PlanetTerrain::patch_index(const int x, const int y, const std::uint32_t width)
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
	}

	Result<fs::path> PlanetTerrain::save_chunk_field_image(const vec2 world_position) const
	{
		if (global_field_.empty()) return fail("Cannot export chunk field: global terrain field is empty");

		const auto chunk_coord = chunk_index_from_world(world_position);
		const auto field_samples = extract_chunk_field(chunk_coord);
		const auto padded_size = padded_field_size(base_chunk_settings_);
		if (field_samples.empty() || padded_size.x == 0 || padded_size.y == 0)
		{
			return fail("Cannot export chunk ({}, {}): field data is empty", chunk_coord.x, chunk_coord.y);
		}

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
		std::error_code directory_error;
		fs::create_directories(output_dir, directory_error);
		if (directory_error)
		{
			return fail(
				"Failed to create export directory '{}': {}",
				output_dir.string(),
				directory_error.message());
		}

		const auto base_name = std::format(
			"chunk_{}_{}_{}",
			chunk_coord.x,
			chunk_coord.y,
			timestamp);
		const fs::path image_path = output_dir / (base_name + ".png");

		if (!image.saveToFile(image_path.string()))
		{
			return fail("Failed to save chunk field image '{}'", image_path.string());
		}

		return image_path;
	}

	Result<void> PlanetTerrain::load_overlay_assets()
	{
		if (overlay_assets_ready_) return {};

		auto load_texture = [](sf::Texture& texture, const char* path) -> Result<void>
		{
			if (!texture.loadFromFile(path))
			{
				return fail("Failed to load texture '{}'", path);
			}

			texture.setSmooth(false);
			return {};
		};

		TRY(load_texture(rock_node_texture_, "assets/images/ores/rock.png"));
		TRY(load_texture(iron_ore_texture_, "assets/images/ores/iron_ore.png"));
		TRY(load_texture(bronze_ore_texture_, "assets/images/ores/bronze_ore.png"));
		TRY(load_texture(gold_ore_texture_, "assets/images/ores/gold_ore.png"));
		TRY(load_texture(diamond_ore_texture_, "assets/images/ores/diamond_ore.png"));
		TRY(load_texture(processed_resource_texture_, "assets/images/ores/processed_ores.png"));
		TRY(load_texture(live_plant_texture_, "assets/images/vegetation/ground_plants.png"));
		TRY(load_texture(grass_plant_texture_, "assets/images/vegetation/grass.png"));
		TRY(load_texture(flowers_plant_texture_, "assets/images/vegetation/flowers.png"));
		TRY(load_texture(bushes_plant_texture_, "assets/images/vegetation/bushes.png"));
		TRY(load_texture(trees_plant_texture_, "assets/images/vegetation/trees.png"));

		for (std::size_t i = 0; i < dead_plant_sprite_families.size(); ++i)
		{
			TRY(load_texture(dead_plant_textures_[i], dead_plant_sprite_families[i].path));
		}

		if (!ui_font_.openFromFile("assets/fonts/arial.ttf"))
		{
			return fail("Failed to load font 'assets/fonts/arial.ttf'");
		}

		overlay_assets_ready_ = true;
		return {};
	}

	Result<void> PlanetTerrain::initialize_ore_render_resources() const
	{
		if (ore_render_resources_) return {};

		struct QuadVertex final
		{
			vec2 position{ 0.0f, 0.0f };
			vec2 uv{ 0.0f, 0.0f };
		};

		auto resources = std::make_unique<OreRenderResources>();
		auto shader = gfx::Shader::from_graphics_files(
			"assets/shaders/resource_instances.vert",
			"assets/shaders/resource_instances.frag");
		if (!shader) return fail(shader.error());
		resources->shader = std::move(*shader);

		glCreateVertexArrays(1, &resources->vao);
		glCreateBuffers(1, &resources->quad_vbo);
		glCreateBuffers(1, &resources->instance_vbo);

		static constexpr std::array quad_vertices{
			QuadVertex{ .position = { -0.5f, 0.0f }, .uv = { 0.0f, 1.0f } },
			QuadVertex{ .position = { 0.5f, 0.0f }, .uv = { 1.0f, 1.0f } },
			QuadVertex{ .position = { -0.5f, -1.0f }, .uv = { 0.0f, 0.0f } },
			QuadVertex{ .position = { 0.5f, -1.0f }, .uv = { 1.0f, 0.0f } }
		};

		glNamedBufferData(
			resources->quad_vbo,
			static_cast<GLsizeiptr>(sizeof(quad_vertices)),
			quad_vertices.data(),
			GL_STATIC_DRAW);
		glNamedBufferData(resources->instance_vbo, static_cast<GLsizeiptr>(sizeof(OreInstanceGpu)), nullptr, GL_DYNAMIC_DRAW);

		glVertexArrayVertexBuffer(resources->vao, 0, resources->quad_vbo, 0, sizeof(QuadVertex));
		glVertexArrayVertexBuffer(resources->vao, 1, resources->instance_vbo, 0, sizeof(OreInstanceGpu));

		glEnableVertexArrayAttrib(resources->vao, 0);
		glVertexArrayAttribFormat(resources->vao, 0, 2, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, position));
		glVertexArrayAttribBinding(resources->vao, 0, 0);

		glEnableVertexArrayAttrib(resources->vao, 1);
		glVertexArrayAttribFormat(resources->vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, uv));
		glVertexArrayAttribBinding(resources->vao, 1, 0);

		glEnableVertexArrayAttrib(resources->vao, 2);
		glVertexArrayAttribFormat(resources->vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, center_world));
		glVertexArrayAttribBinding(resources->vao, 2, 1);

		glEnableVertexArrayAttrib(resources->vao, 3);
		glVertexArrayAttribFormat(resources->vao, 3, 2, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, up));
		glVertexArrayAttribBinding(resources->vao, 3, 1);

		glEnableVertexArrayAttrib(resources->vao, 4);
		glVertexArrayAttribFormat(resources->vao, 4, 4, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, world_height));
		glVertexArrayAttribBinding(resources->vao, 4, 1);

		glEnableVertexArrayAttrib(resources->vao, 5);
		glVertexArrayAttribFormat(resources->vao, 5, 1, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, tile_row));
		glVertexArrayAttribBinding(resources->vao, 5, 1);

		glEnableVertexArrayAttrib(resources->vao, 6);
		glVertexArrayAttribFormat(resources->vao, 6, 1, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, angle_offset));
		glVertexArrayAttribBinding(resources->vao, 6, 1);

		glVertexArrayBindingDivisor(resources->vao, 1, 1);

		std::array<sf::Image, 5> ore_images{
			rock_node_texture_.copyToImage(),
			iron_ore_texture_.copyToImage(),
			bronze_ore_texture_.copyToImage(),
			gold_ore_texture_.copyToImage(),
			diamond_ore_texture_.copyToImage()
		};

		const auto image_size = ore_images.front().getSize();
		for (const auto& image : ore_images)
		{
			if (image.getSize() != image_size)
			{
				return fail("Ore texture sheets must share the same size for instanced rendering");
			}
		}

		glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &resources->texture_array);
		glTextureStorage3D(
			resources->texture_array,
			1,
			GL_RGBA8,
			static_cast<GLsizei>(image_size.x),
			static_cast<GLsizei>(image_size.y),
			static_cast<GLsizei>(ore_images.size()));

		for (std::size_t layer = 0; layer < ore_images.size(); ++layer)
		{
			glTextureSubImage3D(
				resources->texture_array,
				0,
				0,
				0,
				static_cast<GLint>(layer),
				static_cast<GLsizei>(image_size.x),
				static_cast<GLsizei>(image_size.y),
				1,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				ore_images[layer].getPixelsPtr());
		}

		glTextureParameteri(resources->texture_array, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(resources->texture_array, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(resources->texture_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(resources->texture_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		ore_render_resources_ = std::move(resources);
		return {};
	}

	Result<void> PlanetTerrain::rebuild_ore_instances() const
	{
		if (auto res = initialize_ore_render_resources(); !res) return fail(res.error());
		if (!ore_render_resources_) return {};

		auto& resources = *ore_render_resources_;
		if (!resources.instances_dirty) return {};

		std::vector<OreInstanceGpu> instances;
		instances.reserve(resource_nodes_.size());

		auto texture_layer_for = [](const ResourceKind kind)
		{
			switch (kind)
			{
				case ResourceKind::Rock: return 0.0f;
				case ResourceKind::IronOre: return 1.0f;
				case ResourceKind::BronzeOre: return 2.0f;
				case ResourceKind::GoldOre: return 3.0f;
				case ResourceKind::DiamondOre: return 4.0f;
				case ResourceKind::DeadPlant: return -1.0f;
			}

			return -1.0f;
		};

		for (const auto& resource : resource_nodes_)
		{
			if (resource.kind == ResourceKind::DeadPlant) continue;
			if (!is_valid_global_sample(resource.coord)) continue;

			const auto& sample = global_field_[global_field_index(resource.coord)];
			if (!is_solid(sample)) continue;

			const vec2 world_position = resource.surface_attached ? resource.anchor_world : global_sample_world_position(resource.coord);
			const vec2 up = resource.surface_up.lengthSquared() > 1e-6f ?
				normalize_vec2(scale_vec2(resource.surface_up, -1.0f)) :
				normalize_vec2(subtract_vec2(world_position, base_chunk_settings_.world_center));
			const bool exposed = is_exposed_to_air(sample, solid_neighbor_count(resource.coord));

			float world_height = 1.96f;
			float radial_offset = 0.01f;
			if (resource.surface_attached)
			{
				world_height = resource.cave_variant ? 2.24f : 2.16f;
				radial_offset = exposed ? 0.06f : 0.03f;
			}

			instances.push_back({
				.center_world = world_position,
				.up = up,
				.world_height = world_height,
				.radial_offset = radial_offset,
				.texture_layer = texture_layer_for(resource.kind),
				.tile_column = resource.cave_variant ? 7.0f : 0.0f,
				.tile_row = resource.cave_variant ?
					static_cast<float>(resource.variant % 16u) :
					static_cast<float>(16u + resource.variant % 16u),
				.angle_offset = 0.0f
			});
		}

		if (instances.empty())
		{
			OreInstanceGpu dummy{};
			glNamedBufferData(resources.instance_vbo, static_cast<GLsizeiptr>(sizeof(dummy)), &dummy, GL_DYNAMIC_DRAW);
			resources.instance_count = 0;
			resources.instances_dirty = false;
			return {};
		}

		glNamedBufferData(
			resources.instance_vbo,
			static_cast<GLsizeiptr>(instances.size() * sizeof(OreInstanceGpu)),
			instances.data(),
			GL_DYNAMIC_DRAW);
		resources.instance_count = static_cast<GLsizei>(instances.size());
		resources.instances_dirty = false;
		return {};
	}

	void PlanetTerrain::draw_resource_ores_gl(const sf::View& view) const
	{
		if (!overlay_assets_ready_) return;

		if (const auto rebuild_result = rebuild_ore_instances(); !rebuild_result)
		{
			Log::error(rebuild_result.error());
			return;
		}

		if (!ore_render_resources_) return;

		const auto& resources = *ore_render_resources_;
		if (resources.instance_count == 0 || !resources.shader.valid()) return;

		const gfx::ScopedAlphaBlendPass blend_pass{};
		static_cast<void>(blend_pass);

		if (const auto use_result = resources.shader.use(); !use_result)
		{
			Log::error(use_result.error());
			return;
		}

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D_ARRAY, resources.texture_array);
		resources.shader.set_uniform("uProjection", gfx::make_projection(view));
		resources.shader.set_uniform("uOreTextureArray", 0);
		resources.shader.set_uniform("uTileSizePixels", 32);

		glBindVertexArray(resources.vao);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, resources.instance_count);
		glBindVertexArray(0);

		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glActiveTexture(GL_TEXTURE0);
	}

	Result<void> PlanetTerrain::initialize_vegetation_render_resources() const
	{
		if (vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Live32)] &&
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Live64)] &&
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Dead32)] &&
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Dead64)]) return {};

		struct QuadVertex final
		{
			vec2 position{ 0.0f, 0.0f };
			vec2 uv{ 0.0f, 0.0f };
		};

		const std::array live_32_textures{
			&live_plant_texture_,
			&grass_plant_texture_,
			&flowers_plant_texture_,
			&bushes_plant_texture_
		};
		const std::array live_64_textures{
			&trees_plant_texture_
		};
		const std::array dead_32_textures{
			&dead_plant_textures_[0],
			&dead_plant_textures_[1],
			&dead_plant_textures_[2],
			&dead_plant_textures_[3],
			&dead_plant_textures_[4],
			&dead_plant_textures_[5],
			&dead_plant_textures_[6],
			&dead_plant_textures_[7],
			&dead_plant_textures_[8]
		};
		const std::array dead_64_textures{
			&dead_plant_textures_[9]
		};

		auto initialize_batch = [&](std::unique_ptr<VegetationRenderResources>& resource_slot,
			const auto& textures,
			const std::uint32_t tile_size_pixels) -> Result<void>
		{
			if (resource_slot) return {};

			auto resources = std::make_unique<VegetationRenderResources>();
			auto shader = gfx::Shader::from_graphics_files(
				"assets/shaders/resource_instances.vert",
				"assets/shaders/resource_instances.frag");
			if (!shader) return fail(shader.error());
			resources->shader = std::move(*shader);
			resources->tile_size_pixels = tile_size_pixels;

			glCreateVertexArrays(1, &resources->vao);
			glCreateBuffers(1, &resources->quad_vbo);
			glCreateBuffers(1, &resources->instance_vbo);

			static constexpr std::array quad_vertices{
				QuadVertex{ .position = { -0.5f, 0.0f }, .uv = { 0.0f, 1.0f } },
				QuadVertex{ .position = { 0.5f, 0.0f }, .uv = { 1.0f, 1.0f } },
				QuadVertex{ .position = { -0.5f, -1.0f }, .uv = { 0.0f, 0.0f } },
				QuadVertex{ .position = { 0.5f, -1.0f }, .uv = { 1.0f, 0.0f } }
			};

			glNamedBufferData(
				resources->quad_vbo,
				static_cast<GLsizeiptr>(sizeof(quad_vertices)),
				quad_vertices.data(),
				GL_STATIC_DRAW);
			glNamedBufferData(resources->instance_vbo, static_cast<GLsizeiptr>(sizeof(OreInstanceGpu)), nullptr, GL_DYNAMIC_DRAW);

			glVertexArrayVertexBuffer(resources->vao, 0, resources->quad_vbo, 0, sizeof(QuadVertex));
			glVertexArrayVertexBuffer(resources->vao, 1, resources->instance_vbo, 0, sizeof(OreInstanceGpu));

			glEnableVertexArrayAttrib(resources->vao, 0);
			glVertexArrayAttribFormat(resources->vao, 0, 2, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, position));
			glVertexArrayAttribBinding(resources->vao, 0, 0);

			glEnableVertexArrayAttrib(resources->vao, 1);
			glVertexArrayAttribFormat(resources->vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, uv));
			glVertexArrayAttribBinding(resources->vao, 1, 0);

			glEnableVertexArrayAttrib(resources->vao, 2);
			glVertexArrayAttribFormat(resources->vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, center_world));
			glVertexArrayAttribBinding(resources->vao, 2, 1);

			glEnableVertexArrayAttrib(resources->vao, 3);
			glVertexArrayAttribFormat(resources->vao, 3, 2, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, up));
			glVertexArrayAttribBinding(resources->vao, 3, 1);

			glEnableVertexArrayAttrib(resources->vao, 4);
			glVertexArrayAttribFormat(resources->vao, 4, 4, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, world_height));
			glVertexArrayAttribBinding(resources->vao, 4, 1);

			glEnableVertexArrayAttrib(resources->vao, 5);
			glVertexArrayAttribFormat(resources->vao, 5, 1, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, tile_row));
			glVertexArrayAttribBinding(resources->vao, 5, 1);

			glEnableVertexArrayAttrib(resources->vao, 6);
			glVertexArrayAttribFormat(resources->vao, 6, 1, GL_FLOAT, GL_FALSE, offsetof(OreInstanceGpu, angle_offset));
			glVertexArrayAttribBinding(resources->vao, 6, 1);

			glVertexArrayBindingDivisor(resources->vao, 1, 1);

			std::vector<sf::Image> images;
			images.reserve(textures.size());
			for (const auto* texture : textures)
			{
				images.push_back(texture->copyToImage());
			}

			const auto image_size = images.front().getSize();
			for (const auto& image : images)
			{
				if (image.getSize() != image_size)
				{
					return fail("Vegetation texture sheets must share the same size for instanced rendering");
				}
			}

			glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &resources->texture_array);
			glTextureStorage3D(
				resources->texture_array,
				1,
				GL_RGBA8,
				static_cast<GLsizei>(image_size.x),
				static_cast<GLsizei>(image_size.y),
				static_cast<GLsizei>(images.size()));

			for (std::size_t layer = 0; layer < images.size(); ++layer)
			{
				glTextureSubImage3D(
					resources->texture_array,
					0,
					0,
					0,
					static_cast<GLint>(layer),
					static_cast<GLsizei>(image_size.x),
					static_cast<GLsizei>(image_size.y),
					1,
					GL_RGBA,
					GL_UNSIGNED_BYTE,
					images[layer].getPixelsPtr());
			}

			glTextureParameteri(resources->texture_array, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(resources->texture_array, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureParameteri(resources->texture_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(resources->texture_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			resource_slot = std::move(resources);
			return {};
		};

		if (auto res = initialize_batch(
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Live32)],
			live_32_textures,
			32u); !res) return fail(res.error());

		if (auto res = initialize_batch(
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Live64)],
			live_64_textures,
			64u); !res) return fail(res.error());

		if (auto res = initialize_batch(
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Dead32)],
			dead_32_textures,
			32u); !res) return fail(res.error());

		if (auto res = initialize_batch(
			vegetation_render_resources_[static_cast<std::size_t>(VegetationBatchId::Dead64)],
			dead_64_textures,
			64u); !res) return fail(res.error());

		return {};
	}

	void PlanetTerrain::draw_vegetation_gl(const sf::View& view) const
	{
		if (!overlay_assets_ready_) return;
		if (const auto init_result = initialize_vegetation_render_resources(); !init_result)
		{
			Log::error(init_result.error());
			return;
		}

		const vec2 view_center{ view.getCenter().x, view.getCenter().y };
		const vec2 view_size{ std::abs(view.getSize().x), std::abs(view.getSize().y) };
		const float visible_radius = std::sqrt(view_size.x * view_size.x + view_size.y * view_size.y) * 0.5f + 4.0f;
		const float visible_radius_sq = visible_radius * visible_radius;

		std::array<std::vector<OreInstanceGpu>, vegetation_batch_count> instances_by_batch;
		std::vector<OreInstanceGpu> low_cover_live32_instances;
		std::vector<OreInstanceGpu> woody_live32_instances;

		auto vegetation_texture_info = [&](const sf::Texture* texture) -> std::optional<std::pair<VegetationBatchId, float>>
		{
			if (texture == &live_plant_texture_) return std::pair{ VegetationBatchId::Live32, 0.0f };
			if (texture == &grass_plant_texture_) return std::pair{ VegetationBatchId::Live32, 1.0f };
			if (texture == &flowers_plant_texture_) return std::pair{ VegetationBatchId::Live32, 2.0f };
			if (texture == &bushes_plant_texture_) return std::pair{ VegetationBatchId::Live32, 3.0f };
			if (texture == &trees_plant_texture_) return std::pair{ VegetationBatchId::Live64, 0.0f };

			for (std::size_t i = 0; i < dead_plant_sprite_families.size(); ++i)
			{
				if (texture != &dead_plant_textures_[i]) continue;
				return std::pair{
					i == dead_plant_sprite_families.size() - 1u ? VegetationBatchId::Dead64 : VegetationBatchId::Dead32,
					static_cast<float>(i == dead_plant_sprite_families.size() - 1u ? 0u : i)
				};
			}

			return std::nullopt;
		};

		auto push_instance = [&](const VegetationBatchId batch_id, const OreInstanceGpu instance)
		{
			instances_by_batch[static_cast<std::size_t>(batch_id)].push_back(instance);
		};

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
			const vec2 anchor_world = plant.anchor_world.lengthSquared() > 1e-6f ? plant.anchor_world : world_position;
			const vec2 delta = subtract_vec2(anchor_world, view_center);
			if (delta.x * delta.x + delta.y * delta.y > visible_radius_sq) continue;

			const auto spec = plant_visual_spec(plant);
			if (spec.texture == nullptr) continue;
			const auto texture_info = vegetation_texture_info(spec.texture);
			if (!texture_info.has_value()) continue;

			const vec2 up = normalize_vec2(subtract_vec2(anchor_world, base_chunk_settings_.world_center));
			const OreInstanceGpu instance{
				.center_world = anchor_world,
				.up = up,
				.world_height = spec.height,
				.radial_offset = spec.radial_offset,
				.texture_layer = texture_info->second,
				.tile_column = static_cast<float>(spec.column),
				.tile_row = static_cast<float>(plant.variant % 16u),
				.angle_offset = spec.angle_offset
			};

			if (texture_info->first == VegetationBatchId::Live32)
			{
				if (plant.family == PlantFamily::Bush) woody_live32_instances.push_back(instance);
				else low_cover_live32_instances.push_back(instance);
			}
			else
			{
				push_instance(texture_info->first, instance);
			}
		}

		for (const auto& resource : resource_nodes_)
		{
			if (resource.kind != ResourceKind::DeadPlant) continue;
			if (!is_valid_global_sample(resource.coord)) continue;
			const auto& sample = global_field_[global_field_index(resource.coord)];
			if (!is_solid(sample)) continue;

			const vec2 world_position = resource.surface_attached ? resource.anchor_world : global_sample_world_position(resource.coord);
			const vec2 delta = subtract_vec2(world_position, view_center);
			if (delta.x * delta.x + delta.y * delta.y > visible_radius_sq) continue;

			const auto texture_info = vegetation_texture_info(&dead_plant_textures_[std::min<std::size_t>(resource.variant / 16u, dead_plant_textures_.size() - 1u)]);
			if (!texture_info.has_value()) continue;

			const vec2 up = resource.surface_up.lengthSquared() > 1e-6f ?
				normalize_vec2(scale_vec2(resource.surface_up, -1.0f)) :
				normalize_vec2(subtract_vec2(world_position, base_chunk_settings_.world_center));
			push_instance(texture_info->first, {
				.center_world = world_position,
				.up = up,
				.world_height = dead_plant_sprite_families[std::min<std::size_t>(resource.variant / 16u, dead_plant_sprite_families.size() - 1u)].world_height,
				.radial_offset = 0.0f,
				.texture_layer = texture_info->second,
				.tile_column = 7.0f,
				.tile_row = static_cast<float>(resource.variant % 16u),
				.angle_offset = 0.12f
			});
		}

		const gfx::ScopedAlphaBlendPass blend_pass{};
		static_cast<void>(blend_pass);

		auto draw_batch = [&](const VegetationBatchId batch_id, const std::vector<OreInstanceGpu>* override_instances = nullptr)
		{
			const auto batch_index = static_cast<std::size_t>(batch_id);
			const auto& resources_ptr = vegetation_render_resources_[batch_index];
			if (!resources_ptr) return;

			const auto& resources = *resources_ptr;
			const auto& instances = override_instances != nullptr ? *override_instances : instances_by_batch[batch_index];
			if (instances.empty()) return;

			glNamedBufferData(
				resources.instance_vbo,
				static_cast<GLsizeiptr>(instances.size() * sizeof(OreInstanceGpu)),
				instances.data(),
				GL_DYNAMIC_DRAW);

			if (const auto use_result = resources.shader.use(); !use_result)
			{
				Log::error(use_result.error());
				return;
			}

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, resources.texture_array);
			resources.shader.set_uniform("uProjection", gfx::make_projection(view));
			resources.shader.set_uniform("uOreTextureArray", 0);
			resources.shader.set_uniform("uTileSizePixels", static_cast<std::int32_t>(resources.tile_size_pixels));

			glBindVertexArray(resources.vao);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(instances.size()));
			glBindVertexArray(0);
		};

		draw_batch(VegetationBatchId::Live64);
		draw_batch(VegetationBatchId::Live32, &woody_live32_instances);
		draw_batch(VegetationBatchId::Live32, &low_cover_live32_instances);
		draw_batch(VegetationBatchId::Dead32);
		draw_batch(VegetationBatchId::Dead64);

		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glActiveTexture(GL_TEXTURE0);
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

		for (int y = 0; y < static_cast<int>(global_field_size_.y); ++y)
		{
			for (int x = 0; x < static_cast<int>(global_field_size_.x); ++x)
			{
				const ivec2 coord{ x, y };
				auto& sample = global_field_[global_field_index(coord)];
				sample.wetness = 0.0f;
				sample.padding = 0.0f;
				if (has_water(sample)) changed_coords.push_back(coord);
			}
		}

		smooth_cave_terrain(changed_coords);
		if (const auto finalize_result = finalize_generated_field(dirty_chunks, changed_coords); !finalize_result)
		{
			Log::error(finalize_result.error());
		}
	}

	void PlanetTerrain::smooth_cave_terrain(std::vector<ivec2>& changed_coords)
	{
		if (global_field_.empty()) return;

		for (int smooth_pass = 0; smooth_pass < 2; ++smooth_pass)
		{
			std::vector<float> smoothed_terrain(global_field_.size(), 0.0f);
			std::vector<float> smoothed_water(global_field_.size(), 0.0f);
			for (std::size_t i = 0; i < global_field_.size(); ++i)
			{
				smoothed_terrain[i] = global_field_[i].terrain;
				smoothed_water[i] = global_field_[i].water;
			}

			for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
			{
				for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
				{
					const ivec2 coord{ x, y };
					const vec2 world = global_sample_world_position(coord);
					const float depth = normalized_depth(world);
					if (depth < cave_generation_min_depth || depth > cave_generation_max_depth) continue;

					float terrain_total = 0.0f;
					float terrain_weight = 0.0f;
					float water_total = 0.0f;
					float water_weight = 0.0f;
					bool near_boundary = false;

					for (int oy = -1; oy <= 1; ++oy)
					{
						for (int ox = -1; ox <= 1; ++ox)
						{
							const ivec2 neighbor{ x + ox, y + oy };
							const auto& neighbor_sample = global_field_[global_field_index(neighbor)];
							const float weight = (ox == 0 && oy == 0) ? 2.0f : 1.0f;
							terrain_total += neighbor_sample.terrain * weight;
							terrain_weight += weight;

							if (neighbor_sample.water > 0.0f)
							{
								water_total += neighbor_sample.water * weight;
								water_weight += weight;
							}

							if ((neighbor_sample.terrain >= 0.0f) != (global_field_[global_field_index(coord)].terrain >= 0.0f)) near_boundary = true;
						}
					}

					if (!near_boundary) continue;

					const auto index = global_field_index(coord);
					smoothed_terrain[index] = std::lerp(global_field_[index].terrain, terrain_total / std::max(terrain_weight, 1e-4f), 0.42f);
					if (water_weight > 0.0f)
					{
						smoothed_water[index] = std::lerp(global_field_[index].water, water_total / water_weight, 0.55f);
					}
				}
			}

			for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
			{
				for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
				{
					const ivec2 coord{ x, y };
					const auto index = global_field_index(coord);
					const float next_terrain = smoothed_terrain[index];
					const float next_water = smoothed_water[index];
					if (std::abs(global_field_[index].terrain - next_terrain) <= 1e-5f &&
						std::abs(global_field_[index].water - next_water) <= 1e-5f) continue;

					global_field_[index].terrain = next_terrain;
					global_field_[index].water = next_water > 0.0f ? next_water : dry_water_density(global_field_[index]);
					changed_coords.push_back(coord);
				}
			}
		}
	}

	Result<void> PlanetTerrain::finalize_generated_field(std::vector<bool>& dirty_chunks, const std::vector<ivec2>& changed_coords)
	{
		recompute_wetness_around(changed_coords, dirty_chunks);
		generate_resource_nodes();
		return rebuild_dirty_chunks(dirty_chunks);
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
		if (ore_render_resources_) ore_render_resources_->instances_dirty = true;
		resource_nodes_.reserve(16000u);
		std::unordered_set<std::uint64_t> occupied_samples;

		auto choose_variant_row = [&](const ivec2 coord, const std::uint32_t seed_offset)
		{
			return static_cast<std::uint8_t>(std::clamp(
				static_cast<int>(hash01(static_cast<float>(coord.x), static_cast<float>(coord.y), base_chunk_settings_.seed + seed_offset) * 16.0f),
				0,
				15));
		};

		auto add_resource_node = [&](const ResourceKind kind, const ivec2 coord, const std::uint8_t variant, const bool cave_variant,
			const bool surface_attached = false, const vec2 anchor_world = { 0.0f, 0.0f }, const vec2 surface_up = { 0.0f, 0.0f })
		{
			resource_nodes_.push_back({
				.kind = kind,
				.coord = coord,
				.variant = variant,
				.cave_variant = cave_variant,
				.surface_attached = surface_attached,
				.anchor_world = anchor_world,
				.surface_up = surface_up
			});
			occupied_samples.insert(sample_key(coord));
		};

		auto has_occupied_neighbor = [&](const ivec2 coord, const int radius)
		{
			for (int oy = -radius; oy <= radius; ++oy)
			{
				for (int ox = -radius; ox <= radius; ++ox)
				{
					if (ox == 0 && oy == 0) continue;

					const ivec2 neighbor{ coord.x + ox, coord.y + oy };
					if (!is_valid_global_sample(neighbor)) continue;
					if (occupied_samples.contains(sample_key(neighbor))) return true;
				}
			}

			return false;
		};

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
				const bool cave = depth > cave_resource_min_depth && depth < cave_resource_max_depth;
				const auto attachment = exposed_surface_attachment(coord);
				if (!attachment.has_value()) continue;
				const float alignment = cave ? attachment->floor_alignment : 0.0f;
				const float roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 1701u);
				const float density = !cave ? 0.028f :
					(alignment > 0.95f ? 0.012f :
						alignment > 0.86f ? 0.032f :
						0.075f);
				if (roll > density) continue;

				ResourceKind kind = ResourceKind::Rock;
				const float ore_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 2309u);
				if (cave && alignment > 0.90f)
				{
					kind = ResourceKind::Rock;
				}
				else if (cave && depth > 0.44f && ore_roll > 0.84f) kind = ResourceKind::DiamondOre;
				else if (cave && depth > 0.34f && ore_roll > 0.62f) kind = ResourceKind::GoldOre;
				else if (cave && depth > 0.22f && ore_roll > 0.44f) kind = ResourceKind::BronzeOre;
				else if (cave && ore_roll > 0.20f) kind = ResourceKind::IronOre;

				add_resource_node(kind, coord, choose_variant_row(coord, 19u), cave, true, attachment->anchor_world, attachment->surface_up);
			}
		}

		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				if (occupied_samples.contains(sample_key(coord)) || has_occupied_neighbor(coord, 1)) continue;

				const auto& sample = global_field_[global_field_index(coord)];
				if (!is_solid(sample)) continue;

				const int neighbors = solid_neighbor_count(coord);
				if (neighbors < 8) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < cave_generation_min_depth || depth > constants::hard_rock_depth_threshold - 0.03f) continue;

				const float cluster_noise = perlin_fbm(add_vec2(scale_vec2(world, 0.076f), { 14.0f, -11.0f }), base_chunk_settings_.seed + 3209u);
				const float seam_noise = std::abs(perlin_noise(add_vec2(scale_vec2(world, 0.182f), { -7.0f, 19.0f }), base_chunk_settings_.seed + 4513u));
				const float depth_factor = std::clamp(
					(depth - cave_generation_min_depth) /
						std::max(constants::hard_rock_depth_threshold - cave_generation_min_depth - 0.03f, 0.01f),
					0.0f,
					1.0f);
				const float density = 0.0018f + std::max(cluster_noise, 0.0f) * 0.015f + depth_factor * 0.006f;
				const float placement_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 5003u);
				if (seam_noise > 0.46f || placement_roll > density) continue;

				const float ore_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 5407u);
				ResourceKind kind = ResourceKind::Rock;
				if (depth > 0.50f && ore_roll > 0.88f) kind = ResourceKind::DiamondOre;
				else if (depth > 0.38f && ore_roll > 0.68f) kind = ResourceKind::GoldOre;
				else if (depth > 0.22f && ore_roll > 0.42f) kind = ResourceKind::BronzeOre;
				else if (ore_roll > 0.14f) kind = ResourceKind::IronOre;
				else continue;

				add_resource_node(kind, coord, choose_variant_row(coord, 149u), true);
			}
		}

		static constexpr std::array<std::size_t, 5> dry_floor_dead_families{ 0u, 2u, 3u, 8u, 9u };
		static constexpr std::array<std::size_t, 5> damp_floor_dead_families{ 1u, 2u, 4u, 8u, 9u };
		static constexpr std::array<std::size_t, 5> shelf_dead_families{ 0u, 2u, 3u, 5u, 8u };
		static constexpr std::array<std::size_t, 5> wall_dead_families{ 5u, 6u, 7u, 6u, 5u };

		for (int y = 1; y < static_cast<int>(global_field_size_.y) - 1; ++y)
		{
			for (int x = 1; x < static_cast<int>(global_field_size_.x) - 1; ++x)
			{
				const ivec2 coord{ x, y };
				const auto& sample = global_field_[global_field_index(coord)];
				if (!is_exposed_to_air(sample, solid_neighbor_count(coord))) continue;

				const vec2 world = global_sample_world_position(coord);
				const float depth = normalized_depth(world);
				if (depth < cave_resource_min_depth || depth > cave_resource_max_depth) continue;
				if (has_water_neighbor(coord)) continue;
				const auto attachment = exposed_surface_attachment(coord);
				if (!attachment.has_value()) continue;
				const float alignment = attachment->floor_alignment;
				if (alignment < 0.72f) continue;
				const float roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 4073u);
				const float density =
					alignment > 0.96f ? (sample.wetness > 0.18f ? 0.98f : 0.92f) :
					alignment > 0.86f ? (sample.wetness > 0.18f ? 0.90f : 0.82f) :
					0.56f;
				if (roll > density) continue;

				if (occupied_samples.contains(sample_key(coord))) continue;

				const bool damp_family = sample.wetness > 0.18f;
				const float family_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 4483u);
				const auto& family_pool = alignment > 0.93f ?
					(damp_family ? damp_floor_dead_families : dry_floor_dead_families) :
					shelf_dead_families;
				const auto family_slot = std::min<std::size_t>(
					static_cast<std::size_t>(family_roll * static_cast<float>(family_pool.size())),
					family_pool.size() - 1u);
				std::uint8_t family_index = static_cast<std::uint8_t>(family_pool[family_slot]);

				const float large_prop_roll = hash01(static_cast<float>(x), static_cast<float>(y), base_chunk_settings_.seed + 4937u);
				if (alignment > 0.988f && large_prop_roll > (damp_family ? 0.86f : 0.68f)) family_index = 9u;
				else if (alignment > 0.94f && large_prop_roll > 0.42f) family_index = 8u;

				const std::uint8_t variant = static_cast<std::uint8_t>(family_index * 16u + choose_variant_row(coord, 4673u));

				add_resource_node(ResourceKind::DeadPlant, coord, variant, true, true, attachment->anchor_world, attachment->surface_up);
			}
		}
	}

	Result<void> PlanetTerrain::initialize_global_field()
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
			auto field = chunk.readback_field();
			if (!field)
			{
				return fail("Failed to read back field for chunk ({}, {}): {}",
					chunk.chunk_coord().x,
					chunk.chunk_coord().y,
					field.error().message);
			}
			if (field->empty()) continue;

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

					global_field_[global_field_index(global_coord)] = (*field)[static_cast<std::size_t>(y) * padded_size.x + x];
				}
			}
		}

		for (auto& sample : global_field_)
		{
			sample.wetness = 0.0f;
			sample.padding = 0.0f;
		}

		generate_caves_resources_and_plants();
		rebuild_water_blob_colliders();
		return {};
	}

	void PlanetTerrain::rebuild_water_blob_colliders()
	{
		water_blob_colliders_.clear();
		if (global_field_.empty() || global_field_size_.x < 2u || global_field_size_.y < 2u) return;
		const float min_cell_size = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
		const float min_segment_length = min_cell_size * 0.45f;
		const float collinear_epsilon = min_cell_size * 0.30f;
		const float min_loop_area = terrain_cell_size_.x * terrain_cell_size_.y * 0.5f;

		std::vector visited(global_field_.size(), false);
		for (std::uint32_t y = 0; y < global_field_size_.y; ++y)
		{
			for (std::uint32_t x = 0; x < global_field_size_.x; ++x)
			{
				const ivec2 start_coord{ static_cast<int>(x), static_cast<int>(y) };
				const auto start_index = global_field_index(start_coord);
				if (visited[start_index]) continue;
				visited[start_index] = true;
				if (!has_water(global_field_[start_index])) continue;

				auto component = collect_water_component(start_coord, false);
				if (component.empty()) continue;

				SampleBounds bounds{
					.min = component.front(),
					.max = component.front()
				};
				std::unordered_set<std::uint64_t> component_keys;
				component_keys.reserve(component.size());
				for (const auto coord : component)
				{
					visited[global_field_index(coord)] = true;
					component_keys.insert(sample_key(coord));
					bounds.min.x = std::min(bounds.min.x, coord.x);
					bounds.min.y = std::min(bounds.min.y, coord.y);
					bounds.max.x = std::max(bounds.max.x, coord.x);
					bounds.max.y = std::max(bounds.max.y, coord.y);
				}

				bounds = expand_sample_bounds(bounds, 1);
				const uvec2 local_size{
					static_cast<std::uint32_t>(bounds.max.x - bounds.min.x + 1),
					static_cast<std::uint32_t>(bounds.max.y - bounds.min.y + 1)
				};
				std::vector<float> local_values(static_cast<std::size_t>(local_size.x) * static_cast<std::size_t>(local_size.y), -1.0f);
				for (std::uint32_t local_y = 0; local_y < local_size.y; ++local_y)
				{
					for (std::uint32_t local_x = 0; local_x < local_size.x; ++local_x)
					{
						const ivec2 global_coord{
							bounds.min.x + static_cast<int>(local_x),
							bounds.min.y + static_cast<int>(local_y)
						};
						const auto index = static_cast<std::size_t>(local_y) * local_size.x + local_x;
						if (!component_keys.contains(sample_key(global_coord))) continue;
						local_values[index] = combined_water_field(global_field_[global_field_index(global_coord)]);
					}
				}

				auto loops = extract_contour_loops_from_scalar_field(
					local_values,
					local_size,
					{
						global_field_origin_.x + static_cast<float>(bounds.min.x) * terrain_cell_size_.x,
						global_field_origin_.y + static_cast<float>(bounds.min.y) * terrain_cell_size_.y
					},
					terrain_cell_size_);

				std::vector<std::vector<vec2>> simplified_loops;
				vec2 bounds_min{ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
				vec2 bounds_max{ -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
				for (auto loop : loops)
				{
					loop = TerrainContour::simplify_contour(loop, true, min_segment_length, collinear_epsilon);
					if (loop.size() < 3u) continue;

					float area = 0.0f;
					for (std::size_t i = 0; i < loop.size(); ++i)
					{
						const auto& a = loop[i];
						const auto& b = loop[(i + 1u) % loop.size()];
						area += a.x * b.y - b.x * a.y;
						bounds_min.x = std::min(bounds_min.x, a.x);
						bounds_min.y = std::min(bounds_min.y, a.y);
						bounds_max.x = std::max(bounds_max.x, a.x);
						bounds_max.y = std::max(bounds_max.y, a.y);
					}
					area *= 0.5f;
					if (area < 0.0f)
					{
						std::ranges::reverse(loop);
						area = -area;
					}
					if (area < min_loop_area) continue;
					simplified_loops.push_back(std::move(loop));
				}

				if (simplified_loops.empty()) continue;

				auto& blob = water_blob_colliders_.emplace_back(world_id_);
				blob.bounds_min = bounds_min;
				blob.bounds_max = bounds_max;
				blob.collider.build({}, {}, simplified_loops);
				blob.collider.set_water_enabled(true);
			}
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

	Result<void> PlanetTerrain::rebuild_dirty_chunks(const std::vector<bool>& dirty_chunks, const bool smooth_water)
	{
		const auto padded_size = padded_field_size(base_chunk_settings_);
		const auto stride = chunk_sample_stride(base_chunk_settings_);

		for (std::size_t i = 0; i < chunks_.size(); ++i)
		{
			if (!dirty_chunks[i]) continue;
			if (chunks_[i].has_pending_gpu_ground_brush())
			{
				auto synced_field = chunks_[i].finalize_gpu_ground_brush();
				if (!synced_field)
				{
					return fail("Failed to finalize GPU terrain edits for chunk ({}, {}): {}",
						chunks_[i].chunk_coord().x,
						chunks_[i].chunk_coord().y,
						synced_field.error().message);
				}

				const ivec2 chunk_base{
					chunks_[i].chunk_coord().x * stride.x,
					chunks_[i].chunk_coord().y * stride.y
				};
				for (std::uint32_t y = 0; y < padded_size.y; ++y)
				{
					for (std::uint32_t x = 0; x < padded_size.x; ++x)
					{
						const ivec2 global_coord{
							chunk_base.x + static_cast<int>(x),
							chunk_base.y + static_cast<int>(y)
						};
						if (!is_valid_global_sample(global_coord)) continue;
						global_field_[global_field_index(global_coord)] = (*synced_field)[static_cast<std::size_t>(y) * padded_size.x + x];
					}
				}
				continue;
			}

			auto rebuild_result = chunks_[i].rebuild_from_field(extract_chunk_field(chunks_[i].chunk_coord()), smooth_water);
			if (!rebuild_result)
			{
				return fail("Failed to rebuild chunk ({}, {}): {}",
					chunks_[i].chunk_coord().x,
					chunks_[i].chunk_coord().y,
					rebuild_result.error().message);
			}

			if (smooth_water)
			{
				auto synced_field = chunks_[i].readback_field();
				if (!synced_field)
				{
					return fail("Failed to read back smoothed water field for chunk ({}, {}): {}",
						chunks_[i].chunk_coord().x,
						chunks_[i].chunk_coord().y,
						synced_field.error().message);
				}

				const ivec2 chunk_base{
					chunks_[i].chunk_coord().x * stride.x,
					chunks_[i].chunk_coord().y * stride.y
				};
				for (std::uint32_t y = 0; y < padded_size.y; ++y)
				{
					for (std::uint32_t x = 0; x < padded_size.x; ++x)
					{
						const ivec2 global_coord{
							chunk_base.x + static_cast<int>(x),
							chunk_base.y + static_cast<int>(y)
						};
						if (!is_valid_global_sample(global_coord)) continue;
						global_field_[global_field_index(global_coord)] = (*synced_field)[static_cast<std::size_t>(y) * padded_size.x + x];
					}
				}
			}
		}

		rebuild_water_blob_colliders();
		return {};
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

		const vec2 sample_world = global_sample_world_position(coord);
		const vec2 up = normalize_vec2(
			subtract_vec2(sample_world, base_chunk_settings_.world_center),
			{ 0.0f, 1.0f });
		const vec2 tangent{ up.y, -up.x };
		const float cell_extent = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
		const float tangential_limit = cell_extent * 2.35f;
		const float outward_limit = cell_extent * 2.35f;
		const float inward_allowance = cell_extent * 0.60f;
		static constexpr int search_radius = 4;

		for (int y = -search_radius; y <= search_radius; ++y)
		{
			for (int x = -search_radius; x <= search_radius; ++x)
			{
				if (x == 0 && y == 0) continue;

				const ivec2 neighbor{ coord.x + x, coord.y + y };
				if (!is_valid_global_sample(neighbor)) continue;

				const auto& neighbor_sample = global_field_[global_field_index(neighbor)];
				if (!has_water(neighbor_sample)) continue;

				const vec2 delta = subtract_vec2(global_sample_world_position(neighbor), sample_world);
				const float tangent_offset = std::abs(delta.dot(tangent));
				const float up_offset = delta.dot(up);
				if (tangent_offset > tangential_limit) continue;
				if (up_offset < -inward_allowance || up_offset > outward_limit) continue;

				return true;
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

	std::vector<ivec2> PlanetTerrain::collect_water_component(const ivec2 start_coord, const bool include_diagonals) const
	{
		if (!is_valid_global_sample(start_coord)) return {};
		if (!has_water(global_field_[global_field_index(start_coord)])) return {};

		static constexpr std::array orthogonal_neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 }
		};
		static constexpr std::array diagonal_neighbors{
			ivec2{ 1, 1 },
			ivec2{ 1, -1 },
			ivec2{ -1, 1 },
			ivec2{ -1, -1 }
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

			for (const auto& offset : orthogonal_neighbors)
			{
				const ivec2 neighbor{ coord.x + offset.x, coord.y + offset.y };
				if (!is_valid_global_sample(neighbor)) continue;
				if (!has_water(global_field_[global_field_index(neighbor)])) continue;

				const auto key = sample_key(neighbor);
				if (!visited.insert(key).second) continue;

				frontier.push(neighbor);
			}

			if (!include_diagonals) continue;

			for (const auto& offset : diagonal_neighbors)
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

		auto component = collect_water_component(anchor, false);
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

		return build_water_plan(plan_start, desired_total, !pickup);
	}

	Result<void> PlanetTerrain::apply_water_plan_and_rebuild(const WaterPlan& plan)
	{
		std::vector<bool> dirty_chunks(chunks_.size(), false);
		std::vector<ivec2> changed_coords;
		const bool changed = apply_water_plan(plan, dirty_chunks, changed_coords);
		if (!changed) return fail("The requested water plan did not change any terrain samples");

		recompute_wetness_around(changed_coords, dirty_chunks);
		return rebuild_dirty_chunks(dirty_chunks, true);
	}

	PlanetTerrain::TerrainEditResult PlanetTerrain::apply_terrain_edit_to_global_field(const TerrainEdit& edit,
		std::vector<bool>& dirty_chunks,
		std::vector<ivec2>& changed_coords,
		const std::uint32_t unit_budget,
		const std::optional<GroundBrushBlocker>& blocker)
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
		const bool hard_dig = digging && edit.shape.y >= 0.5f;
		bool requires_wetness_rebuild = false;
		std::vector<TerrainEditCandidate> candidates;
		collect_terrain_edit_candidates(
			edit_center,
			radius,
			signed_strength,
			falloff_exponent,
			digging,
			hard_dig,
			candidates,
			requires_wetness_rebuild,
			blocker);

		if (candidates.empty()) return {};

		std::ranges::sort(candidates, [](const TerrainEditCandidate& lhs, const TerrainEditCandidate& rhs)
		{
			if (std::abs(lhs.falloff - rhs.falloff) > 1e-6f) return lhs.falloff > rhs.falloff;
			return lhs.distance_to_center < rhs.distance_to_center;
		});

		TerrainEditResult result{};
		result.requires_wetness_rebuild = requires_wetness_rebuild;
		result.candidates = static_cast<std::uint32_t>(std::min<std::size_t>(candidates.size(), std::numeric_limits<std::uint32_t>::max()));
		apply_terrain_edit_candidates(candidates, signed_strength, unit_budget, hard_dig, dirty_chunks, changed_coords, result);

		return result;
	}

	void PlanetTerrain::collect_terrain_edit_candidates(
		const vec2 edit_center,
		const float radius,
		const float signed_strength,
		const float falloff_exponent,
		const bool digging,
		const bool hard_dig,
		std::vector<TerrainEditCandidate>& candidates,
		bool& requires_wetness_rebuild,
		const std::optional<GroundBrushBlocker>& blocker) const
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
				if (blocker.has_value() && point_inside_brush_blocker(world, *blocker, { terrain_cell_size_.x * 0.35f, terrain_cell_size_.y * 0.35f }))
				{
					continue;
				}

				if (digging && is_dig_protected(coord)) continue;

				const float normalized = 1.0f - distance_to_center / radius;
				const float falloff = hard_dig ? 1.0f : std::pow(normalized, falloff_exponent);
				if (falloff <= 1e-6f) continue;

				const auto& sample = global_field_[global_field_index(coord)];
				const bool had_water = has_water(sample);
				const bool had_wetness = sample.wetness > 1e-4f;
				const bool had_water_adjacent = has_water_neighbor(coord);
				const float next_terrain = clamp_terrain_density(
					hard_dig ? std::min(sample.terrain, signed_strength) : sample.terrain + signed_strength * falloff,
					world,
					base_chunk_settings_);
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
		const bool hard_dig,
		std::vector<bool>& dirty_chunks,
		std::vector<ivec2>& changed_coords,
		TerrainEditResult& result)
	{
		std::unordered_set<std::uint64_t> cleared_keys;
		const auto apply_count = std::min<std::size_t>(candidates.size(), unit_budget);

		for (std::size_t i = 0; i < apply_count; ++i)
		{
			const auto coord = candidates[i].coord;
			const vec2 world = global_sample_world_position(coord);
			const auto sample_index = global_field_index(coord);
			auto& sample = global_field_[sample_index];
			const bool had_water = has_water(sample);
			const bool was_solid = is_solid(sample);
			const float next_terrain = clamp_terrain_density(
				hard_dig ? std::min(sample.terrain, signed_strength) : sample.terrain + signed_strength * candidates[i].falloff,
				world,
				base_chunk_settings_);
			bool local_changed = std::abs(next_terrain - sample.terrain) > 1e-6f;
			sample.terrain = next_terrain;

			if (sample.terrain >= 0.0f || !had_water)
			{
				const float next_water = dry_water_density(sample);
				local_changed = std::abs(next_water - sample.water) > 1e-6f || local_changed;
				sample.water = next_water;
			}

			if (!local_changed) continue;
			const bool is_solid_now = is_solid(sample);

			result.changed = true;
			if ((signed_strength < 0.0f && was_solid && !is_solid_now) ||
				(signed_strength > 0.0f && !was_solid && is_solid_now))
			{
				++result.units;
			}

			changed_coords.push_back(coord);
			mark_chunks_covering_global_sample(coord, dirty_chunks);

			if (was_solid && !is_solid_now && sample_index < plant_samples_.size())
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
		if (ore_render_resources_) ore_render_resources_->instances_dirty = true;
	}

	std::optional<PlanetTerrain::WaterPlan> PlanetTerrain::build_water_plan(const ivec2 start_coord,
		const std::uint32_t desired_wet_sample_count, const bool preserve_existing_water) const
	{
		if (!is_valid_global_sample(start_coord)) return std::nullopt;
		if (is_solid(global_field_[global_field_index(start_coord)])) return std::nullopt;

		static constexpr std::array neighbors{
			ivec2{ 1, 0 },
			ivec2{ -1, 0 },
			ivec2{ 0, 1 },
			ivec2{ 0, -1 },
			ivec2{ 1, 1 },
			ivec2{ 1, -1 },
			ivec2{ -1, 1 },
			ivec2{ -1, -1 }
		};
		const float cell_extent = std::min(terrain_cell_size_.x, terrain_cell_size_.y);
		const float smoothing_margin = cell_extent * 2.25f;

		WaterPlan plan{};
		plan.dried_component = collect_water_component(start_coord, false);
		const auto existing_water_count = static_cast<std::uint32_t>(plan.dried_component.size());

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

		if (preserve_existing_water && desired_wet_sample_count >= existing_water_count && !plan.dried_component.empty())
		{
			selected_samples.reserve(plan.dried_component.size());
			for (const auto coord : plan.dried_component)
			{
				visited.insert(sample_key(coord));
				const vec2 sample_world = global_sample_world_position(coord);
				selected_samples.push_back(WaterCandidate{
					.coord = coord,
					.radial = radial_distance(sample_world, base_chunk_settings_.world_center),
					.click_distance_sq = 0.0f
				});
			}

			for (const auto coord : plan.dried_component)
			{
				for (const auto& offset : neighbors)
				{
					push_candidate({ coord.x + offset.x, coord.y + offset.y });
				}
			}
		}
		else
		{
			push_candidate(start_coord);
		}

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
			const float water_depth = std::max(base_water_depth, contact_support);
			const float basin_support = std::clamp((static_cast<float>(neighbor_solids) - 4.0f) / 4.0f, 0.0f, 1.0f);
			if (!(preserve_existing_water && has_water(field_sample)))
			{
				if (water_depth <= 0.025f && basin_support < 0.85f) continue;
				if (water_depth <= 0.08f && basin_support < 0.55f) continue;
			}

			const float planned_water = std::max(water_depth * std::lerp(1.10f, 1.60f, basin_support), water_depth);

			plan.affected_samples.push_back({
				.coord = selected.coord,
				.water = preserve_existing_water ? std::max(planned_water, field_sample.water) : planned_water
			});
		}

		plan.wet_sample_count = static_cast<std::uint32_t>(selected_samples.size());

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
		recompute_ground_greenness(dirty_chunks);
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

				auto water_cells = collect_water_component(coord, false);
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

	void PlanetTerrain::recompute_ground_greenness(std::vector<bool>& dirty_chunks)
	{
		if (global_field_.empty() || plant_samples_.empty()) return;
		if (active_plants_dirty_) compact_active_plants();

		std::vector<float> next_greenness(global_field_.size(), 0.0f);
		for (const auto index : active_plant_indices_)
		{
			if (index >= plant_samples_.size()) continue;
			const auto& plant = plant_samples_[index];
			if (plant.stage == PlantStage::Empty || plant.stage == PlantStage::Seeded) continue;

			const ivec2 coord{
				static_cast<int>(index % global_field_size_.x),
				static_cast<int>(index / global_field_size_.x)
			};
			if (!is_valid_global_sample(coord)) continue;
			const auto& sample = global_field_[index];
			if (!is_solid(sample)) continue;

			const float wetness_factor = std::clamp((sample.wetness - 0.10f) / 0.30f, 0.0f, 1.0f);
			if (wetness_factor <= 1e-4f) continue;

			const float stage_factor = plant.stage == PlantStage::Mature ? 1.0f : 0.80f;
			const float family_strength = grass_influence_strength_for(plant.family);
			const float radius_world = grass_influence_radius_for(plant.family);
			const int radius_x = std::max(1, static_cast<int>(std::ceil(radius_world / std::max(terrain_cell_size_.x, 1e-4f))));
			const int radius_y = std::max(1, static_cast<int>(std::ceil(radius_world / std::max(terrain_cell_size_.y, 1e-4f))));
			const vec2 center = plant.anchor_world.lengthSquared() > 1e-6f ? plant.anchor_world : global_sample_world_position(coord);

			for (int y = std::max(0, coord.y - radius_y); y <= std::min(static_cast<int>(global_field_size_.y) - 1, coord.y + radius_y); ++y)
			{
				for (int x = std::max(0, coord.x - radius_x); x <= std::min(static_cast<int>(global_field_size_.x) - 1, coord.x + radius_x); ++x)
				{
					const ivec2 target_coord{ x, y };
					const auto target_index = global_field_index(target_coord);
					if (!is_solid(global_field_[target_index])) continue;

					const vec2 world = global_sample_world_position(target_coord);
					const vec2 delta = subtract_vec2(world, center);
					const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
					if (distance >= radius_world) continue;

					float falloff = 0.0f;
					if (distance <= radius_world * 0.72f) falloff = 1.0f;
					else
					{
						const float edge_t = 1.0f - (distance - radius_world * 0.72f) / std::max(radius_world * 0.28f, 1e-4f);
						const float clamped_t = std::clamp(edge_t, 0.0f, 1.0f);
						falloff = clamped_t * clamped_t * (3.0f - 2.0f * clamped_t);
					}
					const float influence = std::clamp(falloff * wetness_factor * stage_factor * family_strength, 0.0f, 1.0f);
					next_greenness[target_index] = std::max(next_greenness[target_index], influence);
				}
			}
		}

		for (int y = 0; y < static_cast<int>(global_field_size_.y); ++y)
		{
			for (int x = 0; x < static_cast<int>(global_field_size_.x); ++x)
			{
				const ivec2 coord{ x, y };
				auto& sample = global_field_[global_field_index(coord)];
				const float greenness = is_solid(sample) ? next_greenness[global_field_index(coord)] : 0.0f;
				if (std::abs(sample.padding - greenness) <= 1e-6f) continue;

				sample.padding = greenness;
				mark_chunks_covering_global_sample(coord, dirty_chunks);
			}
		}
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

	bool PlanetTerrain::contains_water_volume(const vec2 world_position) const
	{
		if (global_field_.empty()) return false;

		const float gx = (world_position.x - global_field_origin_.x) / terrain_cell_size_.x;
		const float gy = (world_position.y - global_field_origin_.y) / terrain_cell_size_.y;

		const float clamped_x = std::clamp(gx, 0.0f, static_cast<float>(global_field_size_.x - 1u));
		const float clamped_y = std::clamp(gy, 0.0f, static_cast<float>(global_field_size_.y - 1u));

		const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
		const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
		const auto x1 = std::min(x0 + 1u, global_field_size_.x - 1u);
		const auto y1 = std::min(y0 + 1u, global_field_size_.y - 1u);

		const float tx = clamped_x - static_cast<float>(x0);
		const float ty = clamped_y - static_cast<float>(y0);

		auto water_field_at = [this](const std::uint32_t x, const std::uint32_t y)
		{
			const auto& sample = global_field_[static_cast<std::size_t>(y) * global_field_size_.x + x];
			return std::min(-sample.terrain, sample.water);
		};

		const float value = std::lerp(
			std::lerp(water_field_at(x0, y0), water_field_at(x1, y0), tx),
			std::lerp(water_field_at(x0, y1), water_field_at(x1, y1), tx),
			ty);

		return value > 0.0f;
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
