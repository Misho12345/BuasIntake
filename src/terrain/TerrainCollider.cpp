#include "pch.hpp"

#include "TerrainCollider.hpp"

namespace game::terrain
{
    namespace
    {
        constexpr float minimum_segment_length_sq = 1e-4f;

        bool is_valid_segment(const vec2& start, const vec2& end)
        {
            if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) || !std::isfinite(end.y))
            {
                return false;
            }

            const vec2 delta = end - start;
            return delta.lengthSquared() > minimum_segment_length_sq;
        }

        std::vector<b2Vec2> sanitize_points(const std::vector<vec2>& source, const bool loop)
        {
            std::vector<b2Vec2> points;
            points.reserve(source.size());

            for (const auto& point : source)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;

                if (!points.empty())
                {
                    const vec2 previous{ points.back().x, points.back().y };
                    if (!is_valid_segment(previous, point)) continue;
                }

                points.push_back(to_b2(point));
            }

            if (loop && points.size() >= 2u)
            {
                const vec2 first{ points.front().x, points.front().y };
                const vec2 last{ points.back().x, points.back().y };
                if (!is_valid_segment(last, first)) points.pop_back();
            }

            return points;
        }
    }

    TerrainCollider::TerrainCollider(const b2WorldId world_id) : world_id_{ world_id } {}

    TerrainCollider::~TerrainCollider()
    {
        if (b2Body_IsValid(terrain_body_)) b2DestroyBody(terrain_body_);
    }

    TerrainCollider::TerrainCollider(TerrainCollider&& other) noexcept
        : world_id_{ other.world_id_ },
          terrain_body_{ std::exchange(other.terrain_body_, b2_nullBodyId) }
    {
        other.world_id_ = b2_nullWorldId;
    }

    TerrainCollider& TerrainCollider::operator=(TerrainCollider&& other) noexcept
    {
        if (this == &other) return *this;

        if (b2Body_IsValid(terrain_body_)) b2DestroyBody(terrain_body_);

        world_id_       = other.world_id_;
        terrain_body_   = std::exchange(other.terrain_body_, b2_nullBodyId);
        other.world_id_ = b2_nullWorldId;
        return *this;
    }

    void TerrainCollider::build(
        const std::vector<std::vector<vec2>>& loops,
        const std::vector<std::vector<vec2>>& paths)
    {
        if (b2Body_IsValid(terrain_body_))
        {
            b2DestroyBody(terrain_body_);
            terrain_body_ = b2_nullBodyId;
        }

        if (loops.empty() && paths.empty()) return;

        std::size_t created_count = 0;
        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type      = b2_staticBody;
        body_def.name      = "terrain_chunk";

        terrain_body_ = b2CreateBody(world_id_, &body_def);
        if (!b2Body_IsValid(terrain_body_)) return;

        b2ShapeDef segment_shape_def           = b2DefaultShapeDef();
        segment_shape_def.material.friction    = 0.9f;
        segment_shape_def.material.restitution = 0.05f;

        for (const auto& loop : loops)
        {
            if (loop.size() < 4) continue;

            auto points = sanitize_points(loop, true);
            if (points.size() < 4u) continue;

            for (std::size_t i = 1; i < points.size(); ++i)
            {
                const b2Segment segment{ .point1 = points[i - 1], .point2 = points[i] };
                const b2ShapeId shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
                if (b2Shape_IsValid(shape)) ++created_count;
            }

            const b2Segment closing_segment{ .point1 = points.back(), .point2 = points.front() };
            const b2ShapeId closing_shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def,
                                                                 &closing_segment);
            if (b2Shape_IsValid(closing_shape)) ++created_count;
        }

        for (const auto& path : paths)
        {
            if (path.size() < 2) continue;

            auto points = sanitize_points(path, false);
            if (points.size() < 2u) continue;

            for (std::size_t i = 1; i < points.size(); ++i)
            {
                const b2Segment segment{ .point1 = points[i - 1], .point2 = points[i] };
                const b2ShapeId shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
                if (b2Shape_IsValid(shape)) ++created_count;
            }
        }

        if (created_count == 0 && b2Body_IsValid(terrain_body_))
        {
            b2DestroyBody(terrain_body_);
            terrain_body_ = b2_nullBodyId;
        }
    }
}
