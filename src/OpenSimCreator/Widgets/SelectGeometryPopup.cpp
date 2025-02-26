#include "SelectGeometryPopup.hpp"

#include "OpenSimCreator/Graphics/SimTKMeshLoader.hpp"

#include <oscar/Bindings/ImGuiHelpers.hpp>
#include <oscar/Platform/os.hpp>
#include <oscar/Utils/Algorithms.hpp>
#include <oscar/Utils/FilesystemHelpers.hpp>
#include <oscar/Widgets/StandardPopup.hpp>

#include <imgui.h>
#include <OpenSim/Simulation/Model/Geometry.h>
#include <SimTKcommon/SmallMatrix.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <optional>
#include <vector>
#include <utility>

// maximum length of the search string
static inline int constexpr c_SearchMaxLen = 128;

namespace
{
    using Geom_ctor_fn = std::unique_ptr<OpenSim::Geometry>(*)(void);
    constexpr auto c_GeomCtors = osc::MakeArray<Geom_ctor_fn>
    (
        []()
        {
            auto ptr = std::make_unique<OpenSim::Brick>();
            ptr->set_half_lengths(SimTK::Vec3{0.1, 0.1, 0.1});
            return std::unique_ptr<OpenSim::Geometry>(std::move(ptr));
        },
        []()
        {
            auto ptr = std::make_unique<OpenSim::Sphere>();
            ptr->set_radius(0.1);
            return std::unique_ptr<OpenSim::Geometry>{std::move(ptr)};
        },
        []()
        {
            auto ptr = std::make_unique<OpenSim::Cylinder>();
            ptr->set_radius(0.1);
            ptr->set_half_height(0.1);
            return std::unique_ptr<OpenSim::Geometry>{std::move(ptr)};
        },
        []()
        {
            return std::unique_ptr<OpenSim::Geometry>{new OpenSim::LineGeometry{}};
        },
        []()
        {
            return std::unique_ptr<OpenSim::Geometry>{new OpenSim::Ellipsoid{}};
        },
        []()
        {
            return std::unique_ptr<OpenSim::Geometry>{new OpenSim::Arrow{}};
        },
        []()
        {
            return std::unique_ptr<OpenSim::Geometry>{new OpenSim::Cone{}};
        }
    );

    constexpr auto c_GeomNames = osc::MakeArray<char const*>
    (
        "Brick",
        "Sphere",
        "Cylinder",
        "LineGeometry",
        "Ellipsoid",
        "Arrow (CARE: may not work in OpenSim's main UI)",
        "Cone"
    );

    static_assert(c_GeomCtors.size() == c_GeomNames.size());

    std::optional<std::filesystem::path> PromptUserForGeometryFile()
    {
        return osc::PromptUserForFile(osc::GetCommaDelimitedListOfSupportedSimTKMeshFormats());
    }

    std::unique_ptr<OpenSim::Mesh> LoadGeometryFile(std::filesystem::path const& p)
    {
        return std::make_unique<OpenSim::Mesh>(p.string());
    }
}

class osc::SelectGeometryPopup::Impl final : public osc::StandardPopup {
public:
    Impl(std::string_view popupName,
         std::filesystem::path const& geometryDir,
         std::function<void(std::unique_ptr<OpenSim::Geometry>)> onSelection) :

        StandardPopup{std::move(popupName)},
        m_OnSelection{std::move(onSelection)},
        m_GeometryFiles{GetAllFilesInDirRecursively(geometryDir)}
    {
    }

private:

