#pragma once

#include "src/Tabs/Tab.hpp"
#include "src/Utils/CStringView.hpp"
#include "src/Utils/UID.hpp"

#include <SDL_events.h>

#include <memory>

namespace osc { class TabHost; }

namespace osc
{
    class CookiecutterTab final : public Tab {
    public:
        CookiecutterTab(TabHost*);
        CookiecutterTab(CookiecutterTab const&) = delete;
        CookiecutterTab(CookiecutterTab&&) noexcept;
        CookiecutterTab& operator=(CookiecutterTab const&) = delete;
        CookiecutterTab& operator=(CookiecutterTab&&) noexcept;
        ~CookiecutterTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        TabHost* implParent() const final;
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