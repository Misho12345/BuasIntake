#include "pch.hpp"

#include "TerrainCollider.hpp"

#include "core/ScopedProfiler.hpp"

namespace game::terrain
{
    namespace
    {
        constexpr float minimum_segment_length_sq = 1e-4f;
        constexpr float minimum_polygon_area = 1e-3f;

        float signed_area(const std::span<const vec2> points)
        {
            if (points.size() < 3u)
                return 0.0f;

            float area = 0.0f;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto& a = points[i];
                const auto& b = points[(i + 1u) % points.size()];
                area += a.x * b.y - b.x * a.y;
            }

            return area * 0.5f;
        }

        float cross(const vec2& a, const vec2& b, const vec2& c)
        {
            const vec2 ab{b.x - a.x, b.y - a.y};
            const vec2 ac{c.x - a.x, c.y - a.y};
            return ab.x * ac.y - ab.y * ac.x;
        }

        bool point_in_triangle(const vec2& point, const vec2& a, const vec2& b, const vec2& c)
        {
            const float ab = cross(a, b, point);
            const float bc = cross(b, c, point);
            const float ca = cross(c, a, point);
            return ab >= -1e-4f && bc >= -1e-4f && ca >= -1e-4f;
        }

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
                if (!std::isfinite(point.x) || !std::isfinite(point.y))
                    continue;

                if (!points.empty())
                {
                    const vec2 previous{points.back().x, points.back().y};
                    if (!is_valid_segment(previous, point))
                        continue;
                }

