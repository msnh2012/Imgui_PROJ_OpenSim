#pragma once

#include "oscar/Graphics/MaterialPropertyBlock.hpp"
#include "oscar/Graphics/BlitFlags.hpp"

#include <glm/mat4x4.hpp>

#include <optional>

namespace osc { class Camera; }
namespace osc { class Image; }
namespace osc { class Mesh; }
namespace osc { class Material; }
namespace osc { struct Rect; }
namespace osc { class RenderTexture; }
namespace osc { class Texture2D; }
namespace osc { struct Transform; }

// rendering functions
//
// these perform the necessary backend steps to get something useful done
namespace osc::Graphics
{
    void DrawMesh(
        Mesh const&,
        Transform const&,
        Material const&,
        Camera&,
        std::optional<MaterialPropertyBlock> = std::nullopt
    );

    void DrawMesh(
        Mesh const&,
        glm::mat4 const&,
        Material const&,
        Camera&,
        std::optional<MaterialPropertyBlock> = std::nullopt
    );

    void Blit(
        Texture2D const&,
        RenderTexture& dest
    );

    void ReadPixels(
        RenderTexture const&,
        Image& dest
    );

    void BlitToScreen(
        RenderTexture const&,
        Rect const&,
        BlitFlags = BlitFlags::None
    );

    // assigns the source RenderTexture to uniform "uTexture"
    void BlitToScreen(
        RenderTexture const&,
        Rect const&,
        Material const&,
        BlitFlags = BlitFlags::None
    );
}
