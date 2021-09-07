#pragma once

#include "src/3D/Model.hpp"
#include "src/3D/BVH.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace osc {
    class SceneMesh final {
    public:
        using IdType = int;

        SceneMesh(Mesh const&);

        IdType getID() const noexcept;
        Mesh const& getMesh() const noexcept;
        std::vector<glm::vec3> const& getVerts() const noexcept;
        std::vector<glm::vec3> const& getNormals() const noexcept;
        std::vector<glm::vec2> const& getTexCoords() const noexcept;
        std::vector<unsigned short> const& getIndices() const noexcept;
        AABB const& getAABB() const noexcept;
        Sphere const& getBoundingSphere() const noexcept;
        BVH const& getTriangleBVH() const noexcept;

    private:
        IdType ID;
        Mesh mesh;
        AABB aabb;
        Sphere boundingSphere;
        BVH triangleBVH;
    };
}
