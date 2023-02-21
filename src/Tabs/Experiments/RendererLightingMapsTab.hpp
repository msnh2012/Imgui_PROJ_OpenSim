#pragma once

#include "src/Tabs/Tab.hpp"
#include "src/Utils/CStringView.hpp"
#include "src/Utils/UID.hpp"

#include <SDL_events.h>

#include <memory>

namespace osc { class TabHost; }

namespace osc
{
    class RendererLightingMapsTab final : public Tab {
    public:
        static CStringView id() noexcept;

        explicit RendererLightingMapsTab(std::weak_ptr<TabHost>);
        RendererLightingMapsTab(RendererLightingMapsTab const&) = delete;
        RendererLightingMapsTab(RendererLightingMapsTab&&) noexcept;
        RendererLightingMapsTab& operator=(RendererLightingMapsTab const&) = delete;
        RendererLightingMapsTab& operator=(RendererLightingMapsTab&&) noexcept;
        ~RendererLightingMapsTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        void implOnMount() final;
        void implOnUnmount() final;
        bool implOnEvent(SDL_Event const&) final;
        void implOnDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
