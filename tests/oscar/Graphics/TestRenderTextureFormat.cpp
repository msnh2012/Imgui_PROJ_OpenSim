#include "oscar/Graphics/RenderTextureFormat.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>

TEST(RenderTextureFormat, AnyValueCanBePrintedToStream)
{
    for (int32_t i = 0; i < static_cast<int32_t>(osc::RenderTextureFormat::TOTAL); ++i)
    {
        auto const format = static_cast<osc::RenderTextureFormat>(i);
        std::stringstream ss;
        ss << format;

        ASSERT_FALSE(std::move(ss).str().empty());
    }
}

TEST(RenderTextureFormat, DefaultsToRGBA32)
{
    static_assert(osc::RenderTextureFormat::Default == osc::RenderTextureFormat::ARGB32);
}

TEST(RenderTextureFormat, DefaultHDRFormatIsARGBHalf)
{
    static_assert(osc::RenderTextureFormat::DefaultHDR == osc::RenderTextureFormat::ARGBHalf);
}
