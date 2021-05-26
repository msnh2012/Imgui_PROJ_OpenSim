#include "model_actions.hpp"

#include "src/application.hpp"
#include "src/log.hpp"
#include "src/opensim_bindings/type_registry.hpp"
#include "src/screens/show_model_screen.hpp"
#include "src/ui/add_body_popup.hpp"
#include "src/ui/select_2_pfs_popup.hpp"

#include <OpenSim/Simulation/Model/ContactGeometry.h>
#include <OpenSim/Simulation/Model/Force.h>
#include <OpenSim/Simulation/Model/Model.h>
#include <OpenSim/Simulation/SimbodyEngine/Body.h>
#include <OpenSim/Simulation/SimbodyEngine/Constraint.h>
#include <OpenSim/Simulation/SimbodyEngine/Joint.h>
#include <imgui.h>

using namespace osc;

osc::ui::model_actions::State::State() : abm{}, select_2_pfs{} {
}

static void draw_tooltip(char const* header, char const* description) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(header);
    ImGui::Dummy(ImVec2{0.0f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.7f, 0.7f, 1.0f});
    ImGui::TextUnformatted(description);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

static void render_actions_panel_content(
    osc::ui::model_actions::State& st,
    OpenSim::Model& model,
    std::function<void(OpenSim::Component*)> const& on_set_selection,
    std::function<void()> const& on_before_modify_model,
    std::function<void()> const& on_after_modify_model) {

    // draw add body button
    {
        static constexpr char const* add_body_modal_name = "add body";

        if (ImGui::MenuItem("add body")) {
            ImGui::OpenPopup(add_body_modal_name);
        }

        // tooltip
        if (ImGui::IsItemHovered()) {
            draw_tooltip(
                "Add an OpenSim::Body to the model",
                "An OpenSim::Body is a PhysicalFrame (reference frame) with associated inertia specified by its mass, center-of-mass located in the PhysicalFrame, and its moment of inertia tensor about the center-of-mass");
        }

        if (auto maybe_new_body = ui::add_body_popup::draw(st.abm, add_body_modal_name, model); maybe_new_body) {
            on_before_modify_model();
            model.addJoint(maybe_new_body->joint.release());
            auto* ptr = maybe_new_body->body.get();
            model.addBody(maybe_new_body->body.release());
            on_set_selection(const_cast<OpenSim::Body*>(ptr));
            on_after_modify_model();
        }
    }

    // draw add joint dropdown
    {
        int joint_idx = -1;
        if (ImGui::BeginMenu("add joint")) {
            auto names = joint::names();
            for (size_t i = 0; i < names.size(); ++i) {
                if (ImGui::MenuItem(names[i])) {
                    joint_idx = static_cast<int>(i);
                }
                if (ImGui::IsItemHovered()) {
                    draw_tooltip(names[i], joint::descriptions()[i]);
                }
            }
            ImGui::EndMenu();
        }

        static constexpr char const* modal_name = "select joint pfs";
        if (joint_idx != -1) {
            st.joint_idx_for_pfs_popup = joint_idx;
            ImGui::OpenPopup(modal_name);
        }

        if (auto resp = ui::select_2_pfs::draw(st.select_2_pfs, modal_name, model, "parent", "child"); resp) {
            OSC_ASSERT(
                st.joint_idx_for_pfs_popup >= 0 &&
                static_cast<size_t>(st.joint_idx_for_pfs_popup) < joint::prototypes().size());

            OpenSim::Joint const& prototype = *joint::prototypes()[static_cast<size_t>(st.joint_idx_for_pfs_popup)];

            std::unique_ptr<OpenSim::Joint> copy{prototype.clone()};
            copy->connectSocket_parent_frame(resp->first);
            copy->connectSocket_child_frame(resp->second);

            auto ptr = copy.get();
            on_before_modify_model();
            model.addJoint(copy.release());
            on_set_selection(ptr);
            on_after_modify_model();

            st.joint_idx_for_pfs_popup = -1;
        }
    }

    // draw add contact geometry dropdown
    {
        bool open_popup = false;  // has to be outside ImGui::Menu

        if (ImGui::BeginMenu("add contact geometry")) {
            auto names = contact_geom::names();

            for (size_t i = 0; i < names.size(); ++i) {
                if (ImGui::MenuItem(names[i])) {
                    std::unique_ptr<OpenSim::ContactGeometry> copy{contact_geom::prototypes()[i]->clone()};
                    st.add_component_popup = ui::add_component_popup::State{std::move(copy)};
                    st.add_component_popup_name = "Add Contact Geometry";
                    open_popup = true;
                }
                if (ImGui::IsItemHovered()) {
                    draw_tooltip(names[i], contact_geom::descriptions()[i]);
                }
            }

            ImGui::EndMenu();
        }

        if (open_popup) {
            ImGui::OpenPopup(st.add_component_popup_name);
        }
    }

    // draw add constraint dropdown
    {
        bool open_popup = false;  // has to be outside ImGui::Menu

        if (ImGui::BeginMenu("add constraint")) {
            auto names = constraint::names();
            for (size_t i = 0; i < names.size(); ++i) {

                if (ImGui::MenuItem(names[i])) {
                    std::unique_ptr<OpenSim::Constraint> copy{constraint::prototypes()[i]->clone()};
                    st.add_component_popup = ui::add_component_popup::State{std::move(copy)};
                    st.add_component_popup_name = "Add Constraint";
                    open_popup = true;
                }
                if (ImGui::IsItemHovered()) {
                    draw_tooltip(names[i], constraint::descriptions()[i]);
                }
            }

            ImGui::EndMenu();
        }

        if (open_popup) {
            ImGui::OpenPopup(st.add_component_popup_name);
        }
    }

    {
        bool open_popup = false;  // has to be outside ImGui::Menu
        if (ImGui::BeginMenu("add force")) {
            auto names = force::names();
            for (size_t i = 0; i < names.size(); ++i) {

                if (ImGui::MenuItem(names[i])) {
                    std::unique_ptr<OpenSim::Force> copy{force::prototypes()[i]->clone()};
                    st.add_component_popup = ui::add_component_popup::State{std::move(copy)};
                    st.add_component_popup_name = "Add Force";
                    open_popup = true;
                }
                if (ImGui::IsItemHovered()) {
                    draw_tooltip(names[i], force::descriptions()[i]);
                }
            }

            ImGui::EndMenu();
        }

        if (open_popup) {
            ImGui::OpenPopup(st.add_component_popup_name);
        }
    }

    if (st.add_component_popup && st.add_component_popup_name) {
        auto new_component =
            ui::add_component_popup::draw(*st.add_component_popup, st.add_component_popup_name, model);

        if (new_component) {
            auto ptr = new_component.get();

            if (dynamic_cast<OpenSim::Joint*>(new_component.get())) {
                on_before_modify_model();
                model.addJoint(static_cast<OpenSim::Joint*>(new_component.release()));
                on_set_selection(ptr);
                on_after_modify_model();
            } else if (dynamic_cast<OpenSim::Force*>(new_component.get())) {
                on_before_modify_model();
                model.addForce(static_cast<OpenSim::Force*>(new_component.release()));
                on_set_selection(ptr);
                on_after_modify_model();
            } else if (dynamic_cast<OpenSim::Constraint*>(new_component.get())) {
                on_before_modify_model();
                model.addConstraint(static_cast<OpenSim::Constraint*>(new_component.release()));
                on_set_selection(ptr);
                on_after_modify_model();
            } else if (dynamic_cast<OpenSim::ContactGeometry*>(new_component.get())) {
                on_before_modify_model();
                model.addContactGeometry(static_cast<OpenSim::ContactGeometry*>(new_component.release()));
                on_set_selection(ptr);
                on_after_modify_model();
            } else {
                log::error(
                    "don't know how to add a component of type %s to the model",
                    new_component->getConcreteClassName().c_str());
            }
        }
    }
}

void osc::ui::model_actions::draw(
    State& st,
    OpenSim::Model& model,
    std::function<void(OpenSim::Component*)> const& on_set_selection,
    std::function<void()> const& on_before_modify_model,
    std::function<void()> const& on_after_modify_model) {

    if (ImGui::BeginMenuBar()) {
        render_actions_panel_content(st, model, on_set_selection, on_before_modify_model, on_after_modify_model);
        ImGui::EndMenuBar();
    }
}
