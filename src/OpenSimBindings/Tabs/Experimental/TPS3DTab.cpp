#include "TPS3DTab.hpp"

#include "src/Bindings/ImGuiHelpers.hpp"
#include "src/Bindings/GlmHelpers.hpp"
#include "src/Graphics/Camera.hpp"
#include "src/Graphics/Graphics.hpp"
#include "src/Graphics/GraphicsHelpers.hpp"
#include "src/Graphics/Material.hpp"
#include "src/Graphics/Mesh.hpp"
#include "src/Graphics/MeshCache.hpp"
#include "src/Graphics/MeshGen.hpp"
#include "src/Graphics/SceneDecoration.hpp"
#include "src/Graphics/SceneRenderer.hpp"
#include "src/Graphics/SceneRendererParams.hpp"
#include "src/Graphics/ShaderCache.hpp"
#include "src/Maths/Constants.hpp"
#include "src/Maths/MathHelpers.hpp"
#include "src/Maths/PolarPerspectiveCamera.hpp"
#include "src/Platform/App.hpp"
#include "src/Platform/Log.hpp"
#include "src/Utils/Algorithms.hpp"
#include "src/Widgets/LogViewerPanel.hpp"

#include <glm/mat3x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <IconsFontAwesome5.h>
#include <imgui.h>
#include <nonstd/span.hpp>
#include <SDL_events.h>
#include <Simbody.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <sstream>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

// 3D TPS algorithm stuff
//
// most of the background behind this is discussed in issue #467. For redundancy's sake, here
// are some of the references used to write this implementation:
//
// - primary literature source: https://ieeexplore.ieee.org/document/24792
// - blog explanation: https://profs.etsmtl.ca/hlombaert/thinplates/
// - blog explanation #2: https://khanhha.github.io/posts/Thin-Plate-Splines-Warping/
namespace
{
    // a single source-to-destination landmark pair in 3D space
    //
    // this is typically what the user/caller defines
    struct LandmarkPair3D final {
        glm::vec3 src;
        glm::vec3 dest;
    };

    // pretty-prints `LandmarkPair3D`
    std::ostream& operator<<(std::ostream& o, LandmarkPair3D const& p)
    {
        using osc::operator<<;
        o << "LandmarkPair3D{src = " << p.src << ", dest = " << p.dest << '}';
        return o;
    }

    // this is effectviely the "U" term in the TPS algorithm literature (which is usually U(r) = r^2 * log(r^2))
    //
    // i.e. U(||pi - p||) in the literature is equivalent to `RadialBasisFunction3D(pi, p)` here
    float RadialBasisFunction3D(glm::vec3 controlPoint, glm::vec3 p)
    {
        glm::vec3 const diff = controlPoint - p;
        float const r2 = glm::dot(diff, diff);

        if (r2 == 0.0f)
        {
            // this ensures that the result is always non-zero and non-NaN (this might be
            // necessary for some types of linear solvers?)
            return std::numeric_limits<float>::min();
        }
        else
        {
            return r2 * std::log(r2);
        }
    }

    // a single non-affine term of the 3D TPS equation
    //
    // i.e. in `f(p) = a1 + a2*p.x + a3*p.y + a4*p.z + SUM{ wi * U(||controlPoint - p||) }` this encodes
    //      the `wi` and `controlPoint` parts of that equation
    struct TPSNonAffineTerm3D final {
        glm::vec3 weight;
        glm::vec3 controlPoint;

        TPSNonAffineTerm3D(glm::vec3 const& weight_, glm::vec3 const& controlPoint_) :
            weight{weight_},
            controlPoint{controlPoint_}
        {
        }
    };

    // pretty-prints `TPSNonAffineTerm3D`
    std::ostream& operator<<(std::ostream& o, TPSNonAffineTerm3D const& wt)
    {
        using osc::operator<<;
        return o << "TPSNonAffineTerm3D{weight = " << wt.weight << ", controlPoint = " << wt.controlPoint << '}';
    }

    // all coefficients in the 3D TPS equation
    //
    // i.e. these are the a1, a2, a3, a4, and w's (+ control points) terms of the equation
    struct TPSCoefficients3D final {
        glm::vec3 a1 = {0.0f, 0.0f, 0.0f};
        glm::vec3 a2 = {1.0f, 0.0f, 0.0f};
        glm::vec3 a3 = {0.0f, 1.0f, 0.0f};
        glm::vec3 a4 = {0.0f, 0.0f, 1.0f};
        std::vector<TPSNonAffineTerm3D> weights;
    };

