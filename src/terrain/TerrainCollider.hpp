#pragma once

#include "pch.hpp"

namespace game::terrain
{
	enum class ColliderKind
	{
		Terrain,
		Water
	};

	[[nodiscard]] void* collider_user_data(ColliderKind kind);
	[[nodiscard]] bool is_water_collider_user_data(const void* user_data);

	class TerrainCollider final
	{
	public:
		explicit TerrainCollider(b2WorldId world_id, ColliderKind kind = ColliderKind::Terrain, bool sensor = false);
		~TerrainCollider();

		TerrainCollider(const TerrainCollider&) = delete;
		TerrainCollider& operator=(const TerrainCollider&) = delete;
		TerrainCollider(TerrainCollider&& other) noexcept;
		TerrainCollider& operator=(TerrainCollider&& other) noexcept;

		void build(const std::vector<std::vector<vec2>>& loops, const std::vector<std::vector<vec2>>& paths);
		void set_enabled(bool enabled) const;

		[[nodiscard]] b2BodyId body() const;
		[[nodiscard]] bool has_body() const;
		[[nodiscard]] bool is_enabled() const;

	private:
		b2WorldId world_id_{ b2_nullWorldId };
		b2BodyId terrain_body_{ b2_nullBodyId };
		ColliderKind kind_{ ColliderKind::Terrain };
		bool sensor_{ false };
	};
}
