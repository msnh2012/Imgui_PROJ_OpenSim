#include "ModelEditorToolbar.hpp"

#include "OpenSimCreator/MiddlewareAPIs/EditorAPI.hpp"
#include "OpenSimCreator/MiddlewareAPIs/MainUIStateAPI.hpp"
#include "OpenSimCreator/Widgets/ParamBlockEditorPopup.hpp"
#include "OpenSimCreator/ActionFunctions.hpp"
#include "OpenSimCreator/OpenSimHelpers.hpp"
#include "OpenSimCreator/UndoableModelStatePair.hpp"

#include <oscar/Bindings/ImGuiHelpers.hpp>
#include <oscar/Graphics/Icon.hpp>
#include <oscar/Graphics/IconCache.hpp>
#include <oscar/Graphics/MeshCache.hpp>
#include <oscar/Platform/App.hpp>
#include <oscar/Platform/RecentFile.hpp>
#include <oscar/Platform/Styling.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome5.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class osc::ModelEditorToolbar::Impl final {
public:
    Impl(
        std::string_view label_,
        std::weak_ptr<MainUIStateAPI> mainUIStateAPI_,
        EditorAPI* editorAPI_,
        std::shared_ptr<osc::UndoableModelStatePair> model_) :

        m_Label{std::move(label_)},
        m_MainUIStateAPI{std::move(mainUIStateAPI_)},
        m_EditorAPI{editorAPI_},
        m_Model{std::move(model_)}
    {
    }

    void draw()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5.0f, 5.0f});
        float const height = ImGui::GetFrameHeight() + 2.0f*ImGui::GetStyle().WindowPadding.y;
        ImGuiWindowFlags const flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        if (osc::BeginMainViewportTopBar(m_Label, height, flags))
        {
            drawContent();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
private:
    void drawNewModelButton()
    {
        if (ImGui::Button(ICON_FA_FILE))
        {
            ActionNewModel(m_MainUIStateAPI);
        }
        osc::DrawTooltipIfItemHovered("New Model", "Creates a new OpenSim model in a new tab");
    }

    void drawOpenButton()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.0f, 0.0f});
        if (ImGui::Button(ICON_FA_FOLDER_OPEN))
        {
            ActionOpenModel(m_MainUIStateAPI);
        }
        osc::DrawTooltipIfItemHovered("Open Model", "Opens an existing osim file in a new tab");
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1.0f, ImGui::GetStyle().FramePadding.y});
        ImGui::Button(ICON_FA_CARET_DOWN);
        osc::DrawTooltipIfItemHovered("Open Recent File", "Opens a recently-opened osim file in a new tab");
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextItem("##RecentFilesMenu", ImGuiPopupFlags_MouseButtonLeft))
        {
            std::vector<RecentFile> recentFiles = App::get().getRecentFiles();
            std::reverse(recentFiles.begin(), recentFiles.end());  // sort newest -> oldest
            int imguiID = 0;

            for (RecentFile const& rf : recentFiles)
            {
                ImGui::PushID(imguiID++);
                if (ImGui::Selectable(rf.path.filename().string().c_str()))
                {
                    ActionOpenModel(m_MainUIStateAPI, rf.path);
                }
                ImGui::PopID();
            }

            ImGui::EndPopup();
        }
    }

    void drawSaveButton()
    {
        if (ImGui::Button(ICON_FA_SAVE))
        {
            ActionSaveModel(*m_MainUIStateAPI.lock(), *m_Model);
        }
        osc::DrawTooltipIfItemHovered("Save Model", "Saves the model to an osim file");
    }

    void drawReloadButton()
    {
        if (!HasInputFileName(m_Model->getModel()))
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f * ImGui::GetStyle().Alpha);
        }

        if (ImGui::Button(ICON_FA_RECYCLE))
        {
            ActionReloadOsimFromDisk(*m_Model, *App::upd().singleton<MeshCache>());
        }

        if (!HasInputFileName(m_Model->getModel()))
        {
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }

        osc::DrawTooltipIfItemHovered("Reload Model", "Reloads the model from its source osim file");
    }

    void drawUndoButton()
    {
        int itemFlagsPushed = 0;
        int styleVarsPushed = 0;
        if (!m_Model->canUndo())
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ++itemFlagsPushed;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f * ImGui::GetStyle().Alpha);
            ++styleVarsPushed;
        }

        if (ImGui::Button(ICON_FA_UNDO))
        {
            ActionUndoCurrentlyEditedModel(*m_Model);
        }

        osc::PopItemFlags(itemFlagsPushed);
        itemFlagsPushed = 0;
        ImGui::PopStyleVar(styleVarsPushed);
        styleVarsPushed = 0;

        osc::DrawTooltipIfItemHovered("Undo", "Undo the model to an earlier version");
    }

    void drawRedoButton()
    {
        int itemFlagsPushed = 0;
        int styleVarsPushed = 0;
        if (!m_Model->canRedo())
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ++itemFlagsPushed;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f * ImGui::GetStyle().Alpha);
            ++styleVarsPushed;
        }

        if (ImGui::Button(ICON_FA_REDO))
        {
            ActionRedoCurrentlyEditedModel(*m_Model);
        }

        osc::PopItemFlags(itemFlagsPushed);
        itemFlagsPushed = 0;
        ImGui::PopStyleVar(styleVarsPushed);
        styleVarsPushed = 0;

        osc::DrawTooltipIfItemHovered("Redo", "Redo the model to an undone version");
    }

    void drawToggleFramesButton()
    {
        Icon const icon = m_IconCache->getIcon(IsShowingFrames(m_Model->getModel()) ? "frame_colored" : "frame_bw");
        if (osc::ImageButton("##toggleframes", icon.getTexture(), icon.getDimensions()))
        {
            ActionToggleFrames(*m_Model);
        }
        osc::DrawTooltipIfItemHovered("Toggle Rendering Frames", "Toggles whether frames (coordinate systems) within the model should be rendered in the 3D scene.");
    }

    void drawToggleMarkersButton()
    {
        Icon const icon = m_IconCache->getIcon(IsShowingMarkers(m_Model->getModel()) ? "marker_colored" : "marker");
        if (osc::ImageButton("##togglemarkers", icon.getTexture(), icon.getDimensions()))
        {
            ActionToggleMarkers(*m_Model);
        }
        osc::DrawTooltipIfItemHovered("Toggle Rendering Markers", "Toggles whether markers should be rendered in the 3D scene");
    }

    void drawToggleWrapGeometryButton()
    {
        Icon const icon = m_IconCache->getIcon(IsShowingWrapGeometry(m_Model->getModel()) ? "wrap_colored" : "wrap");
        if (osc::ImageButton("##togglewrapgeom", icon.getTexture(), icon.getDimensions()))
        {
            ActionToggleWrapGeometry(*m_Model);
        }
        osc::DrawTooltipIfItemHovered("Toggle Rendering Wrap Geometry", "Toggles whether wrap geometry should be rendered in the 3D scene.\n\nNOTE: This is a model-level property. Individual wrap geometries *within* the model may have their visibility set to 'false', which will cause them to be hidden from the visualizer, even if this is enabled.");
    }

    void drawToggleContactGeometryButton()
    {
        Icon const icon = m_IconCache->getIcon(IsShowingContactGeometry(m_Model->getModel()) ? "contact_colored" : "contact");
        if (osc::ImageButton("##togglecontactgeom", icon.getTexture(), icon.getDimensions()))
        {
            ActionToggleContactGeometry(*m_Model);
        }
        osc::DrawTooltipIfItemHovered("Toggle Rendering Contact Geometry", "Toggles whether contact geometry should be rendered in the 3D scene");
    }

    void drawFileRelatedActionsGroup()
    {
        drawNewModelButton();
        ImGui::SameLine();

        drawOpenButton();
        ImGui::SameLine();

        drawSaveButton();
        ImGui::SameLine();

        drawReloadButton();
    }

    void drawUndoRedoGroup()
    {
        drawUndoButton();
        ImGui::SameLine();

        drawRedoButton();
    }

    void drawScaleFactorGroup()
    {
        // TODO: tooltip on hover
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
        ImGui::TextUnformatted(ICON_FA_EXPAND_ALT);
        osc::DrawTooltipIfItemHovered("Scene Scale Factor", "Rescales decorations in the model by this amount. Changing this can be handy when working on extremely small/large models.");
        ImGui::SameLine();

        {
            float scaleFactor = m_Model->getFixupScaleFactor();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("0.00000").x);
            if (ImGui::InputFloat("##scaleinput", &scaleFactor))
            {
                osc::ActionSetModelSceneScaleFactorTo(*m_Model, scaleFactor);
            }
        }
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.0f, 0.0f});
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_EXPAND_ARROWS_ALT))
        {
            osc::ActionAutoscaleSceneScaleFactor(*m_Model);
        }
        ImGui::PopStyleVar();
        osc::DrawTooltipIfItemHovered("Autoscale Scale Factor", "Try to autoscale the model's scale factor based on the current dimensions of the model");
    }

    void drawSimulationGroup()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.0f, 0.0f});

        ImGui::PushStyleColor(ImGuiCol_Text, OSC_POSITIVE_RGBA);
        if (ImGui::Button(ICON_FA_PLAY))
        {
            osc::ActionStartSimulatingModel(m_MainUIStateAPI, *m_Model);
        }
        ImGui::PopStyleColor();
        App::upd().addFrameAnnotation("Simulate Button", osc::GetItemRect());
        osc::DrawTooltipIfItemHovered("Simulate Model", "Run a forward-dynamic simulation of the model");

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_EDIT))
        {
            m_EditorAPI->pushPopup(std::make_unique<ParamBlockEditorPopup>("simulation parameters", &m_MainUIStateAPI.lock()->updSimulationParams()));
        }
        osc::DrawTooltipIfItemHovered("Edit Simulation Settings", "Change the parameters used when simulating the model");

        ImGui::PopStyleVar();
    }

    void drawDecorationsGroup()
    {
        drawToggleFramesButton();
        ImGui::SameLine();

        drawToggleMarkersButton();
        ImGui::SameLine();

        drawToggleWrapGeometryButton();
        ImGui::SameLine();

        drawToggleContactGeometryButton();
    }

    void drawContent()
    {
        drawFileRelatedActionsGroup();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        drawUndoRedoGroup();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        drawScaleFactorGroup();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        drawSimulationGroup();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        drawDecorationsGroup();
    }

    std::string m_Label;
    std::weak_ptr<MainUIStateAPI> m_MainUIStateAPI;
    EditorAPI* m_EditorAPI;
    std::shared_ptr<osc::UndoableModelStatePair> m_Model;

    std::shared_ptr<IconCache> m_IconCache = App::singleton<osc::IconCache>(
        App::resource("icons/"),
        ImGui::GetTextLineHeight()/128.0f
    );
};

osc::ModelEditorToolbar::ModelEditorToolbar(
    std::string_view label_,
    std::weak_ptr<MainUIStateAPI> mainUIStateAPI_,
    EditorAPI* editorAPI_,
    std::shared_ptr<UndoableModelStatePair> model_) :

    m_Impl{std::make_unique<Impl>(std::move(label_), std::move(mainUIStateAPI_), editorAPI_, std::move(model_))}
{
}
osc::ModelEditorToolbar::ModelEditorToolbar(ModelEditorToolbar&&) noexcept = default;
osc::ModelEditorToolbar& osc::ModelEditorToolbar::operator=(ModelEditorToolbar&&) noexcept = default;
osc::ModelEditorToolbar::~ModelEditorToolbar() noexcept = default;

void osc::ModelEditorToolbar::draw()
{
    m_Impl->draw();
}