    // pretty-prints TPSCoefficients3D
    std::ostream& operator<<(std::ostream& o, TPSCoefficients3D const& coefs)
    {
        using osc::operator<<;
        o << "TPSCoefficients3D{a1 = " << coefs.a1 << ", a2 = " << coefs.a2 << ", a3 = " << coefs.a3 << ", a4 = " << coefs.a4;
        for (size_t i = 0; i < coefs.weights.size(); ++i)
        {
            o << ", w" << i << " = " << coefs.weights[i];
        }
        o << '}';
        return o;
    }

    // evaluates the TPS equation with the given coefficients and input point
    glm::vec3 Evaluate(TPSCoefficients3D const& coefs, glm::vec3 const& p)
    {
        // this implementation effectively evaluates `fx(x, y, z)`, `fy(x, y, z)`, and
        // `fz(x, y, z)` the same time, because `TPSCoefficients3D` stores the X, Y, and Z
        // variants of the coefficients together in memory (as `vec3`s)

        // compute affine terms (a1 + a2*x + a3*y + a4*z)
        glm::vec3 rv = coefs.a1 + coefs.a2*p.x + coefs.a3*p.y + coefs.a4*p.z;

        // accumulate non-affine terms (effectively: wi * U(||controlPoint - p||))
        for (TPSNonAffineTerm3D const& wt : coefs.weights)
        {
            rv += wt.weight * RadialBasisFunction3D(wt.controlPoint, p);
        }

        return rv;
    }

