#include "LOGLBloomTab.hpp"

#include "oscar/Bindings/ImGuiHelpers.hpp"
#include "oscar/Graphics/Camera.hpp"
#include "oscar/Graphics/Color.hpp"
#include "oscar/Graphics/Graphics.hpp"
#include "oscar/Graphics/GraphicsHelpers.hpp"
#include "oscar/Graphics/Material.hpp"
#include "oscar/Graphics/Mesh.hpp"
#include "oscar/Graphics/MeshGen.hpp"
#include "oscar/Graphics/RenderTarget.hpp"
#include "oscar/Graphics/RenderTextureDescriptor.hpp"
#include "oscar/Graphics/RenderTexture.hpp"
#include "oscar/Graphics/Shader.hpp"
#include "oscar/Graphics/Texture2D.hpp"
#include "oscar/Maths/Transform.hpp"
#include "oscar/Maths/MathHelpers.hpp"
#include "oscar/Platform/App.hpp"
#include "oscar/Utils/Algorithms.hpp"
#include "oscar/Utils/CStringView.hpp"

#include <glm/vec3.hpp>
#include <IconsFontAwesome5.h>
#include <imgui.h>
#include <SDL_events.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr osc::CStringView c_TabStringID = "LearnOpenGL/Bloom";

    constexpr auto c_SceneLightPositions = osc::MakeArray<glm::vec3>(
        glm::vec3{ 0.0f, 0.5f,  1.5f},
        glm::vec3{-4.0f, 0.5f, -3.0f},
        glm::vec3{ 3.0f, 0.5f,  1.0f},
        glm::vec3{-0.8f, 2.4f, -1.0f}
    );

    auto c_SceneLightColors = osc::MakeSizedArray<osc::Color, c_SceneLightPositions.size()>(
        osc::ToSRGB(osc::Color{ 5.0f, 5.0f,  5.0f}),
        osc::ToSRGB(osc::Color{10.0f, 0.0f,  0.0f}),
        osc::ToSRGB(osc::Color{ 0.0f, 0.0f, 15.0f}),
        osc::ToSRGB(osc::Color{ 0.0f, 5.0f,  0.0f})
    );

    std::vector<glm::mat4> CreateCubeTransforms()
    {
        std::vector<glm::mat4> rv;

        {
            glm::mat4 m{1.0f};
            m = glm::translate(m, glm::vec3(0.0f, 1.5f, 0.0));
            m = glm::scale(m, glm::vec3(0.5f));
            rv.push_back(m);
        }

        {
            glm::mat4 m{1.0f};
            m = glm::translate(m, glm::vec3(2.0f, 0.0f, 1.0));
            m = glm::scale(m, glm::vec3(0.5f));
            rv.push_back(m);
        }

        {
            glm::mat4 m{1.0f};
            m = glm::translate(m, glm::vec3(-1.0f, -1.0f, 2.0));
            m = glm::rotate(m, glm::radians(60.0f), glm::normalize(glm::vec3(1.0, 0.0, 1.0)));
            rv.push_back(m);
        }

        {
            glm::mat4 m{1.0f};
            m = glm::translate(m, glm::vec3(0.0f, 2.7f, 4.0));
            m = glm::rotate(m, glm::radians(23.0f), glm::normalize(glm::vec3(1.0, 0.0, 1.0)));
            m = glm::scale(m, glm::vec3(1.25));
            rv.push_back(m);
        }

        {
            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-2.0f, 1.0f, -3.0));
            m = glm::rotate(m, glm::radians(124.0f), glm::normalize(glm::vec3(1.0, 0.0, 1.0)));
            rv.push_back(m);
        }

        {
            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-3.0f, 0.0f, 0.0));
            m = glm::scale(m, glm::vec3(0.5f));
            rv.push_back(m);
        }

        return rv;
    }
}

class osc::LOGLBloomTab::Impl final {
public:

    Impl(std::weak_ptr<TabHost> parent_) :
        m_Parent{std::move(parent_)}
    {
        m_Camera.setPosition({0.0f, 0.0f, 5.0f});
        m_Camera.setNearClippingPlane(0.1f);
        m_Camera.setFarClippingPlane(100.0f);
        m_Camera.setBackgroundColor({0.0f, 0.0f, 0.0f, 1.0f});

        m_SceneMaterial.setVec3Array("uLightPositions", c_SceneLightPositions);
        m_SceneMaterial.setColorArray("uLightColors", c_SceneLightColors);
    }

    UID getID() const
    {
        return m_TabID;
    }

    CStringView getName() const
    {
        return c_TabStringID;
    }

    void onMount()
    {
        App::upd().makeMainEventLoopPolling();
        m_IsMouseCaptured = true;
    }

    void onUnmount()
    {
        App::upd().setShowCursor(true);
        App::upd().makeMainEventLoopWaiting();
        m_IsMouseCaptured = false;
    }

