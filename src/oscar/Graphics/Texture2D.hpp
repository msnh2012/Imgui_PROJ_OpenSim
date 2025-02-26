#pragma once

#include "oscar/Graphics/ColorSpace.hpp"
#include "oscar/Graphics/TextureFilterMode.hpp"
#include "oscar/Graphics/TextureFormat.hpp"
#include "oscar/Graphics/TextureWrapMode.hpp"
#include "oscar/Utils/CopyOnUpdPtr.hpp"
#include "oscar/Utils/CStringView.hpp"

#include <glm/vec2.hpp>
#include <nonstd/span.hpp>

#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace osc { struct Rgba32; }
namespace osc { class Texture2D; }
namespace osc { void DrawTextureAsImGuiImage(Texture2D const&, glm::vec2, glm::vec2, glm::vec2); }
namespace osc { bool ImageButton(CStringView label, Texture2D const& t, glm::vec2 dims); }

// note: implementation is in `GraphicsImplementation.cpp`
namespace osc
{
    // a handle to a 2D texture that can be rendered by the graphics backend
    class Texture2D final {
    public:
        Texture2D(
            glm::ivec2 dimensions,
            nonstd::span<Rgba32 const> rgbaPixelsRowByRow,
            ColorSpace
        );
        Texture2D(
            glm::ivec2 dimensions,
            TextureFormat,
            nonstd::span<uint8_t const> channelsRowByRow,
            ColorSpace
        );
        Texture2D(Texture2D const&);
        Texture2D(Texture2D&&) noexcept;
        Texture2D& operator=(Texture2D const&);
        Texture2D& operator=(Texture2D&&) noexcept;
        ~Texture2D() noexcept;

        glm::ivec2 getDimensions() const;
        float getAspectRatio() const;

        ColorSpace getColorSpace() const;

        TextureWrapMode getWrapMode() const;  // same as getWrapModeU
        void setWrapMode(TextureWrapMode);  // sets all axes
        TextureWrapMode getWrapModeU() const;
        void setWrapModeU(TextureWrapMode);
        TextureWrapMode getWrapModeV() const;
        void setWrapModeV(TextureWrapMode);
        TextureWrapMode getWrapModeW() const;
        void setWrapModeW(TextureWrapMode);

        TextureFilterMode getFilterMode() const;
        void setFilterMode(TextureFilterMode);

        friend void swap(Texture2D& a, Texture2D& b) noexcept
        {
            swap(a.m_Impl, b.m_Impl);
        }

    private:
        friend void osc::DrawTextureAsImGuiImage(Texture2D const&, glm::vec2, glm::vec2, glm::vec2);
        friend bool osc::ImageButton(CStringView label, Texture2D const& t, glm::vec2 dims);
        void* getTextureHandleHACK() const;  // used by ImGui... for now

        friend class GraphicsBackend;
        friend bool operator==(Texture2D const&, Texture2D const&) noexcept;
        friend bool operator!=(Texture2D const&, Texture2D const&) noexcept;
        friend std::ostream& operator<<(std::ostream&, Texture2D const&);

        class Impl;
        CopyOnUpdPtr<Impl> m_Impl;
    };

    inline bool operator==(Texture2D const& a, Texture2D const& b) noexcept
    {
        return a.m_Impl == b.m_Impl;
    }

    inline bool operator!=(Texture2D const& a, Texture2D const& b) noexcept
    {
        return a.m_Impl != b.m_Impl;
    }

    std::ostream& operator<<(std::ostream&, Texture2D const&);
}
