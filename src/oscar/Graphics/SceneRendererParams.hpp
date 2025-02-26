#pragma once

#include "oscar/Graphics/Color.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace osc
{
    struct SceneRendererParams final {

        SceneRendererParams();

        glm::ivec2 dimensions;
        int32_t samples;
        bool drawMeshNormals;
        bool drawRims;
        bool drawShadows;
        bool drawFloor;
        float nearClippingPlane;
        float farClippingPlane;
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec3 viewPos;
        glm::vec3 lightDirection;
        Color lightColor;  // ignores alpha
        float ambientStrength;
        float diffuseStrength;
        float specularStrength;
        float shininess;
        Color backgroundColor;
        Color rimColor;
        glm::vec2 rimThicknessInPixels;
        glm::vec3 floorLocation;
        float fixupScaleFactor;
    };

    bool operator==(SceneRendererParams const&, SceneRendererParams const&);
    bool operator!=(SceneRendererParams const&, SceneRendererParams const&);
}