#include "LOGLShadowMappingTab.hpp"

#include "oscar/Bindings/ImGuiHelpers.hpp"
#include "oscar/Graphics/Camera.hpp"
#include "oscar/Graphics/ColorSpace.hpp"
#include "oscar/Graphics/Graphics.hpp"
#include "oscar/Graphics/GraphicsHelpers.hpp"
#include "oscar/Graphics/Material.hpp"
#include "oscar/Graphics/Mesh.hpp"
#include "oscar/Graphics/MeshGen.hpp"
#include "oscar/Graphics/RenderTexture.hpp"
#include "oscar/Graphics/RenderTextureDescriptor.hpp"
#include "oscar/Graphics/Shader.hpp"
#include "oscar/Graphics/Texture2D.hpp"
#include "oscar/Maths/Transform.hpp"
#include "oscar/Platform/App.hpp"
#include "oscar/Utils/CStringView.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <IconsFontAwesome5.h>
#include <SDL_events.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    constexpr osc::CStringView c_TabStringID = "LearnOpenGL/ShadowMapping";

    // this matches the plane vertices used in the LearnOpenGL tutorial
    osc::Mesh GeneratePlaneMesh()
    {
        std::vector<glm::vec3> const verts =
        {
            { 25.0f, -0.5f,  25.0f},
            {-25.0f, -0.5f,  25.0f},
            {-25.0f, -0.5f, -25.0f},

            { 25.0f, -0.5f,  25.0f},
            {-25.0f, -0.5f, -25.0f},
            { 25.0f, -0.5f, -25.0f},
        };

        std::vector<glm::vec3> const normals =
        {
            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},

            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };

        std::vector<glm::vec2> const texCoords =
        {
            {25.0f,  0.0f},
            {0.0f,  0.0f},
            {0.0f, 25.0f},

            {25.0f,  0.0f},
            {0.0f, 25.0f},
            {25.0f, 25.0f},
        };

        std::vector<uint16_t> const indices = {0, 1, 2, 3, 4, 5};

        osc::Mesh rv;
        rv.setVerts(verts);
        rv.setNormals(normals);
        rv.setTexCoords(texCoords);
        rv.setIndices(indices);
        return rv;
    }
}

class osc::LOGLShadowMappingTab::Impl final {
public:

    Impl()
    {
        m_Camera.setNearClippingPlane(0.1f);
        m_Camera.setFarClippingPlane(100.0f);

        RenderTextureDescriptor shadowmapDescriptor{glm::ivec2{1024, 1024}};
        shadowmapDescriptor.setReadWrite(osc::RenderTextureReadWrite::Linear);
        m_DepthTexture.reformat(shadowmapDescriptor);
    }

    UID getID() const
    {
        return m_ID;
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
        m_IsMouseCaptured = false;
        App::upd().makeMainEventLoopWaiting();
        App::upd().setShowCursor(true);
    }

    bool onEvent(SDL_Event const& e)
    {
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            m_IsMouseCaptured = false;
            return true;
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN && osc::IsMouseInMainViewportWorkspaceScreenRect())
        {
            m_IsMouseCaptured = true;
            return true;
        }
        return false;
    }

    void onDraw()
    {
        handleMouseCapture();
        draw3DScene();
    }

