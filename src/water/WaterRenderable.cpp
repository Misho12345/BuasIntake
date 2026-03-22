#include "pch.hpp"
#include "WaterRenderable.hpp"

#include "gfx/Projection.hpp"

namespace game::water
{
	WaterRenderable::WaterRenderable() : shader_{
		gfx::Shader::from_graphics_files(
			"assets/shaders/water_mesh.vert",
			"assets/shaders/water_mesh.frag")
	} {}

	void WaterRenderable::draw([[maybe_unused]] const gfx::Mesh& mesh, [[maybe_unused]] const sf::View& view) const
	{
		[[maybe_unused]]
		const auto projection = gfx::make_projection(view);

		if (!shader_.valid()) return;

		// TODO: implement water mesh rendering.
	}
}
