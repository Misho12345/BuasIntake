#include "pch.hpp"
#include "TerrainCollider.hpp"

namespace game::terrain
{
	namespace
	{
		int terrain_collider_tag = 0;
		int water_collider_tag = 0;
	}

	void* collider_user_data(const ColliderKind kind)
	{
		return kind == ColliderKind::Water ? static_cast<void*>(&water_collider_tag) : static_cast<void*>(&terrain_collider_tag);
	}

	bool is_water_collider_user_data(const void* user_data)
	{
		return user_data == static_cast<const void*>(&water_collider_tag);
	}

	TerrainCollider::TerrainCollider(const b2WorldId world_id, const ColliderKind kind, const bool sensor) :
		world_id_{ world_id },
		kind_{ kind },
		sensor_{ sensor } {}

	TerrainCollider::~TerrainCollider()
	{
		if (b2Body_IsValid(terrain_body_)) b2DestroyBody(terrain_body_);
	}

	TerrainCollider::TerrainCollider(TerrainCollider&& other) noexcept :
		world_id_{ other.world_id_ },
		terrain_body_{ std::exchange(other.terrain_body_, b2_nullBodyId) },
		kind_{ other.kind_ },
		sensor_{ other.sensor_ }
	{
		other.world_id_ = b2_nullWorldId;
	}

	TerrainCollider& TerrainCollider::operator=(TerrainCollider&& other) noexcept
	{
		if (this == &other) return *this;

		if (b2Body_IsValid(terrain_body_)) b2DestroyBody(terrain_body_);

		world_id_ = other.world_id_;
		terrain_body_ = std::exchange(other.terrain_body_, b2_nullBodyId);
		kind_ = other.kind_;
		sensor_ = other.sensor_;
		other.world_id_ = b2_nullWorldId;
		return *this;
	}

	void TerrainCollider::build(const std::vector<std::vector<vec2>>& loops, const std::vector<std::vector<vec2>>& paths)
	{
		if (b2Body_IsValid(terrain_body_))
		{
			b2DestroyBody(terrain_body_);
			terrain_body_ = b2_nullBodyId;
		}

		if (loops.empty() && paths.empty()) return;

		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;
		body_def.name = kind_ == ColliderKind::Water ? "water_chunk" : "terrain_chunk";
		body_def.userData = collider_user_data(kind_);

		terrain_body_ = b2CreateBody(world_id_, &body_def);

		b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
		material.friction = 0.9f;
		material.restitution = 0.05f;

		b2ShapeDef segment_shape_def = b2DefaultShapeDef();
		segment_shape_def.material.friction = material.friction;
		segment_shape_def.material.restitution = material.restitution;
		segment_shape_def.userData = collider_user_data(kind_);
		segment_shape_def.isSensor = sensor_;
		segment_shape_def.enableSensorEvents = sensor_;

		std::size_t created_count = 0;

		for (const auto& loop : loops)
		{
			if (loop.size() < 4) continue;

			if (sensor_)
			{
				for (std::size_t i = 0; i < loop.size(); ++i)
				{
					const auto& start = loop[i];
					const auto& end = loop[(i + 1) % loop.size()];
					const b2Segment segment{ to_b2(start), to_b2(end) };
					b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
					++created_count;
				}

				continue;
			}

			std::vector<b2Vec2> points;
			points.reserve(loop.size());
			for (const auto& point : loop)
			{
				points.push_back(to_b2(point));
			}

			b2ChainDef chain_def = b2DefaultChainDef();
			chain_def.userData = collider_user_data(kind_);
			chain_def.points = points.data();
			chain_def.count = static_cast<int>(points.size());
			chain_def.materials = &material;
			chain_def.materialCount = 1;
			chain_def.isLoop = true;

			b2CreateChain(terrain_body_, &chain_def);
			++created_count;
		}

		for (const auto& path : paths)
		{
			if (path.size() < 2) continue;

			for (std::size_t i = 1; i < path.size(); ++i)
			{
				const b2Segment segment{ to_b2(path[i - 1]), to_b2(path[i]) };
				b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
				++created_count;
			}
		}

		if (created_count == 0)
		{
			b2DestroyBody(terrain_body_);
			terrain_body_ = b2_nullBodyId;
		}
	}

	void TerrainCollider::set_enabled(const bool enabled) const
	{
		if (!b2Body_IsValid(terrain_body_)) return;

		const auto currently_enabled = b2Body_IsEnabled(terrain_body_);
		if (enabled && !currently_enabled) b2Body_Enable(terrain_body_);
		if (!enabled && currently_enabled) b2Body_Disable(terrain_body_);
	}

	b2BodyId TerrainCollider::body() const { return terrain_body_; }
	bool TerrainCollider::has_body() const { return b2Body_IsValid(terrain_body_); }
	bool TerrainCollider::is_enabled() const { return b2Body_IsValid(terrain_body_) && b2Body_IsEnabled(terrain_body_); }
}
