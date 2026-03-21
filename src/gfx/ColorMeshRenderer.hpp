#pragma once

#include "pch.hpp"

#include "Mesh.hpp"
#include "Shader.hpp"

namespace game::gfx
{
	class ColorMeshRenderer final
	{
	public:
		ColorMeshRenderer();

		void draw(const Mesh& mesh, const mat4& projection) const;

	private:
		Shader shader_{};
	};
}
