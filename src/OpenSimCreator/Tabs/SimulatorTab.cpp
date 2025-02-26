#include "SimulatorTab.hpp"

#include "OpenSimCreator/MiddlewareAPIs/MainUIStateAPI.hpp"
#include "OpenSimCreator/MiddlewareAPIs/SimulatorUIAPI.hpp"
#include "OpenSimCreator/Panels/NavigatorPanel.hpp"
#include "OpenSimCreator/Panels/OutputPlotsPanel.hpp"
#include "OpenSimCreator/Panels/SelectionDetailsPanel.hpp"
#include "OpenSimCreator/Panels/SimulationDetailsPanel.hpp"
#include "OpenSimCreator/Panels/SimulationViewerPanel.hpp"
#include "OpenSimCreator/Widgets/BasicWidgets.hpp"
#include "OpenSimCreator/Widgets/MainMenu.hpp"
#include "OpenSimCreator/Widgets/SimulationOutputPlot.hpp"
#include "OpenSimCreator/Widgets/SimulationScrubber.hpp"
#include "OpenSimCreator/Widgets/SimulationToolbar.hpp"
#include "OpenSimCreator/ComponentOutputExtractor.hpp"
#include "OpenSimCreator/OutputExtractor.hpp"
#include "OpenSimCreator/OpenSimHelpers.hpp"
#include "OpenSimCreator/Simulation.hpp"
#include "OpenSimCreator/SimulationClock.hpp"
#include "OpenSimCreator/SimulationModelStatePair.hpp"
#include "OpenSimCreator/SimulationReport.hpp"
#include "OpenSimCreator/VirtualOutputExtractor.hpp"
#include "OpenSimCreator/VirtualSimulation.hpp"

#include <oscar/Bindings/ImGuiHelpers.hpp>
#include <oscar/Panels/LogViewerPanel.hpp>
#include <oscar/Panels/PanelManager.hpp>
#include <oscar/Panels/PerfPanel.hpp>
#include <oscar/Platform/App.hpp>
#include <oscar/Platform/Config.hpp>
#include <oscar/Platform/os.hpp>
#include <oscar/Tabs/TabHost.hpp>
#include <oscar/Utils/SynchronizedValue.hpp>
#include <oscar/Utils/Perf.hpp>
#include <oscar/Widgets/WindowMenu.hpp>

#include <glm/vec2.hpp>
#include <imgui.h>
#include <IconsFontAwesome5.h>
#include <nonstd/span.hpp>
#include <OpenSim/Common/Component.h>
#include <OpenSim/Common/ComponentOutput.h>
#include <OpenSim/Simulation/Model/Model.h>
#include <SDL_events.h>
#include <SimTKcommon/basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static std::atomic<int> g_SimulationNumber = 1;

class osc::SimulatorTab::Impl final : public SimulatorUIAPI {
public:

    Impl(
        std::weak_ptr<MainUIStateAPI> parent_,
        std::shared_ptr<Simulation> simulation_) :

        m_Parent{std::move(parent_)},
        m_Simulation{std::move(simulation_)}
    {
        // register panels

        m_PanelManager->registerToggleablePanel(
            "Performance",
            [](std::string_view panelName)
            {
                return std::make_shared<PerfPanel>(panelName);
            }
        );
        m_PanelManager->registerToggleablePanel(
            "Navigator",
            [this](std::string_view panelName)
            {
                return std::make_shared<NavigatorPanel>(
                    panelName,
                    m_ShownModelState
                );
            }
        );
        m_PanelManager->registerToggleablePanel(
            "Selection Details",
            [this](std::string_view panelName)
            {
                return std::make_shared<SelectionDetailsPanel>(
                    panelName,
                    this
                );
            }
        );
        m_PanelManager->registerToggleablePanel(
            "Output Plots",
            [this](std::string_view panelName)
            {
                return std::make_shared<OutputPlotsPanel>(
                    panelName,
                    m_Parent,
                    this
                );
            }
        );
        m_PanelManager->registerToggleablePanel(
            "Simulation Details",
            [this](std::string_view panelName)
            {
                return std::make_shared<SimulationDetailsPanel>(
                    panelName,
                    this,
                    m_Simulation
                );
            }
        );
        m_PanelManager->registerToggleablePanel(
            "Log",
            [](std::string_view panelName)
            {
                return std::make_shared<LogViewerPanel>(panelName);
            }
        );
        m_PanelManager->registerSpawnablePanel(
            "viewer",
            [this](std::string_view panelName)
            {
                return std::make_shared<SimulationViewerPanel>(
                    panelName,
                    m_ShownModelState,
                    m_Parent
                );
            },
            1  // by default, open one viewer
        );
    }

    UID getID() const
    {
        return m_ID;
    }

    CStringView getName() const
    {
        return m_Name;
    }

    void onMount()
    {
        App::upd().makeMainEventLoopWaiting();
        m_PanelManager->onMount();
    }

    void onUnmount()
    {
        m_PanelManager->onUnmount();
        App::upd().makeMainEventLoopPolling();
    }

