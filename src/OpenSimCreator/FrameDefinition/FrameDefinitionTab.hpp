#pragma once

#include <oscar/Tabs/Tab.hpp>
#include <oscar/Utils/CStringView.hpp>
#include <oscar/Utils/UID.hpp>

#include <SDL_events.h>

#include <memory>

namespace osc { class TabHost; }

namespace osc
{
    class FrameDefinitionTab final : public Tab {
    public:
        static CStringView id() noexcept;

        explicit FrameDefinitionTab(std::weak_ptr<TabHost>);
        FrameDefinitionTab(FrameDefinitionTab const&) = delete;
        FrameDefinitionTab(FrameDefinitionTab&&) noexcept;
        FrameDefinitionTab& operator=(FrameDefinitionTab const&) = delete;
        FrameDefinitionTab& operator=(FrameDefinitionTab&&) noexcept;
        ~FrameDefinitionTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        void implOnMount() final;
        void implOnUnmount() final;
        bool implOnEvent(SDL_Event const&) final;
        void implOnTick() final;
        void implOnDrawMainMenu() final;
        void implOnDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}