#pragma once

#include "pch.hpp"

namespace game::gfx
{
	class ScopedAlphaBlendPass final
	{
	public:
		ScopedAlphaBlendPass()
		{
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		~ScopedAlphaBlendPass()
		{
			glUseProgram(0);
		}

		ScopedAlphaBlendPass(const ScopedAlphaBlendPass&) = delete;
		ScopedAlphaBlendPass& operator=(const ScopedAlphaBlendPass&) = delete;
		ScopedAlphaBlendPass(ScopedAlphaBlendPass&&) = delete;
		ScopedAlphaBlendPass& operator=(ScopedAlphaBlendPass&&) = delete;
	};
}
