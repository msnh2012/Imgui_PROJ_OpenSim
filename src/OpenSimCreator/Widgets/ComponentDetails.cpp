#include "ComponentDetails.hpp"

#include "OpenSimCreator/OpenSimHelpers.hpp"

#include <imgui.h>
#include <OpenSim/Common/AbstractProperty.h>
#include <OpenSim/Common/Array.h>
#include <OpenSim/Common/Component.h>
#include <OpenSim/Common/ComponentPath.h>
#include <OpenSim/Common/ComponentSocket.h>

#include <string>
#include <vector>

osc::ComponentDetails::Response osc::ComponentDetails::draw(SimTK::State const& state,
                                                            OpenSim::Component const* comp)
{
    Response rv;

    if (!comp)
    {
        ImGui::Text("(nothing selected)");
        return rv;
    }

    OpenSim::Component const& c = *comp;

    ImGui::Text("selection information:");
    ImGui::Dummy({0.0, 2.5f});
    ImGui::Separator();

    // top-level info
    {
        ImGui::Columns(2);

        ImGui::Text("getName()");
        ImGui::NextColumn();
        ImGui::Text("%s", c.getName().c_str());
        ImGui::NextColumn();

        ImGui::Text("getAuthors()");
        ImGui::NextColumn();
        ImGui::Text("%s", c.getAuthors().c_str());
        ImGui::NextColumn();

        ImGui::Text("getOwner().getName()");
        ImGui::NextColumn();
        ImGui::Text("%s", c.hasOwner() ? c.getOwner().getName().c_str() : "N/A (no owner)");
        ImGui::NextColumn();

        ImGui::Text("getAbsolutePath()");
        ImGui::NextColumn();
        ImGui::Text("%s", osc::GetAbsolutePathString(c).c_str());
        ImGui::NextColumn();

        ImGui::Text("getConcreteClassName()");
        ImGui::NextColumn();
        ImGui::Text("%s", c.getConcreteClassName().c_str());
        ImGui::NextColumn();

        ImGui::Text("getNumInputs()");
        ImGui::NextColumn();
        ImGui::Text("%i", c.getNumInputs());
        ImGui::NextColumn();

        ImGui::Text("getNumOutputs()");
        ImGui::NextColumn();
        ImGui::Text("%i", c.getNumOutputs());
        ImGui::NextColumn();

        ImGui::Text("getNumSockets()");
        ImGui::NextColumn();
        ImGui::Text("%i", c.getNumSockets());
        ImGui::NextColumn();

        ImGui::Text("getNumStateVariables()");
        ImGui::NextColumn();
        ImGui::Text("%i", c.getNumStateVariables());
        ImGui::NextColumn();

        ImGui::Text("getNumProperties()");
        ImGui::NextColumn();
        ImGui::Text("%i", c.getNumProperties());
        ImGui::NextColumn();

        ImGui::Columns();
    }

    // properties
    if (ImGui::CollapsingHeader("properties"))
    {
        ImGui::Columns(2);
        for (int i = 0; i < c.getNumProperties(); ++i)
        {
            OpenSim::AbstractProperty const& p = c.getPropertyByIndex(i);
            ImGui::Text("%s", p.getName().c_str());
            ImGui::NextColumn();
            ImGui::Text("%s", p.toString().c_str());
            ImGui::NextColumn();
        }
        ImGui::Columns();
    }

    // state variables
    if (ImGui::CollapsingHeader("state variables"))
    {
        OpenSim::Array<std::string> names = c.getStateVariableNames();
        ImGui::Columns(2);
        for (int i = 0; i < names.size(); ++i)
        {
            std::string const& name = names[i];

            ImGui::Text("%s", name.c_str());
            ImGui::NextColumn();
            ImGui::Text("%f", c.getStateVariableValue(state, name));
            ImGui::NextColumn();

            ImGui::Text("%s (deriv)", name.c_str());
            ImGui::NextColumn();
            ImGui::Text("%f", c.getStateVariableDerivativeValue(state, name));
            ImGui::NextColumn();
        }
        ImGui::Columns();
    }

    // inputs
    if (ImGui::CollapsingHeader("inputs"))
    {
        std::vector<std::string> input_names = c.getInputNames();
        for (std::string const& inputName : input_names)
        {
            ImGui::Text("%s", inputName.c_str());
        }
    }

    // sockets
    if (ImGui::CollapsingHeader("sockets"))
    {
        std::vector<std::string> socknames = osc::GetSocketNames(c);
        ImGui::Columns(2);
        for (std::string const& sn : socknames)
        {
            ImGui::Text("%s", sn.c_str());
            ImGui::NextColumn();

            std::string const& cp = c.getSocket(sn).getConnecteePath();
            ImGui::Text("%s", cp.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                rv.type = ResponseType::SelectionChanged;
                rv.ptr = &c.getComponent(cp);
            }
            ImGui::NextColumn();
        }
        ImGui::Columns();
    }

    return rv;
}
