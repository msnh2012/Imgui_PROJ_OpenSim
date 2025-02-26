#include "LOGLParallaxMappingTab.hpp"

#include "oscar/Bindings/ImGuiHelpers.hpp"
#include "oscar/Graphics/Camera.hpp"
#include "oscar/Graphics/Color.hpp"
#include "oscar/Graphics/ColorSpace.hpp"
#include "oscar/Graphics/Graphics.hpp"
#include "oscar/Graphics/GraphicsHelpers.hpp"
#include "oscar/Graphics/Material.hpp"
#include "oscar/Graphics/Mesh.hpp"
#include "oscar/Graphics/MeshGen.hpp"
#include "oscar/Graphics/MeshIndicesView.hpp"
#include "oscar/Graphics/MeshTopology.hpp"
#include "oscar/Graphics/Shader.hpp"
#include "oscar/Graphics/Texture2D.hpp"
#include "oscar/Maths/Transform.hpp"
#include "oscar/Platform/App.hpp"
#include "oscar/Utils/CStringView.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <SDL_events.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr osc::CStringView c_TabStringID = "LearnOpenGL/ParallaxMapping";

    // matches the quad used in LearnOpenGL's parallax mapping tutorial
    osc::Mesh GenerateQuad()
    {
        std::vector<glm::vec3> const verts =
        {
            {-1.0f,  1.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f},
        };

        std::vector<glm::vec3> const normals =
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };

        std::vector<glm::vec2> const texCoords =
        {
            {0.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
        };

        std::vector<uint16_t> const indices =
        {
            0, 1, 2,
            0, 2, 3,
        };

        std::vector<glm::vec4> const tangents = osc::CalcTangentVectors(
            osc::MeshTopology::Triangles,
            verts,
            normals,
            texCoords,
            osc::MeshIndicesView{indices}
        );
        OSC_ASSERT_ALWAYS(tangents.size() == verts.size());

        osc::Mesh rv;
        rv.setVerts(verts);
        rv.setNormals(normals);
        rv.setTexCoords(texCoords);
        rv.setTangents(tangents);
        rv.setIndices(indices);
        return rv;
    }
}

class osc::LOGLParallaxMappingTab::Impl final {
public:

    Impl(std::weak_ptr<TabHost> parent_) :
        m_Parent{std::move(parent_)}
    {
        m_ParallaxMappingMaterial.setTexture("uDiffuseMap", m_DiffuseMap);
        m_ParallaxMappingMaterial.setTexture("uNormalMap", m_NormalMap);
        m_ParallaxMappingMaterial.setTexture("uDisplacementMap", m_DisplacementMap);
        m_ParallaxMappingMaterial.setFloat("uHeightScale", 0.1f);

        // these roughly match what LearnOpenGL default to
        m_Camera.setPosition({0.0f, 0.0f, 3.0f});
        m_Camera.setCameraFOV(glm::radians(45.0f));
        m_Camera.setNearClippingPlane(0.1f);
        m_Camera.setFarClippingPlane(100.0f);

        m_LightTransform.position = {0.5f, 1.0f, 0.3f};
        m_LightTransform.scale *= 0.2f;
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
        m_IsMouseCaptured = true;
    }

    void onUnmount()
    {
        m_IsMouseCaptured = false;
        App::upd().setShowCursor(true);
    }

    bool onEvent(SDL_Event const& e)
    {
        // handle mouse capturing
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
        // handle mouse capturing and update camera
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

        // clear screen and ensure camera has correct pixel rect
        App::upd().clearScreen({0.1f, 0.1f, 0.1f, 1.0f});

        // draw normal-mapped quad
        {
            m_ParallaxMappingMaterial.setVec3("uLightWorldPos", m_LightTransform.position);
            m_ParallaxMappingMaterial.setVec3("uViewWorldPos", m_Camera.getPosition());
            m_ParallaxMappingMaterial.setBool("uEnableMapping", m_IsMappingEnabled);
            Graphics::DrawMesh(m_QuadMesh, m_QuadTransform, m_ParallaxMappingMaterial, m_Camera);
        }

        // draw light source cube
        {
            m_LightCubeMaterial.setColor("uLightColor", Color::white());
            Graphics::DrawMesh(m_CubeMesh, m_LightTransform, m_LightCubeMaterial, m_Camera);
        }

        m_Camera.setPixelRect(osc::GetMainViewportWorkspaceScreenRect());
        m_Camera.renderToScreen();

        ImGui::Begin("controls");
        ImGui::Checkbox("normal mapping", &m_IsMappingEnabled);
        ImGui::End();
    }

private:
    UID m_TabID;
    std::weak_ptr<TabHost> m_Parent;
    bool m_IsMouseCaptured = false;

    // rendering state
    Material m_ParallaxMappingMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentParallaxMapping.vert"),
            App::slurp("shaders/ExperimentParallaxMapping.frag"),
        },
    };
    Material m_LightCubeMaterial
    {
        Shader
        {
            App::slurp("shaders/ExperimentLightCube.vert"),
            App::slurp("shaders/ExperimentLightCube.frag"),
        },
    };
    Mesh m_CubeMesh = GenLearnOpenGLCube();
    Mesh m_QuadMesh = GenerateQuad();
    Texture2D m_DiffuseMap = LoadTexture2DFromImage(
        App::resource("textures/bricks2.jpg"),
        ColorSpace::sRGB
    );
    Texture2D m_DisplacementMap = LoadTexture2DFromImage(
        App::resource("textures/bricks2_disp.jpg"),
        ColorSpace::Linear
    );
    Texture2D m_NormalMap = LoadTexture2DFromImage(
        App::resource("textures/bricks2_normal.jpg"),
        ColorSpace::Linear
    );

    // scene state
    Camera m_Camera;
    glm::vec3 m_CameraEulers = {0.0f, 0.0f, 0.0f};
    Transform m_QuadTransform;
    Transform m_LightTransform;
    bool m_IsMappingEnabled = true;
};


// public API (PIMPL)

osc::CStringView osc::LOGLParallaxMappingTab::id() noexcept
{
    return c_TabStringID;
}

osc::LOGLParallaxMappingTab::LOGLParallaxMappingTab(std::weak_ptr<TabHost> parent_) :
    m_Impl{std::make_unique<Impl>(std::move(parent_))}
{
}

osc::LOGLParallaxMappingTab::LOGLParallaxMappingTab(LOGLParallaxMappingTab&&) noexcept = default;
osc::LOGLParallaxMappingTab& osc::LOGLParallaxMappingTab::operator=(LOGLParallaxMappingTab&&) noexcept = default;
osc::LOGLParallaxMappingTab::~LOGLParallaxMappingTab() noexcept = default;

osc::UID osc::LOGLParallaxMappingTab::implGetID() const
{
    return m_Impl->getID();
}

osc::CStringView osc::LOGLParallaxMappingTab::implGetName() const
{
    return m_Impl->getName();
}

void osc::LOGLParallaxMappingTab::implOnMount()
{
    m_Impl->onMount();
}

void osc::LOGLParallaxMappingTab::implOnUnmount()
{
    m_Impl->onUnmount();
}

bool osc::LOGLParallaxMappingTab::implOnEvent(SDL_Event const& e)
{
    return m_Impl->onEvent(e);
}

void osc::LOGLParallaxMappingTab::implOnDraw()
{
    m_Impl->onDraw();
}
