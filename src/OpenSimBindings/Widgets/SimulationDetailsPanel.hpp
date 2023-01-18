#pragma once

#include "src/Utils/CStringView.hpp"
#include "src/Widgets/Panel.hpp"

#include <memory>
#include <string_view>

namespace osc { class Simulation; }
namespace osc { class SimulatorUIAPI; }

namespace osc
{
    class SimulationDetailsPanel final : public Panel {
    public:
        SimulationDetailsPanel(
            std::string_view panelName,
            SimulatorUIAPI*,
            std::shared_ptr<Simulation>
        );
        SimulationDetailsPanel(SimulationDetailsPanel const&) = delete;
        SimulationDetailsPanel(SimulationDetailsPanel&&) noexcept;
        SimulationDetailsPanel& operator=(SimulationDetailsPanel const&) = delete;
        SimulationDetailsPanel& operator=(SimulationDetailsPanel&&) noexcept;
        ~SimulationDetailsPanel() noexcept;

    private:
        CStringView implGetName() const final;
        bool implIsOpen() const final;
        void implOpen() final;
        void implClose() final;
        void implDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}