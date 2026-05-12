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
		// initialize sizes the plant grid to the terrain field so every sample can own one plant slot if needed
		void         initialize(std::size_t sample_count);
		Result<void> plant_seed(
			const terrain::PlanetTerrain& terrain,
			resources::ResourceSystem&    resources,
			vec2                          world_position);

		// update advances growth and also runs plant spreading when mature plants are ready to try it again
		bool update(float dt, const terrain::PlanetTerrain& terrain);
		void clear_plant_at(std::size_t sample_index);

		// terrain edits call this so plants can either move to a refreshed surface anchor or die if their support is gone
		void refresh_surface_anchors(const terrain::PlanetTerrain&            terrain,
		                             const std::unordered_set<std::uint64_t>& affected_keys);
		void validate() const;

		std::span<const Plant> plant_samples() const { return plant_samples_; }
		std::span<const std::size_t> active_plant_indices() const;

		std::uint64_t revision() const { return revision_; }

	private:
		// this is the local planting search around the click and it folds in wetness spacing cover and resource checks
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

		// mature plants call into this to try a controlled nearby spread instead of instantly turning the whole map green
		bool spread_plants(const terrain::PlanetTerrain& terrain);
		void compact_active_plants() const;

		std::vector<Plant>               plant_samples_{};
		mutable std::vector<std::size_t> active_plant_indices_{};
		mutable bool                     active_plants_dirty_{ false };
		std::uint64_t                    revision_{ 0u };
	};
}