    void implDrawContent() final
    {
        // premade geometry section
        //
        // let user select from a shorter sequence of analytical geometry that can be
        // generated without a mesh file
        {
            ImGui::TextUnformatted("Generated geometry");
            ImGui::SameLine();
            DrawHelpMarker("This is geometry that OpenSim can generate without needing an external mesh file. Useful for basic geometry.");
            ImGui::Separator();
            ImGui::Dummy({0.0f, 2.0f});

            int item = -1;
            if (ImGui::Combo("##premade", &item, c_GeomNames.data(), static_cast<int>(c_GeomNames.size())))
            {
                auto const& ctor = c_GeomCtors.at(static_cast<size_t>(item));
                m_Result = ctor();
            }
        }

        // mesh file selection
        //
        // let the user select a mesh file that the implementation should load + use
        ImGui::Dummy({0.0f, 3.0f});
        ImGui::TextUnformatted("mesh file");
        ImGui::SameLine();
        DrawHelpMarker("This is geometry that OpenSim loads from external mesh files. Useful for custom geometry (usually, created in some other application, such as ParaView or Blender)");
        ImGui::Separator();
        ImGui::Dummy({0.0f, 2.0f});

        // let the user search through mesh files in pre-established Geometry/ dirs
        osc::InputString("search", m_Search, c_SearchMaxLen);
        ImGui::Dummy({0.0f, 1.0f});

        ImGui::BeginChild(
            "mesh list",
            ImVec2(ImGui::GetContentRegionAvail().x, 256),
            false,
            ImGuiWindowFlags_HorizontalScrollbar);

        if (!m_RecentUserChoices.empty())
        {
            ImGui::TextDisabled("  (recent)");
        }

        for (std::filesystem::path const& p : m_RecentUserChoices)
        {
            auto resp = tryDrawFileChoice(p);
            if (resp)
            {
                m_Result = std::move(resp);
            }
        }

        if (!m_RecentUserChoices.empty())
        {
            ImGui::TextDisabled("  (from Geometry/ dir)");
        }
        for (std::filesystem::path const& p : m_GeometryFiles)
        {
            auto resp = tryDrawFileChoice(p);
            if (resp)
            {
                m_Result = std::move(resp);
            }
        }

        ImGui::EndChild();

        if (ImGui::Button("Open Mesh File"))
        {
            if (auto maybeMeshFile = PromptUserForGeometryFile())
            {
                m_Result = onMeshFileChosen(std::move(maybeMeshFile).value());
            }
        }
        osc::DrawTooltipIfItemHovered("Open Mesh File", "Open a mesh file on the filesystem");

        ImGui::Dummy({0.0f, 5.0f});

        if (ImGui::Button("Cancel"))
        {
            m_Search.clear();
            requestClose();
        }

        if (m_Result)
        {
            m_OnSelection(std::move(m_Result));
            m_Search.clear();
            requestClose();
        }
    }

    std::unique_ptr<OpenSim::Mesh> onMeshFileChosen(std::filesystem::path path)
    {
        auto rv = LoadGeometryFile(path);

        // add to recent list
        m_RecentUserChoices.push_back(std::move(path));

        // reset search (for next popup open)
        m_Search.clear();

        requestClose();

        return rv;
    }

    std::unique_ptr<OpenSim::Mesh> tryDrawFileChoice(std::filesystem::path const& p)
    {
        if (p.filename().string().find(m_Search) != std::string::npos)
        {
            if (ImGui::Selectable(p.filename().string().c_str()))
            {
                return onMeshFileChosen(p.filename());
            }
        }
        return nullptr;
    }

    // holding space for result
    std::unique_ptr<OpenSim::Geometry> m_Result;

    // callback that's called with the geometry
    std::function<void(std::unique_ptr<OpenSim::Geometry>)> m_OnSelection;

    // geometry files found in the user's/installation's `Geometry/` dir
    std::vector<std::filesystem::path> m_GeometryFiles;

    // recent file choices by the user
    std::vector<std::filesystem::path> m_RecentUserChoices;

    // the user's current search filter
    std::string m_Search;
};


// public API (PIMPL)

osc::SelectGeometryPopup::SelectGeometryPopup(
    std::string_view popupName,
    std::filesystem::path const& geometryDir,
    std::function<void(std::unique_ptr<OpenSim::Geometry>)> onSelection) :

    m_Impl{std::make_unique<Impl>(std::move(popupName), geometryDir, std::move(onSelection))}
{
}

osc::SelectGeometryPopup::SelectGeometryPopup(SelectGeometryPopup&&) noexcept = default;
osc::SelectGeometryPopup& osc::SelectGeometryPopup::operator=(SelectGeometryPopup&&) noexcept = default;
osc::SelectGeometryPopup::~SelectGeometryPopup() noexcept = default;

bool osc::SelectGeometryPopup::implIsOpen() const
{
    return m_Impl->isOpen();
}

void osc::SelectGeometryPopup::implOpen()
{
    m_Impl->open();
}

void osc::SelectGeometryPopup::implClose()
{
    m_Impl->close();
}

bool osc::SelectGeometryPopup::implBeginPopup()
{
    return m_Impl->beginPopup();
}

void osc::SelectGeometryPopup::implDrawPopupContent()
{
    m_Impl->drawPopupContent();
}

void osc::SelectGeometryPopup::implEndPopup()
{
    m_Impl->endPopup();
}
