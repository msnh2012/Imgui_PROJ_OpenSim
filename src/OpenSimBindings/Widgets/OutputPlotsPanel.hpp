#pragma once

#include "src/Utils/CStringView.hpp"
#include "src/Widgets/Panel.hpp"

#include <memory>
#include <string_view>

namespace osc { class MainUIStateAPI; }
namespace osc { class SimulatorUIAPI; }

namespace osc
{
    class OutputPlotsPanel final : public Panel {
    public:
        OutputPlotsPanel(
            std::string_view panelName,
            MainUIStateAPI* mainUIStateAPI,
            SimulatorUIAPI* simulatorUIAPI
        );
        OutputPlotsPanel(OutputPlotsPanel const&) = delete;
        OutputPlotsPanel(OutputPlotsPanel&&) noexcept;
        OutputPlotsPanel& operator=(OutputPlotsPanel const&) = delete;
        OutputPlotsPanel& operator=(OutputPlotsPanel&&) noexcept;
        ~OutputPlotsPanel() noexcept;

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