    bool onEvent(SDL_Event const& e)
    {
        return false;
    }

    void onTick()
    {
        if (m_IsPlayingBack)
        {
            SimulationClock::time_point const playbackPos = implGetSimulationScrubTime();

            if ((m_PlaybackSpeed >= 0.0f && playbackPos < m_Simulation->getEndTime()) ||
                (m_PlaybackSpeed < 0.0f && playbackPos > m_Simulation->getStartTime()))
            {
                App::upd().requestRedraw();
            }
            else
            {
                m_PlaybackStartSimtime = playbackPos;
                m_IsPlayingBack = false;
            }
        }

        m_PanelManager->onTick();
    }

    void onDrawMainMenu()
    {
        m_MainMenuFileTab.draw(m_Parent);
        m_MainMenuWindowTab.draw();
        m_MainMenuAboutTab.draw();
    }

    void onDraw()
    {
        ImGui::DockSpaceOverViewport(
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode
        );
        drawContent();
    }

private:
    std::optional<SimulationReport> tryFindNthReportAfter(SimulationClock::time_point t, int offset = 0)
    {
        int const numSimulationReports = m_Simulation->getNumReports();

        if (numSimulationReports <= 0)
        {
            return std::nullopt;
        }

        int zeroethReportIndex = numSimulationReports - 1;
        for (int i = 0; i < numSimulationReports; ++i)
        {
            SimulationReport r = m_Simulation->getSimulationReport(i);
            if (r.getTime() >= t)
            {
                zeroethReportIndex = i;
                break;
            }
        }

        int const reportIndex = zeroethReportIndex + offset;
        if (0 <= reportIndex && reportIndex < numSimulationReports)
        {
            return m_Simulation->getSimulationReport(reportIndex);
        }
        else
        {
            return std::nullopt;
        }
    }

    VirtualSimulation& implUpdSimulation() final
    {
        return *m_Simulation;
    }

    bool implGetSimulationPlaybackState() final
    {
        return m_IsPlayingBack;
    }

    void implSetSimulationPlaybackState(bool v) final
    {
        if (v)
        {
            m_PlaybackStartWallTime = std::chrono::system_clock::now();
            m_IsPlayingBack = true;
        }
        else
        {
            m_PlaybackStartSimtime = getSimulationScrubTime();
            m_IsPlayingBack = false;
        }
    }

    float implGetSimulationPlaybackSpeed() final
    {
        return m_PlaybackSpeed;
    }

    void implSetSimulationPlaybackSpeed(float v)
    {
        m_PlaybackSpeed = v;
    }

    SimulationClock::time_point implGetSimulationScrubTime() final
    {
        if (!m_IsPlayingBack)
        {
            return m_PlaybackStartSimtime;
        }
        // map wall time onto sim time

        int nReports = m_Simulation->getNumReports();
        if (nReports <= 0)
        {
            return m_Simulation->getStartTime();
        }
        else
        {
            std::chrono::system_clock::time_point wallNow = std::chrono::system_clock::now();
            std::chrono::system_clock::duration wallDur = wallNow - m_PlaybackStartWallTime;

            SimulationClock::duration const simDur = m_PlaybackSpeed * SimulationClock::duration{wallDur};
            SimulationClock::time_point const simNow = m_PlaybackStartSimtime + simDur;
            SimulationClock::time_point const simEarliest = m_Simulation->getSimulationReport(0).getTime();
            SimulationClock::time_point const simLatest = m_Simulation->getSimulationReport(nReports - 1).getTime();

            if (simNow < simEarliest)
            {
                return simEarliest;
            }
            else if (simNow > simLatest)
            {
                return simLatest;
            }
            else
            {
                return simNow;
            }
        }
    }

    void implSetSimulationScrubTime(SimulationClock::time_point t) final
    {
        m_PlaybackStartSimtime = t;
        m_PlaybackStartWallTime = std::chrono::system_clock::now();
    }

    void implStepBack() final
    {
        std::optional<SimulationReport> const maybePrev = tryFindNthReportAfter(getSimulationScrubTime(), -1);
        if (maybePrev)
        {
            setSimulationScrubTime(maybePrev->getTime());
        }
    }

    void implStepForward() final
    {
        std::optional<SimulationReport> const maybeNext = tryFindNthReportAfter(getSimulationScrubTime(), 1);
        if (maybeNext)
        {
            setSimulationScrubTime(maybeNext->getTime());
        }
    }

    std::optional<SimulationReport> implTrySelectReportBasedOnScrubbing() final
    {
        return tryFindNthReportAfter(getSimulationScrubTime());
    }

    int implGetNumUserOutputExtractors() const final
    {
        return m_Parent.lock()->getNumUserOutputExtractors();
    }

    OutputExtractor const& implGetUserOutputExtractor(int i) const final
    {
        return m_Parent.lock()->getUserOutputExtractor(i);
    }

    void implAddUserOutputExtractor(OutputExtractor const& outputExtractor) final
    {
        m_Parent.lock()->addUserOutputExtractor(outputExtractor);
    }

