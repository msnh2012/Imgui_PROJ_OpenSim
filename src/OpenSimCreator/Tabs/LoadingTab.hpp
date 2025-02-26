#pragma once

#include <oscar/Tabs/Tab.hpp>
#include <oscar/Utils/CStringView.hpp>
#include <oscar/Utils/UID.hpp>

#include <filesystem>
#include <memory>

namespace osc { class MainUIStateAPI; }

namespace osc
{
    class LoadingTab final : public Tab {
    public:
        LoadingTab(std::weak_ptr<MainUIStateAPI>, std::filesystem::path);
        LoadingTab(LoadingTab const&) = delete;
        LoadingTab(LoadingTab&&) noexcept;
        LoadingTab& operator=(LoadingTab const&) = delete;
        LoadingTab& operator=(LoadingTab&&) noexcept;
        ~LoadingTab() noexcept override;

    private:
        UID implGetID() const final;
        CStringView implGetName() const final;
        void implOnTick() final;
        void implOnDraw() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
