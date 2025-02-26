#pragma once

#include <oscar/Tabs/Tab.hpp>
#include <oscar/Utils/CStringView.hpp>
#include <oscar/Utils/UID.hpp>

#include <memory>

namespace osc { class TabHost; }

namespace osc
{
    class TPS3DTab final : public Tab {
    public:
        static CStringView id() noexcept;

        explicit TPS3DTab(std::weak_ptr<TabHost>);
        TPS3DTab(TPS3DTab const&) = delete;
        TPS3DTab(TPS3DTab&&) noexcept;
        TPS3DTab& operator=(TPS3DTab const&) = delete;
        TPS3DTab& operator=(TPS3DTab&&) noexcept;
        ~TPS3DTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        void implOnMount() final;
        void implOnUnmount() final;
        void implOnTick() final;
        void implOnDrawMainMenu() final;
        void implOnDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}