    bool onEvent(SDL_Event const& e)
    {
        // handle mouse input
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            m_IsMouseCaptured = false;
            return true;
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN && IsMouseInMainViewportWorkspaceScreenRect())
        {
            m_IsMouseCaptured = true;
            return true;
        }
        return false;
    }

    void onTick() {}
    void onDrawMainMenu() {}

    void onDraw()
    {
        // handle mouse capturing
        if (m_IsMouseCaptured)
        {
            UpdateEulerCameraFromImGuiUserInput(m_Camera, m_CameraEulers);
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            App::upd().setShowCursor(false);
        }
        else
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            App::upd().setShowCursor(true);
        }

        draw3DScene();
    }

private:
    void draw3DScene()
    {
        Rect const viewportRect = GetMainViewportWorkspaceScreenRect();

        reformatAllTextures(viewportRect);
        renderSceneMRT();
        renderBlurredBrightness();
        renderCombinedScene(viewportRect);
        drawOverlays(viewportRect);
    }

    void reformatAllTextures(Rect const& viewportRect)
    {
        glm::vec2 const viewportDims = Dimensions(viewportRect);
        int32_t const msxaaSamples = App::get().getMSXAASamplesRecommended();

        RenderTextureDescriptor textureDescription{viewportDims};
        textureDescription.setAntialiasingLevel(msxaaSamples);
        textureDescription.setColorFormat(RenderTextureFormat::DefaultHDR);

        // direct render targets are multisampled HDR textures
        m_SceneHDRColorOutput.reformat(textureDescription);
        m_SceneHDRThresholdedOutput.reformat(textureDescription);

        // intermediate buffers are single-sampled HDR textures
        textureDescription.setAntialiasingLevel(1);
        for (RenderTexture& pingPongBuffer : m_PingPongBlurOutputBuffers)
        {
            pingPongBuffer.reformat(textureDescription);
        }
    }

    void renderSceneMRT()
    {
        drawSceneCubesToCamera();
        drawLightBoxesToCamera();
        flushCameraRenderQueueToMRT();
    }

    void drawSceneCubesToCamera()
    {
        m_SceneMaterial.setVec3("uViewWorldPos", m_Camera.getPosition());

        // draw floor
        {
            glm::mat4 floorTransform{1.0f};
            floorTransform = glm::translate(floorTransform, glm::vec3(0.0f, -1.0f, 0.0));
            floorTransform = glm::scale(floorTransform, glm::vec3(12.5f, 0.5f, 12.5f));

            MaterialPropertyBlock floorProps;
            floorProps.setTexture("uDiffuseTexture", m_WoodTexture);

            Graphics::DrawMesh(
                m_CubeMesh,
                floorTransform,
                m_SceneMaterial,
                m_Camera,
                std::move(floorProps)
            );
        }

        MaterialPropertyBlock cubeProps;
        cubeProps.setTexture("uDiffuseTexture", m_ContainerTexture);
        for (auto const& cubeTransform : CreateCubeTransforms())
        {
            Graphics::DrawMesh(
                m_CubeMesh,
                cubeTransform,
                m_SceneMaterial,
                m_Camera,
                cubeProps
            );
        }
    }

    void drawLightBoxesToCamera()
    {
        static_assert(c_SceneLightPositions.size() == c_SceneLightColors.size());

        for (size_t i = 0; i < c_SceneLightPositions.size(); ++i)
        {
            glm::mat4 lightTransform{1.0f};
            lightTransform = glm::translate(lightTransform, glm::vec3(c_SceneLightPositions[i]));
            lightTransform = glm::scale(lightTransform, glm::vec3(0.25f));

            MaterialPropertyBlock lightProps;
            lightProps.setColor("uLightColor", c_SceneLightColors[i]);

            Graphics::DrawMesh(
                m_CubeMesh,
                lightTransform,
                m_LightboxMaterial,
                m_Camera,
                std::move(lightProps)
            );
        }
    }

    void flushCameraRenderQueueToMRT()
    {
        RenderTarget mrt
        {
            {
                RenderTargetColorAttachment
                {
                    m_SceneHDRColorOutput.updColorBuffer(),
                    RenderBufferLoadAction::Clear,
                    RenderBufferStoreAction::Resolve,
                    Color::clear(),
                },
                RenderTargetColorAttachment
                {
                    m_SceneHDRThresholdedOutput.updColorBuffer(),
                    RenderBufferLoadAction::Clear,
                    RenderBufferStoreAction::Resolve,
                    Color::clear(),
                },
            },
            RenderTargetDepthAttachment
            {
                m_SceneHDRThresholdedOutput.updDepthBuffer(),
                RenderBufferLoadAction::Clear,
                RenderBufferStoreAction::DontCare,
            },
        };
        m_Camera.renderTo(mrt);
    }

    void renderBlurredBrightness()
    {
        m_BlurMaterial.setRenderTexture("uInputImage", m_SceneHDRThresholdedOutput);

        bool horizontal = false;
        for (RenderTexture& pingPongBuffer : m_PingPongBlurOutputBuffers)
        {
            m_BlurMaterial.setBool("uHorizontal", horizontal);
            Camera camera;
            Graphics::DrawMesh(m_QuadMesh, Transform{}, m_BlurMaterial, camera);
            camera.renderTo(pingPongBuffer);
            m_BlurMaterial.clearRenderTexture("uInputImage");

            horizontal = !horizontal;
        }
    }

    void renderCombinedScene(Rect const& viewportRect)
    {
        m_FinalCompositingMaterial.setRenderTexture("uHDRSceneRender", m_SceneHDRColorOutput);
        m_FinalCompositingMaterial.setRenderTexture("uBloomBlur", m_PingPongBlurOutputBuffers[0]);
        m_FinalCompositingMaterial.setBool("uBloom", true);
        m_FinalCompositingMaterial.setFloat("uExposure", 1.0f);

        Camera camera;
        Graphics::DrawMesh(m_QuadMesh, Transform{}, m_FinalCompositingMaterial, camera);
        camera.setPixelRect(viewportRect);
        camera.renderToScreen();

        m_FinalCompositingMaterial.clearRenderTexture("uBloomBlur");
        m_FinalCompositingMaterial.clearRenderTexture("uHDRSceneRender");
    }

    void drawOverlays(Rect const& viewportRect)
    {
        float constexpr w = 200.0f;

        auto const textures = MakeArray<RenderTexture const*>(
            &m_SceneHDRColorOutput,
            &m_SceneHDRThresholdedOutput,
            &m_PingPongBlurOutputBuffers[0],
            &m_PingPongBlurOutputBuffers[1]
        );

        for (size_t i = 0; i < textures.size(); ++i)
        {
            glm::vec2 const offset = {static_cast<float>(i)*w, 0.0f};
            Rect const overlayRect
            {
                viewportRect.p1 + offset,
                viewportRect.p1 + offset + w
            };

            Graphics::BlitToScreen(*textures[i], overlayRect);
        }
    }

    UID m_TabID;
    std::weak_ptr<TabHost> m_Parent;

    Material m_SceneMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentBloom.vert"),
            App::slurp("shaders/ExperimentBloom.frag"),
        },
    };

    Material m_LightboxMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentBloomLightBox.vert"),
            App::slurp("shaders/ExperimentBloomLightBox.frag"),
        },
    };

    Material m_BlurMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentBloomBlur.vert"),
            App::slurp("shaders/ExperimentBloomBlur.frag"),
        },
    };

    Material m_FinalCompositingMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentBloomFinal.vert"),
            App::slurp("shaders/ExperimentBloomFinal.frag"),
        },
    };

    Texture2D m_WoodTexture = osc::LoadTexture2DFromImage(
        App::resource("textures/wood.png"),
        ColorSpace::sRGB
    );
    Texture2D m_ContainerTexture = osc::LoadTexture2DFromImage(
        App::resource("textures/container2.png"),
        ColorSpace::sRGB
    );
    Mesh m_CubeMesh = GenCube();
    Mesh m_QuadMesh = GenTexturedQuad();

    RenderTexture m_SceneHDRColorOutput;
    RenderTexture m_SceneHDRThresholdedOutput;
    std::array<RenderTexture, 2> m_PingPongBlurOutputBuffers;

    Camera m_Camera;
    bool m_IsMouseCaptured = true;
    glm::vec3 m_CameraEulers = {0.0f, 0.0f, 0.0f};

    bool m_UseBloom = true;
    float m_Exposure = 1.0f;
};


