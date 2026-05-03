#pragma once

#include "pch.hpp"

namespace game::terrain
{
	class TerrainCollider final
	{
	public:
		explicit TerrainCollider(b2WorldId world_id);
		~TerrainCollider();

		TerrainCollider(const TerrainCollider&) = delete;
		TerrainCollider& operator=(const TerrainCollider&) = delete;
		TerrainCollider(TerrainCollider&& other) noexcept;
		TerrainCollider& operator=(TerrainCollider&& other) noexcept;

		void build(const std::vector<std::vector<vec2>>& loops, const std::vector<std::vector<vec2>>& paths,
			const std::vector<std::vector<vec2>>& water_loops = {});
		void set_water_enabled(bool enabled) const;

	private:
		b2WorldId world_id_{ b2_nullWorldId };
		b2BodyId terrain_body_{ b2_nullBodyId };
		b2BodyId water_sensor_body_{ b2_nullBodyId };
	};
}
