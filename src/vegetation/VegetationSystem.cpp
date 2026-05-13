#include "pch.hpp"

#include "vegetation/VegetationSystem.hpp"

#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"

namespace game::vegetation
{
    namespace
    {
        float hash01(const float x, const float y, const std::uint32_t seed)
        {
            const float value = std::sin(x * 12.9898f + y * 78.233f + static_cast<float>(seed) * 0.013f) * 43758.5453f;
            return value - std::floor(value);
        }

        bool is_solid(const terrain::PlanetTerrain::FieldSample& sample) { return sample.terrain >= 0.0f; }
        bool has_water(const terrain::PlanetTerrain::FieldSample& sample) { return water::has_water(sample); }

        bool is_low_cover_family(const PlantFamily family)
        {
            return family == PlantFamily::Grass || family == PlantFamily::Flowers;
        }

        bool is_woody_family(const PlantFamily family)
        {
            return family == PlantFamily::Bush || family == PlantFamily::Tree;
        }

        struct PlantCandidate final
        {
            ivec2 coord{ 0, 0 };
            float anchor_dist_sq{ 0.0f };
            float radial{ 0.0f };
        };
    }

    void VegetationSystem::initialize(const std::size_t sample_count)
    {
        plant_samples_.assign(sample_count, {});
        active_plant_indices_.clear();
        active_plants_dirty_ = false;
        ++revision_;
    }

    // planting is more than just dropping a flag in a cell
    // we resolve the actual best nearby sample then lock in family variant and anchor right away so later growth is deterministic
    Result<void> VegetationSystem::plant_seed(
        const terrain::PlanetTerrain& terrain,
        resources::ResourceSystem&    resources,
        const vec2                    world_position)
    {
        if (resources.inventory().count(resources::InventoryItem::Seeds) == 0u)
        {
            return fail("Cannot plant seed: inventory is empty");
        }

        if (plant_samples_.empty()) return fail("Cannot plant seed: terrain field is not initialized");

        const auto seed_coord = find_plantable_seed_coord(terrain, world_position);
        if (!seed_coord.has_value()) return fail("No valid planting spot is within reach");

        const auto coord      = *seed_coord;
        const auto field_size = terrain.global_field_size();

        const auto index =
                static_cast<std::size_t>(coord.y) *
                static_cast<std::size_t>(field_size.x) +
                static_cast<std::size_t>(coord.x);

        const auto family  = choose_plant_family(terrain, coord);
        const auto variant = choose_plant_variant(terrain, coord, family);
        const auto anchor  = terrain.surface_anchor_world(coord);

        if (!anchor.has_value()) return fail("Failed to resolve a surface anchor for the planted seed");

        plant_samples_[index] = {
            .stage        = PlantStage::Seeded,
            .family       = family,
            .age          = 0.0f,
            .spread_age   = 0.0f,
            .variant      = variant,
            .anchor_world = *anchor
        };

        active_plant_indices_.push_back(index);
        ++revision_;
        static_cast<void>(resources.spend(resources::ResourceInventory{ .seeds = 1u }));
        return {};
    }

