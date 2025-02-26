#pragma once

#include <oscar/Tabs/Tab.hpp>
#include <oscar/Utils/CStringView.hpp>
#include <oscar/Utils/UID.hpp>

#include <SDL_events.h>

#include <memory>

namespace osc { class TabHost; }
namespace osc { class UndoableModelStatePair; }

namespace osc
{
    class ExcitationEditorTab final : public Tab {
    public:
        static CStringView id() noexcept;

        ExcitationEditorTab(
            std::weak_ptr<TabHost>,
            std::shared_ptr<UndoableModelStatePair const>
        );
        ExcitationEditorTab(ExcitationEditorTab const&) = delete;
        ExcitationEditorTab(ExcitationEditorTab&&) noexcept;
        ExcitationEditorTab& operator=(ExcitationEditorTab const&) = delete;
        ExcitationEditorTab& operator=(ExcitationEditorTab&&) noexcept;
        ~ExcitationEditorTab() noexcept override;

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