    void implRemoveUserOutputExtractor(int i) final
    {
        m_Parent.lock()->removeUserOutputExtractor(i);
    }

    bool implHasUserOutputExtractor(OutputExtractor const& oe) const final
    {
        return m_Parent.lock()->hasUserOutputExtractor(oe);
    }

    bool implRemoveUserOutputExtractor(OutputExtractor const& oe) final
    {
        return m_Parent.lock()->removeUserOutputExtractor(oe);
    }

    SimulationModelStatePair* implTryGetCurrentSimulationState() final
    {
        return m_ShownModelState.get();
    }

    void drawContent()
    {
        m_Toolbar.draw();

        // only draw content if a simulation report is available
        std::optional<osc::SimulationReport> maybeReport = TrySelectReportBasedOnScrubbing(*m_Simulation);
        if (maybeReport)
        {
            m_ShownModelState->setSimulation(m_Simulation);
            m_ShownModelState->setSimulationReport(*maybeReport);

            OSC_PERF("draw simulation screen");
            m_PanelManager->onDraw();
        }
        else
        {
            ImGui::Begin("Waiting for simulation");
            ImGui::TextDisabled("(waiting for first simulation state)");
            ImGui::End();

            // and show the log, so that the user can see any errors from the integrator (#628)
            //
            // this might be less necessary once the integrator correctly reports errors to
            // this UI panel (#625)
            LogViewerPanel p{"Log"};
            p.draw();
        }
    }

    std::optional<osc::SimulationReport> TrySelectReportBasedOnScrubbing(osc::VirtualSimulation& sim)
    {
        std::optional<osc::SimulationReport> maybeReport = trySelectReportBasedOnScrubbing();

        if (!maybeReport)
        {
            return maybeReport;
        }

        osc::SimulationReport& report = *maybeReport;

        // HACK: re-realize state, because of the OpenSim pathwrap bug: https://github.com/ComputationalBiomechanicsLab/opensim-creator/issues/123
        SimTK::State& st = report.updStateHACK();
        st.invalidateAllCacheAtOrAbove(SimTK::Stage::Instance);
        sim.getModel()->realizeReport(st);

        return maybeReport;
    }

    // tab data
    UID m_ID;
    std::weak_ptr<MainUIStateAPI> m_Parent;
    std::string m_Name = ICON_FA_PLAY " Simulation_" + std::to_string(g_SimulationNumber++);

    // underlying simulation being shown
    std::shared_ptr<Simulation> m_Simulation;

    // the modelstate that's being shown in the UI, based on scrubbing etc.
    //
    // if possible (i.e. there's a simulation report available), will be set each frame
    std::shared_ptr<SimulationModelStatePair> m_ShownModelState = std::make_shared<osc::SimulationModelStatePair>();

    // scrubbing state
    bool m_IsPlayingBack = true;
    float m_PlaybackSpeed = 1.0f;
    SimulationClock::time_point m_PlaybackStartSimtime = m_Simulation->getStartTime();
    std::chrono::system_clock::time_point m_PlaybackStartWallTime = std::chrono::system_clock::now();

    // manager for toggleable and spawnable UI panels
    std::shared_ptr<PanelManager> m_PanelManager = std::make_shared<PanelManager>();

    // non-toggleable UI panels/menus/toolbars
    MainMenuFileTab m_MainMenuFileTab;
    MainMenuAboutTab m_MainMenuAboutTab;
    WindowMenu m_MainMenuWindowTab{m_PanelManager};
    SimulationToolbar m_Toolbar{"##SimulationToolbar", this, m_Simulation};
};


// public API (PIMPL)

osc::SimulatorTab::SimulatorTab(
    std::weak_ptr<MainUIStateAPI> parent_,
    std::shared_ptr<Simulation> simulation_) :

    m_Impl{std::make_unique<Impl>(std::move(parent_), std::move(simulation_))}
{
}

osc::SimulatorTab::SimulatorTab(SimulatorTab&&) noexcept = default;
osc::SimulatorTab& osc::SimulatorTab::operator=(SimulatorTab&&) noexcept = default;
osc::SimulatorTab::~SimulatorTab() noexcept = default;

osc::UID osc::SimulatorTab::implGetID() const
{
    return m_Impl->getID();
}

osc::CStringView osc::SimulatorTab::implGetName() const
{
    return m_Impl->getName();
}

void osc::SimulatorTab::implOnMount()
{
    m_Impl->onMount();
}

void osc::SimulatorTab::implOnUnmount()
{
    m_Impl->onUnmount();
}

bool osc::SimulatorTab::implOnEvent(SDL_Event const& e)
{
    return m_Impl->onEvent(e);
}

void osc::SimulatorTab::implOnTick()
{
    m_Impl->onTick();
}

void osc::SimulatorTab::implOnDrawMainMenu()
{
    m_Impl->onDrawMainMenu();
}

void osc::SimulatorTab::implOnDraw()
{
    m_Impl->onDraw();
}
