#pragma once

#include "src/Graphics/BasicRenderer.hpp"

#include <glm/vec2.hpp>
#include <nonstd/span.hpp>

namespace osc
{
	class Texture2D;
}

namespace osc
{
	class SceneRenderer final : public BasicRenderer {
	public:
		SceneRenderer();
		SceneRenderer(SceneRenderer const&) = delete;
		SceneRenderer(SceneRenderer&&) noexcept;
		SceneRenderer& operator=(SceneRenderer const&) = delete;
		SceneRenderer& operator=(SceneRenderer&&) noexcept;
		~SceneRenderer() noexcept;

		glm::ivec2 getDimensions() const override;
		void setDimensions(glm::ivec2) override;
		int getSamples() const override;
		void setSamples(int) override;
		void draw(BasicRenderer::Params const&, nonstd::span<BasicSceneElement const>) override;
		gl::Texture2D& updOutputTexture() override;
		
		class Impl;
	private:
		Impl* m_Impl;
	};
}