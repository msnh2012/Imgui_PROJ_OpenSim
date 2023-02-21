#pragma once

#include "src/Tabs/Tab.hpp"
#include "src/Utils/CStringView.hpp"
#include "src/Utils/UID.hpp"

#include <SDL_events.h>

#include <memory>

namespace osc { class TabHost; }

namespace osc
{
    class MeshHittestTab final : public Tab {
    public:
        static CStringView id() noexcept;

        explicit MeshHittestTab(std::weak_ptr<TabHost>);
        MeshHittestTab(MeshHittestTab const&) = delete;
        MeshHittestTab(MeshHittestTab&&) noexcept;
        MeshHittestTab& operator=(MeshHittestTab const&) = delete;
        MeshHittestTab& operator=(MeshHittestTab&&) noexcept;
        ~MeshHittestTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        void implOnTick() final;
        void implOnDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}