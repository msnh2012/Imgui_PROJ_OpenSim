#include "SimulationViewerPanel.hpp"

#include "OpenSimCreator/Widgets/BasicWidgets.hpp"
#include "OpenSimCreator/Widgets/UiModelViewer.hpp"
#include "OpenSimCreator/OpenSimHelpers.hpp"
#include "OpenSimCreator/VirtualModelStatePair.hpp"

#include <oscar/Panels/StandardPanel.hpp>

#include <OpenSim/Common/Component.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

class osc::SimulationViewerPanel::Impl final : public StandardPanel {
public:
    Impl(
        std::string_view panelName_,
        std::shared_ptr<VirtualModelStatePair> modelState_,
        std::weak_ptr<MainUIStateAPI> mainUIStateAPI_) :

        StandardPanel{std::move(panelName_)},
        m_Model{std::move(modelState_)},
        m_API{mainUIStateAPI_}
    {
    }

private:
    void implBeforeImGuiBegin() final
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    }

    void implAfterImGuiBegin() final
    {
        ImGui::PopStyleVar();
    }

    void implDrawContent() final
    {
        std::optional<SceneCollision> const maybeCollision = m_Viewer.draw(*m_Model);

        OpenSim::Component const* maybeHover = maybeCollision ?
            osc::FindComponent(m_Model->getModel(), maybeCollision->decorationID) :
            nullptr;

        // care: this code must check whether the hover != current hover (even if
        // null), because there might be multiple viewports open (#582)
        if (m_Viewer.isMousedOver() && maybeHover != m_Model->getHovered())
        {
            // hovering: update hover and show tooltip
            m_Model->setHovered(maybeHover);
        }

        if (m_Viewer.isMousedOver() && m_Viewer.isLeftClicked())
        {
            m_Model->setSelected(maybeHover);
        }

        if (maybeHover)
        {
            DrawComponentHoverTooltip(*maybeHover);
        }

        {
            // right-click: draw context menu

            std::string menuName = std::string{getName()} + "_contextmenu";

            if (m_Viewer.isRightClicked() && m_Viewer.isMousedOver())
            {
                m_Model->setSelected(maybeHover);  // can be empty
                ImGui::OpenPopup(menuName.c_str());
            }

            OpenSim::Component const* selected = m_Model->getSelected();

            if (selected && ImGui::BeginPopup(menuName.c_str()))
            {
                // draw context menu for whatever's selected
                ImGui::TextUnformatted(selected->getName().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", selected->getConcreteClassName().c_str());
                ImGui::Separator();
                ImGui::Dummy({0.0f, 3.0f});

                DrawSelectOwnerMenu(*m_Model, *selected);
                DrawWatchOutputMenu(*m_API.lock(), *selected);
                ImGui::EndPopup();
            }
        }
    }

    std::shared_ptr<VirtualModelStatePair> m_Model;
    std::weak_ptr<MainUIStateAPI> m_API;
    UiModelViewer m_Viewer;
};


// public API (PIMPL)

osc::SimulationViewerPanel::SimulationViewerPanel(
    std::string_view panelName,
    std::shared_ptr<VirtualModelStatePair> modelState,
    std::weak_ptr<MainUIStateAPI> mainUIStateAPI) :

    m_Impl{std::make_unique<Impl>(std::move(panelName), std::move(modelState), std::move(mainUIStateAPI))}
{
}
osc::SimulationViewerPanel::SimulationViewerPanel(SimulationViewerPanel&&) noexcept = default;
osc::SimulationViewerPanel& osc::SimulationViewerPanel::operator=(SimulationViewerPanel&&) noexcept = default;
osc::SimulationViewerPanel::~SimulationViewerPanel() noexcept = default;

osc::CStringView osc::SimulationViewerPanel::implGetName() const
{
    return m_Impl->getName();
}

bool osc::SimulationViewerPanel::implIsOpen() const
{
    return m_Impl->isOpen();
}

void osc::SimulationViewerPanel::implOpen()
{
    m_Impl->open();
}

void osc::SimulationViewerPanel::implClose()
{
    m_Impl->close();
}

void osc::SimulationViewerPanel::implDraw()
{
    m_Impl->draw();
}