// public API (PIMPL)

osc::CStringView osc::LOGLBloomTab::id() noexcept
{
    return c_TabStringID;
}

osc::LOGLBloomTab::LOGLBloomTab(std::weak_ptr<TabHost> parent_) :
    m_Impl{std::make_unique<Impl>(std::move(parent_))}
{
}

osc::LOGLBloomTab::LOGLBloomTab(LOGLBloomTab&&) noexcept = default;
osc::LOGLBloomTab& osc::LOGLBloomTab::operator=(LOGLBloomTab&&) noexcept = default;
osc::LOGLBloomTab::~LOGLBloomTab() noexcept = default;

osc::UID osc::LOGLBloomTab::implGetID() const
{
    return m_Impl->getID();
}

osc::CStringView osc::LOGLBloomTab::implGetName() const
{
    return m_Impl->getName();
}

void osc::LOGLBloomTab::implOnMount()
{
    m_Impl->onMount();
}

void osc::LOGLBloomTab::implOnUnmount()
{
    m_Impl->onUnmount();
}

bool osc::LOGLBloomTab::implOnEvent(SDL_Event const& e)
{
    return m_Impl->onEvent(e);
}

void osc::LOGLBloomTab::implOnTick()
{
    m_Impl->onTick();
}

void osc::LOGLBloomTab::implOnDrawMainMenu()
{
    m_Impl->onDrawMainMenu();
}

void osc::LOGLBloomTab::implOnDraw()
{
    m_Impl->onDraw();
}
