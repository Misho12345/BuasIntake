#pragma once

#include "pch.hpp"


#include "vegetation/Plant.hpp"

namespace game::resources
{
	class ResourceSystem;
}

namespace game::terrain
{
	class PlanetTerrain;
}

namespace game::vegetation
{
	class VegetationSystem final
	{
	public:
		void         initialize(std::size_t sample_count);
		Result<void> plant_seed(
			const terrain::PlanetTerrain& terrain,
			resources::ResourceSystem&    resources,
			vec2                          world_position);

		bool update(float dt, const terrain::PlanetTerrain& terrain);
		void clear_plant_at(std::size_t sample_index);

		void refresh_surface_anchors(const terrain::PlanetTerrain&            terrain,
		                             const std::unordered_set<std::uint64_t>& affected_keys);
		void validate() const;

		std::span<const Plant> plant_samples() const noexcept { return plant_samples_; }
		std::span<const std::size_t> active_plant_indices() const;

		std::uint64_t revision() const noexcept { return revision_; }

	private:
		std::optional<ivec2>find_plantable_seed_coord(const terrain::PlanetTerrain& terrain, vec2 world_position) const;

		std::uint32_t nearby_cover_count(
			const terrain::PlanetTerrain& terrain,
			ivec2                         coord,
			float                         radius_samples,
			bool                          woody_cover) const;

		std::uint32_t mature_tree_count() const;
		bool can_place_woody_near(const terrain::PlanetTerrain& terrain, ivec2 coord, int min_spacing_samples) const;

		PlantFamily choose_plant_family(const terrain::PlanetTerrain& terrain, ivec2 coord) const;
		std::uint8_t choose_plant_variant(const terrain::PlanetTerrain& terrain, ivec2 coord, PlantFamily family) const;

		bool spread_plants(const terrain::PlanetTerrain& terrain);
		void compact_active_plants() const;

		std::vector<Plant>               plant_samples_{};
		mutable std::vector<std::size_t> active_plant_indices_{};
		mutable bool                     active_plants_dirty_{ false };
		std::uint64_t                    revision_{ 0u };
	};
}