                points.push_back(to_b2(point));
            }

            if (loop && points.size() >= 2u)
            {
                const vec2 first{points.front().x, points.front().y};
                const vec2 last{points.back().x, points.back().y};
                if (!is_valid_segment(last, first))
                    points.pop_back();
            }

            return points;
        }

        bool is_convex_polygon(const std::span<const vec2> points)
        {
            if (points.size() < 3u)
                return false;

            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto& a = points[(i + points.size() - 1u) % points.size()];
                const auto& b = points[i];
                const auto& c = points[(i + 1u) % points.size()];
                if (cross(a, b, c) < -1e-4f)
                    return false;
            }

            return true;
        }

        bool create_sensor_polygon(b2BodyId body, const b2ShapeDef& shape_def, const std::span<const vec2> source)
        {
            if (source.size() < 3u || source.size() > B2_MAX_POLYGON_VERTICES)
                return false;
            if (std::abs(signed_area(source)) <= minimum_polygon_area)
                return false;

            std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> points{};
            for (std::size_t i = 0; i < source.size(); ++i)
            {
                points[i] = to_b2(source[i]);
            }

            const b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(source.size()));
            if (!b2ValidateHull(&hull))
                return false;

            const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
            const b2ShapeId shape = b2CreatePolygonShape(body, &shape_def, &polygon);
            return b2Shape_IsValid(shape);
        }

        std::size_t create_water_sensor_shapes(b2BodyId body, const b2ShapeDef& shape_def, std::vector<vec2> loop)
        {
            if (loop.size() < 3u)
                return 0u;

            if (signed_area(loop) < 0.0f)
                std::ranges::reverse(loop);
            if (std::abs(signed_area(loop)) <= minimum_polygon_area)
                return 0u;

            if (loop.size() <= B2_MAX_POLYGON_VERTICES && is_convex_polygon(loop))
            {
                return create_sensor_polygon(body, shape_def, loop) ? 1u : 0u;
            }

            std::vector<std::size_t> indices(loop.size());
            std::iota(indices.begin(), indices.end(), 0u);

            std::size_t created_count = 0u;
            while (indices.size() > 3u)
            {
                bool clipped_ear = false;

                for (std::size_t i = 0; i < indices.size(); ++i)
                {
                    const auto previous_index = indices[(i + indices.size() - 1u) % indices.size()];
                    const auto current_index = indices[i];
                    const auto next_index = indices[(i + 1u) % indices.size()];

                    const auto& a = loop[previous_index];
                    const auto& b = loop[current_index];
                    const auto& c = loop[next_index];
                    if (cross(a, b, c) <= 1e-4f)
                        continue;

                    bool contains_point = false;
                    for (const auto candidate_index : indices)
                    {
                        if (candidate_index == previous_index || candidate_index == current_index || candidate_index == next_index)
                            continue;
                        if (!point_in_triangle(loop[candidate_index], a, b, c))
                            continue;

                        contains_point = true;
                        break;
                    }

                    if (contains_point)
                        continue;

                    const std::array triangle{a, b, c};
                    if (create_sensor_polygon(body, shape_def, triangle))
                        ++created_count;
                    indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
                    clipped_ear = true;
                    break;
                }

                if (!clipped_ear)
                    break;
            }

            if (indices.size() == 3u)
            {
                const std::array triangle{loop[indices[0]], loop[indices[1]], loop[indices[2]]};
                if (create_sensor_polygon(body, shape_def, triangle))
                    ++created_count;
            }

            return created_count;
        }
    }

    TerrainCollider::TerrainCollider(const b2WorldId world_id) : world_id_{world_id}
    {
    }

    TerrainCollider::~TerrainCollider()
    {
        if (b2Body_IsValid(terrain_body_))
            b2DestroyBody(terrain_body_);
        if (b2Body_IsValid(water_sensor_body_))
            b2DestroyBody(water_sensor_body_);
    }

    TerrainCollider::TerrainCollider(TerrainCollider&& other) noexcept
        : world_id_{other.world_id_}, terrain_body_{std::exchange(other.terrain_body_, b2_nullBodyId)},
          water_sensor_body_{std::exchange(other.water_sensor_body_, b2_nullBodyId)}
    {
        other.world_id_ = b2_nullWorldId;
    }

    TerrainCollider& TerrainCollider::operator=(TerrainCollider&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (b2Body_IsValid(terrain_body_))
            b2DestroyBody(terrain_body_);
        if (b2Body_IsValid(water_sensor_body_))
            b2DestroyBody(water_sensor_body_);

        world_id_ = other.world_id_;
        terrain_body_ = std::exchange(other.terrain_body_, b2_nullBodyId);
        water_sensor_body_ = std::exchange(other.water_sensor_body_, b2_nullBodyId);
        other.world_id_ = b2_nullWorldId;
        return *this;
    }

    void TerrainCollider::build(const std::vector<std::vector<vec2>>& loops,
                                const std::vector<std::vector<vec2>>& paths,
                                const std::vector<std::vector<vec2>>& water_loops)
    {
        const core::ScopedProfiler profiler{"terrain.collider.build"};
        static_cast<void>(profiler);

        if (b2Body_IsValid(terrain_body_))
        {
            b2DestroyBody(terrain_body_);
            terrain_body_ = b2_nullBodyId;
        }

        if (b2Body_IsValid(water_sensor_body_))
        {
            b2DestroyBody(water_sensor_body_);
            water_sensor_body_ = b2_nullBodyId;
        }

        if (loops.empty() && paths.empty() && water_loops.empty())
            return;

        std::size_t created_count = 0;
        if (!loops.empty() || !paths.empty())
        {
            b2BodyDef body_def = b2DefaultBodyDef();
            body_def.type = b2_staticBody;
            body_def.name = "terrain_chunk";

            terrain_body_ = b2CreateBody(world_id_, &body_def);
            if (!b2Body_IsValid(terrain_body_))
                return;

            b2ShapeDef segment_shape_def = b2DefaultShapeDef();
            segment_shape_def.material.friction = 0.9f;
            segment_shape_def.material.restitution = 0.05f;

            for (const auto& loop : loops)
            {
                if (loop.size() < 4)
                    continue;

                auto points = sanitize_points(loop, true);
                if (points.size() < 4u)
                    continue;

                for (std::size_t i = 1; i < points.size(); ++i)
                {
                    const b2Segment segment{.point1 = points[i - 1], .point2 = points[i]};
                    const b2ShapeId shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
                    if (b2Shape_IsValid(shape))
                        ++created_count;
                }

                const b2Segment closing_segment{.point1 = points.back(), .point2 = points.front()};
                const b2ShapeId closing_shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def, &closing_segment);
                if (b2Shape_IsValid(closing_shape))
                    ++created_count;
            }

            for (const auto& path : paths)
            {
                if (path.size() < 2)
                    continue;

                auto points = sanitize_points(path, false);
                if (points.size() < 2u)
                    continue;

                for (std::size_t i = 1; i < points.size(); ++i)
                {
                    const b2Segment segment{.point1 = points[i - 1], .point2 = points[i]};
                    const b2ShapeId shape = b2CreateSegmentShape(terrain_body_, &segment_shape_def, &segment);
                    if (b2Shape_IsValid(shape))
                        ++created_count;
                }
            }
        }

        std::size_t created_sensor_count = 0;
        if (!water_loops.empty())
        {
            b2BodyDef sensor_body_def = b2DefaultBodyDef();
            sensor_body_def.type = b2_staticBody;
            sensor_body_def.name = "water_chunk_sensor";
            water_sensor_body_ = b2CreateBody(world_id_, &sensor_body_def);
            if (!b2Body_IsValid(water_sensor_body_))
                return;

            b2ShapeDef sensor_shape_def = b2DefaultShapeDef();
            sensor_shape_def.isSensor = true;
            sensor_shape_def.enableSensorEvents = true;
            sensor_shape_def.updateBodyMass = false;
            sensor_shape_def.density = 0.0f;

            for (const auto& water_loop : water_loops)
            {
                created_sensor_count += create_water_sensor_shapes(water_sensor_body_, sensor_shape_def, water_loop);
            }
        }

        if (created_count == 0)
        {
            if (b2Body_IsValid(terrain_body_))
            {
                b2DestroyBody(terrain_body_);
                terrain_body_ = b2_nullBodyId;
            }
        }

        if (created_sensor_count == 0)
        {
            if (b2Body_IsValid(water_sensor_body_))
            {
                b2DestroyBody(water_sensor_body_);
                water_sensor_body_ = b2_nullBodyId;
            }
        }
    }

    void TerrainCollider::set_water_enabled(const bool enabled) const
    {
        if (!b2Body_IsValid(water_sensor_body_))
            return;

        const auto currently_enabled = b2Body_IsEnabled(water_sensor_body_);
        if (enabled && !currently_enabled)
            b2Body_Enable(water_sensor_body_);
        if (!enabled && currently_enabled)
            b2Body_Disable(water_sensor_body_);
    }

}