    // this advances growth stages first then runs spreading after that
    // keeping spread separate from growth makes the thresholds easier to reason about and stops one update from doing too many things at once
    bool VegetationSystem::update(const float dt, const terrain::PlanetTerrain& terrain)
    {
        if (plant_samples_.empty() || dt <= 0.0f) return false;
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
                plant.stage    = PlantStage::Sprout;
                plants_changed = true;
            }
            else if (plant.stage == PlantStage::Sprout && plant.age >= seed_to_sprout_time + sprout_growth_duration)
            {
                plant.stage    = PlantStage::Mature;
                plants_changed = true;
            }
        }

        const bool spread_changed = spread_plants(terrain);
        if (plants_changed || spread_changed) ++revision_;
        return plants_changed || spread_changed;
    }

    void VegetationSystem::clear_plant_at(const std::size_t sample_index)
    {
        if (sample_index >= plant_samples_.size()) return;
        if (plant_samples_[sample_index].stage == PlantStage::Empty) return;

        plant_samples_[sample_index] = {};
        active_plants_dirty_         = true;
        ++revision_;
    }

    void VegetationSystem::refresh_surface_anchors(
        const terrain::PlanetTerrain&            terrain,
        const std::unordered_set<std::uint64_t>& affected_keys)
    {
        if (affected_keys.empty() || plant_samples_.empty()) return;

        const auto field_size = terrain.global_field_size();
        bool       changed    = false;
        for (const auto index : active_plant_indices())
        {
            if (index >= plant_samples_.size()) continue;

            auto& plant = plant_samples_[index];
            if (plant.stage == PlantStage::Empty) continue;

            const ivec2 coord{
                static_cast<int>(index % field_size.x),
                static_cast<int>(index / field_size.x)
            };

            if (!affected_keys.contains(sample_key(coord))) continue;

            const auto anchor = terrain.surface_anchor_world(coord);
            if (!anchor.has_value())
            {
                plant                = {};
                active_plants_dirty_ = true;
                changed              = true;
                continue;
            }

            if ((plant.anchor_world - *anchor).lengthSquared() <= 1e-6f) continue;
            plant.anchor_world = *anchor;
            changed            = true;
        }

        if (changed) ++revision_;
    }

    void VegetationSystem::validate() const
    {
        #ifndef NDEBUG
        for (const auto index : active_plant_indices())
        {
            if (index >= plant_samples_.size())
            {
                Log::error("VegetationSystem validation failed: active plant index {} is out of range", index);
                continue;
            }

            if (plant_samples_[index].stage == PlantStage::Empty)
            {
                Log::error("VegetationSystem validation failed: empty plant stored in active index {}", index);
            }
        }
        #endif
    }

    std::span<const std::size_t> VegetationSystem::active_plant_indices() const
    {
        if (active_plants_dirty_) compact_active_plants();
        return active_plant_indices_;
    }

    // this is a local best candidate search around the click
    // there are a lot of filters because planting everywhere looks fake fast and it also breaks progression if it ignores wetness resources and spacing
    std::optional<ivec2> VegetationSystem::find_plantable_seed_coord(
        const terrain::PlanetTerrain& terrain,
        const vec2                    world_position) const
    {
        if (plant_samples_.empty()) return std::nullopt;

        const ivec2 center    = terrain.world_to_global_sample(world_position);
        const auto  cell_size = terrain.terrain_cell_size();

        const float min_cell_size = std::max(std::min(cell_size.x, cell_size.y), 0.01f);
        const int   search_radius = std::clamp(static_cast<int>(std::ceil(1.25f / min_cell_size)), 3, 8);

        std::optional<PlantCandidate> best_candidate;
        for (int y = center.y - search_radius; y <= center.y + search_radius; ++y)
        {
            for (int x = center.x - search_radius; x <= center.x + search_radius; ++x)
            {
                const ivec2 coord{ x, y };
                if (!terrain.is_valid_global_sample(coord)) continue;

                const auto field_size = terrain.global_field_size();

                const auto index =
                        static_cast<std::size_t>(coord.y) *
                        static_cast<std::size_t>(field_size.x) +
                        static_cast<std::size_t>(coord.x);

                const auto& sample = terrain.global_sample(coord);

                if (!is_solid(sample) ||
                    has_water(sample) ||
                    sample.wetness < seed_plantable_wetness_threshold)
                    continue;

                if (plant_samples_[index].stage != PlantStage::Empty) continue;
                if (terrain.has_resource_at(coord)) continue;
                if (!terrain.is_surface_suitable_for_plant(coord)) continue;

                const auto family = choose_plant_family(terrain, coord);

                if (is_low_cover_family(family))
                {
                    if (nearby_cover_count(terrain, coord, 4.8f, false) >= 4u) continue;
                }
                else
                {
                    const int spacing = family == PlantFamily::Tree ? 24 : 12;

                    if (!can_place_woody_near(terrain, coord, spacing)) continue;

                    if (nearby_cover_count(
                        terrain,
                        coord,
                        family == PlantFamily::Tree ? 16.0f : 9.0f,
                        true) >= (family == PlantFamily::Tree ? 1u : 3u))
                        continue;
                }

                const auto anchor = terrain.surface_anchor_world(coord);
                if (!anchor.has_value()) continue;

                const vec2 anchor_delta = *anchor - world_position;

                const PlantCandidate candidate{
                    .coord          = coord,
                    .anchor_dist_sq = anchor_delta.x * anchor_delta.x + anchor_delta.y * anchor_delta.y,
                    .radial         = distance_between(*anchor, terrain.planet_center())
                };

                if (!best_candidate.has_value() ||
                    candidate.anchor_dist_sq < best_candidate->anchor_dist_sq ||
                    (std::abs(candidate.anchor_dist_sq - best_candidate->anchor_dist_sq) <= 1e-5f &&
                        candidate.radial > best_candidate->radial))
                {
                    best_candidate = candidate;
                }
            }
        }

        if (!best_candidate.has_value()) return std::nullopt;
        return best_candidate->coord;
    }

    std::uint32_t VegetationSystem::nearby_cover_count(
        const terrain::PlanetTerrain& terrain,
        const ivec2                   coord,
        const float                   radius_samples,
        const bool                    woody_cover) const
    {
        const float   radius_sq  = radius_samples * radius_samples;
        std::uint32_t count      = 0u;
        const auto    field_size = terrain.global_field_size();

        for (const auto index : active_plant_indices())
        {
            if (index >= plant_samples_.size()) continue;

            const auto& plant = plant_samples_[index];

            if (plant.stage == PlantStage::Empty) continue;
            if (woody_cover ? !is_woody_family(plant.family) : !is_low_cover_family(plant.family)) continue;

            const ivec2 other_coord{
                static_cast<int>(index % field_size.x),
                static_cast<int>(index / field_size.x)
            };

            const float dx = static_cast<float>(other_coord.x - coord.x);
            const float dy = static_cast<float>(other_coord.y - coord.y);

            if (dx * dx + dy * dy <= radius_sq) ++count;
        }

        return count;
    }

    std::uint32_t VegetationSystem::mature_tree_count() const
    {
        std::uint32_t count = 0u;

        for (const auto index : active_plant_indices())
        {
            if (index >= plant_samples_.size()) continue;

            const auto& plant = plant_samples_[index];

            if (plant.stage == PlantStage::Mature &&
                plant.family == PlantFamily::Tree)
                ++count;
        }

        return count;
    }

    bool VegetationSystem::can_place_woody_near(
        const terrain::PlanetTerrain& terrain,
        const ivec2                   coord,
        const int                     min_spacing_samples) const
    {
        const auto field_size = terrain.global_field_size();

        for (const auto index : active_plant_indices())
        {
            if (index >= plant_samples_.size()) continue;

            const auto& plant = plant_samples_[index];

            if (plant.stage == PlantStage::Empty) continue;
            if (!is_woody_family(plant.family)) continue;

            const ivec2 other_coord{
                static_cast<int>(index % field_size.x),
                static_cast<int>(index / field_size.x)
            };

            const int   dx = other_coord.x - coord.x;
            const int   dy = other_coord.y - coord.y;

            if (dx * dx + dy * dy < min_spacing_samples * min_spacing_samples) return false;
        }

        return true;
    }

    // this is intentionally simple and deterministic
    // the hashes give variety but the tree ratio and spacing checks stop the map from degenerating into all woody plants
    PlantFamily VegetationSystem::choose_plant_family(const terrain::PlanetTerrain& terrain, const ivec2 coord) const
    {
        const float roll = hash01(
            static_cast<float>(coord.x),
            static_cast<float>(coord.y),
            terrain.seed() + 6001u);

        const std::uint32_t active_count = static_cast<std::uint32_t>(
            std::max<std::size_t>(active_plant_indices().size(), 1u));

        const float tree_ratio = static_cast<float>(mature_tree_count()) / static_cast<float>(active_count);

        if (roll > 0.86f && tree_ratio < 0.36f && can_place_woody_near(terrain, coord, 12)) return PlantFamily::Tree;
        if (roll > 0.76f) return PlantFamily::Bush;
        if (roll > 0.75f) return PlantFamily::Flowers;

        return PlantFamily::Grass;
    }

    std::uint8_t VegetationSystem::choose_plant_variant(
        const terrain::PlanetTerrain& terrain,
        const ivec2                   coord,
        const PlantFamily             family) const
    {
        const float random_value = hash01(
            static_cast<float>(coord.x),
            static_cast<float>(coord.y),
            terrain.seed() + 911u);

        const std::uint8_t base_variant = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(random_value * 16.0f), 0, 15));

        if (family != PlantFamily::Tree) return base_variant;

        static constexpr std::array<std::uint8_t, 4> tree_variants{ 1u, 2u, 4u, 7u };
        return tree_variants[base_variant % tree_variants.size()];
    }

    // mature plants try a handful of nearby offsets instead of scanning the whole world because we only need believable spread not a botany sim
    // new plants are staged first and committed after the loop so one plant spreading does not immediately affect another one in the same pass
    bool VegetationSystem::spread_plants(const terrain::PlanetTerrain& terrain)
    {
        if (active_plant_indices().empty()) return false;

        struct NewPlant final
        {
            std::size_t index{ 0u };
            Plant       plant{};
        };

        std::vector<NewPlant> spawned_plants;
        spawned_plants.reserve(16u);
        const auto field_size = terrain.global_field_size();

        for (const auto index : active_plant_indices_)
        {
            if (index >= plant_samples_.size()) continue;

            auto& plant = plant_samples_[index];

            const float spread_interval =
                plant.family == PlantFamily::Tree
                    ? 225.0f
                    : plant.family == PlantFamily::Bush
                          ? 175.0f
                          : plant.family == PlantFamily::Flowers
                                ? 150.0f
                                : 130.0f;

            if (plant.stage != PlantStage::Mature || plant.spread_age < spread_interval) continue;
            plant.spread_age = 0.0f;

            const ivec2 origin{
                static_cast<int>(index % field_size.x),
                static_cast<int>(index / field_size.x)
            };

            int radius = 5;

            if (plant.family == PlantFamily::Tree) radius = 15;
            else if (plant.family == PlantFamily::Bush) radius = 8;

            const int start_offset = static_cast<int>(
                hash01(static_cast<float>(origin.x), static_cast<float>(origin.y),
                       terrain.seed() + 7103u) * 32.0f);

            static constexpr std::array offsets{
                ivec2{ 1, 0 }, ivec2{ 2, 0 },
                ivec2{ 3, 1 }, ivec2{ 2, 2 },
                ivec2{ 1, 3 }, ivec2{ 0, 2 },
                ivec2{ -1, 3 }, ivec2{ -2, 2 },
                ivec2{ -3, 1 }, ivec2{ -2, 0 },
                ivec2{ -3, -1 }, ivec2{ -2, -2 },
                ivec2{ -1, -3 }, ivec2{ 0, -2 },
                ivec2{ 1, -3 }, ivec2{ 2, -2 },
                ivec2{ 3, -1 }, ivec2{ 4, 1 },
                ivec2{ 4, -1 }, ivec2{ -4, 1 },
                ivec2{ -4, -1 }, ivec2{ 1, 4 },
                ivec2{ -1, 4 }, ivec2{ 1, -4 },
                ivec2{ -1, -4 }, ivec2{ 5, 0 },
                ivec2{ -5, 0 }, ivec2{ 0, 5 },
                ivec2{ 0, -5 }, ivec2{ 6, 2 },
                ivec2{ -6, 2 }, ivec2{ 2, -6 }
            };

            for (std::size_t attempt = 0; attempt < offsets.size(); ++attempt)
            {
                const auto offset = offsets[(attempt + start_offset) % offsets.size()];
                if (offset.x * offset.x + offset.y * offset.y > radius * radius) continue;

                const ivec2 coord{ origin.x + offset.x, origin.y + offset.y };

                if (!terrain.is_valid_global_sample(coord)) continue;
                if (!terrain.is_surface_suitable_for_plant(coord)) continue;

                const auto target_index =
                        static_cast<std::size_t>(coord.y) *
                        static_cast<std::size_t>(field_size.x) +
                        static_cast<std::size_t>(coord.x);

                const auto& sample = terrain.global_sample(coord);

                if (!is_solid(sample) ||
                    has_water(sample) || sample.wetness < seed_plantable_wetness_threshold)
                    continue;

                if (plant_samples_[target_index].stage != PlantStage::Empty) continue;
                if (terrain.has_resource_at(coord)) continue;

                const auto family = choose_plant_family(terrain, coord);
                if (is_low_cover_family(family))
                {
                    if (nearby_cover_count(terrain, coord, 4.8f, false) >= 4u) continue;
                }
                else
                {
                    const int spacing = family == PlantFamily::Tree ? 12 : 10;
                    if (!can_place_woody_near(terrain, coord, spacing)) continue;

                    if (nearby_cover_count(
                        terrain,
                        coord,
                        family == PlantFamily::Tree ? 11.0f : 8.5f,
                        true) >= 4u)
                        continue;
                }

                const auto variant = choose_plant_variant(terrain, coord, family);
                const auto anchor  = terrain.surface_anchor_world(coord);
                if (!anchor.has_value()) continue;

                spawned_plants.push_back({
                    .index = target_index,
                    .plant = {
                        .stage        = PlantStage::Seeded,
                        .family       = family,
                        .age          = 0.0f,
                        .spread_age   = 0.0f,
                        .variant      = variant,
                        .anchor_world = *anchor
                    }
                });
                break;
            }
        }

        bool committed = false;

        for (const auto& [index, plant] : spawned_plants)
        {
            if (index >= plant_samples_.size()) continue;

            if (plant_samples_[index].stage != PlantStage::Empty) continue;
            plant_samples_[index] = plant;
            active_plant_indices_.push_back(index);

            committed = true;
        }

        return committed;
    }

    void VegetationSystem::compact_active_plants() const
    {
        std::erase_if(
            active_plant_indices_,
            [this](const std::size_t index)
            {
                return index >= plant_samples_.size() ||
                        plant_samples_[index].stage == PlantStage::Empty;
            });

        active_plants_dirty_ = false;
    }
}
