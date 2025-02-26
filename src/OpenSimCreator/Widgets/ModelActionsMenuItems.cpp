#include "ModelActionsMenuItems.hpp"

#include "OpenSimCreator/MiddlewareAPIs/EditorAPI.hpp"
#include "OpenSimCreator/Widgets/AddBodyPopup.hpp"
#include "OpenSimCreator/Widgets/AddComponentPopup.hpp"
#include "OpenSimCreator/TypeRegistry.hpp"

#include <oscar/Bindings/ImGuiHelpers.hpp>
#include <oscar/Utils/CStringView.hpp>

#include <imgui.h>
#include <OpenSim/Common/Component.h>
#include <OpenSim/Simulation/Control/Controller.h>
#include <OpenSim/Simulation/Model/Actuator.h>
#include <OpenSim/Simulation/Model/ContactGeometry.h>
#include <OpenSim/Simulation/Model/Force.h>
#include <OpenSim/Simulation/Model/Probe.h>
#include <OpenSim/Simulation/SimbodyEngine/Constraint.h>
#include <OpenSim/Simulation/SimbodyEngine/Joint.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>

class osc::ModelActionsMenuItems::Impl final {
public:

    Impl(
        EditorAPI* api,
        std::shared_ptr<UndoableModelStatePair> uum_) :

        m_EditorAPI{std::move(api)},
        m_Uum{std::move(uum_)}
    {
    }

    void draw()
    {
        ImGui::PushID(this);

        // action: add body
        {
            // draw button
            if (ImGui::MenuItem("Body"))
            {
                auto popup = std::make_unique<AddBodyPopup>(m_EditorAPI, m_Uum, "add body");
                popup->open();
                m_EditorAPI->pushPopup(std::move(popup));
            }

            // draw tooltip (if hovered)
            if (ImGui::IsItemHovered())
            {
                DrawTooltip(
                    "Add an OpenSim::Body into the model",
                    "An OpenSim::Body is a PhysicalFrame (reference frame) with an associated inertia specified by its mass, center-of-mass located in the PhysicalFrame, and its moment of inertia tensor about the center-of-mass");
            }
        }

        renderButton<OpenSim::Joint>();
        renderButton<OpenSim::ContactGeometry>();
        renderButton<OpenSim::Constraint>();
        renderButton<OpenSim::Force>();
        renderButton<OpenSim::Controller>();
        renderButton<OpenSim::Probe>();
        renderButton<OpenSim::Component>();

        ImGui::PopID();
    }

private:

    template<typename T>
    void renderButton()
    {
        std::stringstream label;
        label << osc::TypeRegistry<T>::name();

        // action: add joint
        if (ImGui::BeginMenu(label.str().c_str()))
        {
            auto names = osc::TypeRegistry<T>::nameCStrings();

            for (size_t i = 0; i < names.size(); ++i)
            {
                if (ImGui::MenuItem(names[i]))
                {
                    std::unique_ptr<T> copy{osc::TypeRegistry<T>::prototypes()[i]->clone()};
                    auto popup = std::make_unique<AddComponentPopup>(m_EditorAPI, m_Uum, std::move(copy), "Add " + osc::TypeRegistry<T>::name());
                    popup->open();
                    m_EditorAPI->pushPopup(std::move(popup));
                }

                if (ImGui::IsItemHovered())
                {
                    DrawTooltip(names[i], osc::TypeRegistry<T>::descriptionCStrings()[i]);
                }
            }

            ImGui::EndMenu();
        }
        if (ImGui::IsItemHovered())
        {
            std::stringstream ttTitle;
            ttTitle << "Add a " << osc::TypeRegistry<T>::name() << " into the model";
            DrawTooltip(
                ttTitle.str().c_str(),
                osc::TypeRegistry<T>::description().c_str());
        }
    }

    EditorAPI* m_EditorAPI;
    std::shared_ptr<UndoableModelStatePair> m_Uum;
};


// public API (PIMPL)

osc::ModelActionsMenuItems::ModelActionsMenuItems(EditorAPI* api, std::shared_ptr<UndoableModelStatePair> m) :
    m_Impl{std::make_unique<Impl>(std::move(api), std::move(m))}
{
}

osc::ModelActionsMenuItems::ModelActionsMenuItems(ModelActionsMenuItems&&) noexcept = default;
osc::ModelActionsMenuItems& osc::ModelActionsMenuItems::operator=(ModelActionsMenuItems&&) noexcept = default;
osc::ModelActionsMenuItems::~ModelActionsMenuItems() noexcept = default;

void osc::ModelActionsMenuItems::draw()
{
    m_Impl->draw();
}