private:
    void handleMouseCapture()
    {
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
    }

    void draw3DScene()
    {
        Rect const viewportRect = osc::GetMainViewportWorkspaceScreenRect();

        renderShadowsToDepthTexture();

        m_Camera.setBackgroundColor({0.1f, 0.1f, 0.1f, 1.0f});

        m_SceneMaterial.setVec3("uLightWorldPos", m_LightPos);
        m_SceneMaterial.setVec3("uViewWorldPos", m_Camera.getPosition());
        m_SceneMaterial.setMat4("uLightSpaceMat", m_LatestLightSpaceMatrix);
        m_SceneMaterial.setTexture("uDiffuseTexture", m_WoodTexture);
        m_SceneMaterial.setRenderTexture("uShadowMapTexture", m_DepthTexture);

        drawMeshesWithMaterial(m_SceneMaterial);
        m_Camera.setPixelRect(viewportRect);
        m_Camera.renderToScreen();
        m_Camera.setPixelRect(std::nullopt);
        Graphics::BlitToScreen(m_DepthTexture, Rect{viewportRect.p1, viewportRect.p1 + 200.0f});

        m_SceneMaterial.clearRenderTexture("uShadowMapTexture");
    }

    void drawMeshesWithMaterial(Material const& material)
    {
        // floor
        Graphics::DrawMesh(m_PlaneMesh, Transform{}, material, m_Camera);

        // cubes
        {
            Transform t;
            t.position = {0.0f, 1.0f, 0.0f};
            t.scale = glm::vec3{0.5f};
            Graphics::DrawMesh(m_CubeMesh, t, material, m_Camera);
        }
        {
            Transform t;
            t.position = {2.0f, 0.0f, 1.0f};
            t.scale = glm::vec3{0.5f};
            Graphics::DrawMesh(m_CubeMesh, t, material, m_Camera);
        }
        {
            Transform t;
            t.position = {-1.0f, 0.0f, 2.0f};
            t.rotation = glm::angleAxis(glm::radians(60.0f), glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
            t.scale = glm::vec3{0.25f};
            Graphics::DrawMesh(m_CubeMesh, t, material, m_Camera);
        }
    }

    void renderShadowsToDepthTexture()
    {
        float const zNear = 1.0f;
        float const zFar = 7.5f;
        glm::mat4 const lightViewMatrix = glm::lookAt(m_LightPos, glm::vec3{0.0f}, {0.0f, 1.0f, 0.0f});
        glm::mat4 const lightProjMatrix = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, zNear, zFar);
        m_LatestLightSpaceMatrix = lightProjMatrix * lightViewMatrix;

        drawMeshesWithMaterial(m_DepthMaterial);

        m_Camera.setViewMatrixOverride(lightViewMatrix);
        m_Camera.setProjectionMatrixOverride(lightProjMatrix);
        m_Camera.renderTo(m_DepthTexture);
        m_Camera.setViewMatrixOverride(std::nullopt);
        m_Camera.setProjectionMatrixOverride(std::nullopt);
    }

    UID m_ID;
    bool m_IsMouseCaptured = false;

    Camera m_Camera;
    glm::vec3 m_CameraEulers = {0.0f, 0.0f, 0.0f};
    Texture2D m_WoodTexture = LoadTexture2DFromImage(
        App::resource("textures/wood.png"),
        ColorSpace::sRGB
    );
    Mesh m_CubeMesh = osc::GenCube();
    Mesh m_PlaneMesh = GeneratePlaneMesh();
    Material m_SceneMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentShadowMapping.vert"),
            App::slurp("shaders/ExperimentShadowMapping.frag"),
        },
    };
    Material m_DepthMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentShadowMappingDepth.vert"),
            App::slurp("shaders/ExperimentShadowMappingDepth.frag"),
        },
    };
    RenderTexture m_DepthTexture;
    glm::mat4 m_LatestLightSpaceMatrix{1.0f};
    glm::vec3 m_LightPos = {-2.0f, 4.0f, -1.0f};
};


// public API (PIMPL)

osc::CStringView osc::LOGLShadowMappingTab::id() noexcept
{
    return c_TabStringID;
}

osc::LOGLShadowMappingTab::LOGLShadowMappingTab(std::weak_ptr<TabHost>) :
    m_Impl{std::make_unique<Impl>()}
{
}

osc::LOGLShadowMappingTab::LOGLShadowMappingTab(LOGLShadowMappingTab&&) noexcept = default;
osc::LOGLShadowMappingTab& osc::LOGLShadowMappingTab::operator=(LOGLShadowMappingTab&&) noexcept = default;
osc::LOGLShadowMappingTab::~LOGLShadowMappingTab() noexcept = default;

osc::UID osc::LOGLShadowMappingTab::implGetID() const
{
    return m_Impl->getID();
}

osc::CStringView osc::LOGLShadowMappingTab::implGetName() const
{
    return m_Impl->getName();
}

void osc::LOGLShadowMappingTab::implOnMount()
{
    m_Impl->onMount();
}

void osc::LOGLShadowMappingTab::implOnUnmount()
{
    m_Impl->onUnmount();
}

bool osc::LOGLShadowMappingTab::implOnEvent(SDL_Event const& e)
{
    return m_Impl->onEvent(e);
}

void osc::LOGLShadowMappingTab::implOnDraw()
{
    m_Impl->onDraw();
}