    // computes all coefficients of the 3D TPS equation (a1, a2, a3, a4, and all the w's)
    TPSCoefficients3D CalcCoefficients(nonstd::span<LandmarkPair3D const> landmarkPairs)
    {
        // this is based on the Bookstein Thin Plate Sline (TPS) warping algorithm
        //
        // 1. A TPS warp is (simplifying here) a linear combination:
        //
        //     f(p) = a1 + a2*p.x + a3*p.y + a4*p.z + SUM{ wi * U(||controlPoint_i - p||) }
        //
        //    which can be represented as a matrix multiplication between the terms (1, p.x, p.y,
        //    p.z, U(||cpi - p||)) and the coefficients (a1, a2, a3, a4, wi..)
        //
        // 2. The caller provides "landmark pairs": these are (effectively) the input
        //    arguments and the expected output
        //
        // 3. This algorithm uses the input + output to solve for the linear coefficients.
        //    Once those coefficients are known, we then have a linear equation that we
        //    we can pump new inputs into (e.g. mesh points, muscle points)
        //
        // 4. So, given the equation L * [w a] = [v o], where L is a matrix of linear terms,
        //    [w a] is a vector of the linear coefficients (we're solving for these), and [v o]
        //    is the expected output (v), with some (padding) zero elements (o)
        //
        // 5. Create matrix L:
        //
        //   |K  P|
        //   |PT 0|
        //
        //     where:
        //
        //     - K is a symmetric matrix of each *input* landmark pair evaluated via the
        //       basis function:
        //
        //        |U(p00) U(p01) U(p02)  ...  |
        //        |U(p10) U(p11) U(p12)  ...  |
        //        | ...    ...    ...   U(pnn)|
        //
        //     - P is a n-row 4-column matrix containing the number 1 (the constant term),
        //       x, y, and z (effectively, the p term):
        //
        //       |1 x1 y1 z1|
        //       |1 x2 y2 z2|
        //
        //     - PT is the transpose of P
        //     - 0 is a 4x4 zero matrix (padding)
        //
        // 6. Use a linear solver to solve L * [w a] = [v o] to yield [w a]
        // 8. Return the coefficients, [w a]

        int const numPairs = static_cast<int>(landmarkPairs.size());

        if (numPairs == 0)
        {
            // edge-case: there are no pairs, so return an identity-like transform
            return TPSCoefficients3D{};
        }

        // construct matrix L
        SimTK::Matrix L(numPairs + 4, numPairs + 4);

        // populate the K part of matrix L (upper-left)
        for (int row = 0; row < numPairs; ++row)
        {
            for (int col = 0; col < numPairs; ++col)
            {
                glm::vec3 const& pi = landmarkPairs[row].src;
                glm::vec3 const& pj = landmarkPairs[col].src;

                L(row, col) = RadialBasisFunction3D(pi, pj);
            }
        }

        // populate the P part of matrix L (upper-right)
        {
            int const pStartColumn = numPairs;

            for (int row = 0; row < numPairs; ++row)
            {
                L(row, pStartColumn)     = 1.0;
                L(row, pStartColumn + 1) = landmarkPairs[row].src.x;
                L(row, pStartColumn + 2) = landmarkPairs[row].src.y;
                L(row, pStartColumn + 3) = landmarkPairs[row].src.z;
            }
        }

        // populate the PT part of matrix L (bottom-left)
        {
            int const ptStartRow = numPairs;

            for (int col = 0; col < numPairs; ++col)
            {
                L(ptStartRow, col)     = 1.0;
                L(ptStartRow + 1, col) = landmarkPairs[col].src.x;
                L(ptStartRow + 2, col) = landmarkPairs[col].src.y;
                L(ptStartRow + 3, col) = landmarkPairs[col].src.z;
            }
        }

        // populate the 0 part of matrix L (bottom-right)
        {
            int const zeroStartRow = numPairs;
            int const zeroStartCol = numPairs;

            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    L(zeroStartRow + row, zeroStartCol + col) = 0.0;
                }
            }
        }

        // construct "result" vectors Vx and Vy (these hold the landmark destinations)
        SimTK::Vector Vx(numPairs + 4, 0.0);
        SimTK::Vector Vy(numPairs + 4, 0.0);
        SimTK::Vector Vz(numPairs + 4, 0.0);
        for (int row = 0; row < numPairs; ++row)
        {
            Vx[row] = landmarkPairs[row].dest.x;
            Vy[row] = landmarkPairs[row].dest.y;
            Vz[row] = landmarkPairs[row].dest.z;
        }

        // construct coefficient vectors that will receive the solver's result
        SimTK::Vector Cx(numPairs + 4, 0.0);
        SimTK::Vector Cy(numPairs + 4, 0.0);
        SimTK::Vector Cz(numPairs + 4, 0.0);

        // solve `L*Cx = Vx` and `L*Cy = Vy` for `Cx` and `Cy` (the coefficients)
        SimTK::FactorQTZ F(L);
        F.solve(Vx, Cx);
        F.solve(Vy, Cy);
        F.solve(Vz, Cz);

        // the coefficient vectors now contain (e.g. for X): [w1, w2, ... wx, a0, a1x, a1y a1z]
        //
        // extract them into the return value

        TPSCoefficients3D rv;

        // populate affine a1, a2, a3, and a4 terms
        rv.a1 = {Cx[numPairs],   Cy[numPairs]  , Cz[numPairs]  };
        rv.a2 = {Cx[numPairs+1], Cy[numPairs+1], Cz[numPairs+1]};
        rv.a3 = {Cx[numPairs+2], Cy[numPairs+2], Cz[numPairs+2]};
        rv.a4 = {Cx[numPairs+3], Cy[numPairs+3], Cz[numPairs+3]};

        // populate `wi` coefficients (+ control points, needed at evaluation-time)
        rv.weights.reserve(numPairs);
        for (int i = 0; i < numPairs; ++i)
        {
            glm::vec3 const weight = {Cx[i], Cy[i], Cz[i]};
            glm::vec3 const controlPoint = landmarkPairs[i].src;
            rv.weights.emplace_back(weight, controlPoint);
        }

        return rv;
    }

    // a class that wraps the 3D TPS algorithm with a basic interface for transforming
    // points
    class ThinPlateWarper3D final {
    public:
        ThinPlateWarper3D(nonstd::span<LandmarkPair3D const> landmarkPairs) :
            m_Coefficients{CalcCoefficients(landmarkPairs)}
        {
        }

        glm::vec3 transform(glm::vec3 const& p) const
        {
            return Evaluate(m_Coefficients, p);
        }

    private:
        TPSCoefficients3D m_Coefficients;
    };

    // returns a mesh that is the equivalent of applying the 3D TPS warp to all
    // vertices of the input mesh
    osc::Mesh ApplyThinPlateWarpToMesh(ThinPlateWarper3D const& t, osc::Mesh const& mesh)
    {
        // load source points
        nonstd::span<glm::vec3 const> srcPoints = mesh.getVerts();

        // map each source point via the warper
        std::vector<glm::vec3> destPoints;
        destPoints.reserve(srcPoints.size());
        for (glm::vec3 const& srcPoint : srcPoints)
        {
            destPoints.emplace_back(t.transform(srcPoint));
        }

        // upload the new points into the returned mesh
        osc::Mesh rv = mesh;
        rv.setVerts(destPoints);
        return rv;
    }

    using PanelFlags = int;
    enum PanelFlags_ {
        PanelFlags_HandleLandmarks = 1<<0,
        PanelFlags_WireframeMode = 1<<1,
        PanelFlags_Default = PanelFlags_WireframeMode | PanelFlags_HandleLandmarks,
    };

    // data associated with one 3D viewer panel
    struct PanelData {
        explicit PanelData(osc::Mesh mesh_) :
            m_Mesh{std::move(mesh_)}
        {
            m_Camera.radius = 4.0f * osc::LongestDim(osc::Dimensions(m_Mesh.getBounds()));
            m_Camera.theta = 0.25f * osc::fpi;
            m_Camera.phi = 0.25f * osc::fpi;
            m_RenderParams.drawFloor = false;
            m_RenderParams.backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f};
        }

        osc::SceneRendererParams m_RenderParams;
        osc::PolarPerspectiveCamera m_Camera;
        osc::Mesh m_Mesh;
        osc::SceneRenderer m_Renderer;
        std::unordered_map<std::string, glm::vec3> m_Landmarks;
        PanelFlags m_Flags = PanelFlags_Default;
    };

    // draw a 3D viewer panel via ImGui
    void DrawPanelDataInContentRegionAvail(PanelData& d)
    {
        // figure out where to draw it
        glm::vec2 const availableSpace = ImGui::GetContentRegionAvail();

        // setup rendering parameters
        d.m_RenderParams.dimensions = availableSpace;
        d.m_RenderParams.viewMatrix = d.m_Camera.getViewMtx();
        d.m_RenderParams.projectionMatrix = d.m_Camera.getProjMtx(osc::AspectRatio(availableSpace));
        d.m_RenderParams.samples = osc::App::get().getMSXAASamplesRecommended();
        d.m_RenderParams.lightDirection = osc::RecommendedLightDirection(d.m_Camera);

        osc::Material wireframeOverlay{osc::ShaderCache::get("shaders/SceneSolidColor.vert", "shaders/SceneSolidColor.frag")};
        wireframeOverlay.setVec4("uDiffuseColor", {0.0f, 0.0f, 0.0f, 1.0f});
        wireframeOverlay.setWireframeMode(true);


        // generate in-scene 3D decorations
        std::vector<osc::SceneDecoration> decorations;
        decorations.reserve(5 + (d.m_Flags & PanelFlags_HandleLandmarks ? d.m_Landmarks.size() : 0));

        // decorations: draw the mesh
        decorations.emplace_back(d.m_Mesh);

        if (d.m_Flags & PanelFlags_WireframeMode)
        {
            decorations.emplace_back(d.m_Mesh).maybeMaterial = wireframeOverlay;
        }

        // decorations: draw each landmark as a sphere
        if (d.m_Flags & PanelFlags_HandleLandmarks)
        {
            for (auto const& [landmarkName, landmarkPos] : d.m_Landmarks)
            {
                std::shared_ptr<osc::Mesh const> const sphere = osc::App::meshes().getSphereMesh();
                osc::Transform transform{};
                transform.scale *= 0.05f;
                transform.position = landmarkPos;
                glm::vec4 const color = {1.0f, 0.0f, 0.0f, 1.0f};

                decorations.emplace_back(sphere, transform, color);
            }
        }

        // add grid
        DrawXZGrid(decorations);
        DrawXZFloorLines(decorations, 100.0f);

        // render
        d.m_Renderer.draw(decorations, d.m_RenderParams);

        // test whether the user is interacting with this particular panel
        osc::ImGuiImageHittestResult const res = osc::DrawTextureAsImGuiImageAndHittest(
            d.m_Renderer.updRenderTexture(),
            d.m_Renderer.getDimensions()
        );

        // if the user's interacting with this particular panel, update the camera
        if (res.isHovered)
        {
            osc::UpdatePolarCameraFromImGuiUserInput(availableSpace, d.m_Camera);
        }

        // if the user left-clicks on a location in the 3D scene, interpret it as "add a landmark here"
        if ((d.m_Flags & PanelFlags_HandleLandmarks) && res.isLeftClickReleasedWithoutDragging)
        {
            // perform raycasted collision test to figure out where the user clicked in 3D space
            osc::Rect const renderRect = res.rect;
            glm::vec2 const mousePos = ImGui::GetMousePos();
            osc::Line const ray = d.m_Camera.unprojectTopLeftPosToWorldRay(mousePos - renderRect.p1, osc::Dimensions(renderRect));
            osc::RayCollision const maybeCollision = osc::GetClosestWorldspaceRayCollision(d.m_Mesh, osc::Transform{}, ray);

            // if the ray intersects the mesh, then that's where the landmark should be placed
            if (maybeCollision)
            {
                d.m_Landmarks[std::to_string(d.m_Landmarks.size())] = ray.origin + ray.dir*maybeCollision.distance;
            }
        }
    }

    osc::Mesh CreateTransformedMesh(PanelData const& src, PanelData const& dest)
    {
        // match up landmarks in the source and destination by name
        std::vector<LandmarkPair3D> landmarks;
        for (auto const& [srcLandmarkName, srcLandmarkPos] : src.m_Landmarks)
        {
            auto it = dest.m_Landmarks.find(srcLandmarkName);
            if (it != dest.m_Landmarks.end())
            {
                landmarks.push_back(LandmarkPair3D{srcLandmarkPos, it->second});
            }
        }

        // create a warper for those pairs (i.e. solve the TPS linear equation)
        ThinPlateWarper3D warper{landmarks};

        // warp the source mesh with the warper to create the output mesh
        return ApplyThinPlateWarpToMesh(warper, src.m_Mesh);
    }
}

