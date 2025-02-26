#pragma once

#include <oscar/Widgets/Popup.hpp>

#include <memory>
#include <string_view>

namespace osc { class ParamBlock; }

namespace osc
{
    // popup that edits a parameter block in-place
    class ParamBlockEditorPopup final : public Popup {
    public:
        ParamBlockEditorPopup(std::string_view popupName, ParamBlock*);
        ParamBlockEditorPopup(ParamBlockEditorPopup const&) = delete;
        ParamBlockEditorPopup(ParamBlockEditorPopup&&) noexcept;
        ParamBlockEditorPopup& operator=(ParamBlockEditorPopup const&) = delete;
        ParamBlockEditorPopup& operator=(ParamBlockEditorPopup&&) noexcept;
        ~ParamBlockEditorPopup() noexcept;

    private:
        bool implIsOpen() const final;
        void implOpen() final;
        void implClose() final;
        bool implBeginPopup() final;
        void implDrawPopupContent() final;
        void implEndPopup() final;

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
