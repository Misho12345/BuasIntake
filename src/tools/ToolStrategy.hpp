#pragma once

#include "pch.hpp"

#include "terrain/PlanetTerrain.hpp"

namespace game::tools
{
	struct TerrainToolContext final
	{
		b2WorldId world{ b2_nullWorldId };
		terrain::PlanetTerrain* terrain{ nullptr };
		b2BodyId player_body{ b2_nullBodyId };
		vec2 player_world_position{ 0.0f, 0.0f };
		vec2 mouse_world_position{ 0.0f, 0.0f };
	};

	class TerrainTargetResolver;

	class IToolStrategy
	{
	public:
		virtual ~IToolStrategy() = default;

		virtual void activate() {}
		virtual void deactivate() {}
		virtual void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) = 0;
		virtual void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button) = 0;
	};
}