class osc::TPS3DTab::Impl final {
public:

    Impl(TabHost* parent) : m_Parent{std::move(parent)}
    {
        m_TransformedData.m_Flags &= ~PanelFlags_HandleLandmarks;
    }

    UID getID() const
    {
        return m_ID;
    }

    CStringView getName() const
    {
        return m_Name;
    }

    TabHost* parent()
    {
        return m_Parent;
    }

    void onMount()
    {

    }

    void onUnmount()
    {

    }

    bool onEvent(SDL_Event const&)
    {
        return false;
    }

    void onTick()
    {

    }

    void onDrawMainMenu()
    {

    }

    void onDraw()
    {
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);


        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::Begin("Source Mesh");
        ImGui::PopStyleVar();
        {
            DrawPanelDataInContentRegionAvail(m_SourceData);
        }
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::Begin("Destination Mesh");
        ImGui::PopStyleVar();
        {
            DrawPanelDataInContentRegionAvail(m_DestData);
        }
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::Begin("Result");
        ImGui::PopStyleVar();
        {
            m_TransformedData.m_Mesh = CreateTransformedMesh(m_SourceData, m_DestData);
            DrawPanelDataInContentRegionAvail(m_TransformedData);
        }
        ImGui::End();

        // draw log panel (debugging)
        m_LogViewerPanel.draw();
    }

