#pragma once

#include "src/Widgets/Panel.hpp"

#include <memory>
#include <string_view>

namespace osc
{
    class PerfPanel final : public Panel {
    public:
        PerfPanel(std::string_view panelName);
        PerfPanel(PerfPanel const&) = delete;
        PerfPanel(PerfPanel&&) noexcept;
        PerfPanel& operator=(PerfPanel const&) = delete;
        PerfPanel& operator=(PerfPanel&&) noexcept;
        ~PerfPanel();

    private:
        bool implIsOpen() const final;
        void implOpen() final;
        void implClose() final;
        void implDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