private:

    // tab data
    UID m_ID;
    std::string m_Name = ICON_FA_BEZIER_CURVE " TPS3DTab";
    TabHost* m_Parent;

    // scene data
    PanelData m_SourceData{osc::GenUntexturedUVSphere(16, 16)};
    PanelData m_DestData{osc::GenUntexturedSimbodyCylinder(16)};
    PanelData m_TransformedData{CreateTransformedMesh(m_SourceData, m_DestData)};

    // log panel (handy for debugging)
    LogViewerPanel m_LogViewerPanel{"Log"};
};


// public API (PIMPL)

osc::TPS3DTab::TPS3DTab(TabHost* parent) :
    m_Impl{new Impl{std::move(parent)}}
{
}

osc::TPS3DTab::TPS3DTab(TPS3DTab&& tmp) noexcept :
    m_Impl{std::exchange(tmp.m_Impl, nullptr)}
{
}

osc::TPS3DTab& osc::TPS3DTab::operator=(TPS3DTab&& tmp) noexcept
{
    std::swap(m_Impl, tmp.m_Impl);
    return *this;
}

osc::TPS3DTab::~TPS3DTab() noexcept
{
    delete m_Impl;
}

osc::UID osc::TPS3DTab::implGetID() const
{
    return m_Impl->getID();
}

osc::CStringView osc::TPS3DTab::implGetName() const
{
    return m_Impl->getName();
}

osc::TabHost* osc::TPS3DTab::implParent() const
{
    return m_Impl->parent();
}

void osc::TPS3DTab::implOnMount()
{
    m_Impl->onMount();
}

void osc::TPS3DTab::implOnUnmount()
{
    m_Impl->onUnmount();
}

bool osc::TPS3DTab::implOnEvent(SDL_Event const& e)
{
    return m_Impl->onEvent(e);
}

void osc::TPS3DTab::implOnTick()
{
    m_Impl->onTick();
}

void osc::TPS3DTab::implOnDrawMainMenu()
{
    m_Impl->onDrawMainMenu();
}

void osc::TPS3DTab::implOnDraw()
{
    m_Impl->onDraw();
}
