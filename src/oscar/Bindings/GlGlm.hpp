#pragma once

#include "oscar/Bindings/Gl.hpp"
#include "oscar/Utils/Assertions.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nonstd/span.hpp>

#include <type_traits>

// gl_glm: extensions for using glm types in OpenGL
namespace gl
{
    inline void Uniform(UniformMat3& u, glm::mat3 const& mat) noexcept
    {
        glUniformMatrix3fv(u.geti(), 1, false, glm::value_ptr(mat));
    }

    inline void Uniform(UniformVec4& u, glm::vec4 const& v) noexcept
    {
        glUniform4fv(u.geti(), 1, glm::value_ptr(v));
    }

    inline void Uniform(UniformVec3& u, glm::vec3 const& v) noexcept
    {
        glUniform3fv(u.geti(), 1, glm::value_ptr(v));
    }

    inline void Uniform(UniformVec3& u, nonstd::span<glm::vec3 const> vs) noexcept
    {
        static_assert(sizeof(glm::vec3) == 3 * sizeof(GLfloat));
        glUniform3fv(u.geti(), static_cast<GLsizei>(vs.size()), glm::value_ptr(vs.front()));
    }

    // set a uniform array of vec3s from a userspace container type (e.g. vector<glm::vec3>)
    template<typename Container, size_t N>
    inline std::enable_if_t<std::is_same_v<glm::vec3, typename Container::value_type>, void>
        Uniform(UniformArray<glsl::vec3, N>& u, Container& container) noexcept
    {
        OSC_ASSERT(container.size() == N);
        glUniform3fv(u.geti(), static_cast<GLsizei>(container.size()), glm::value_ptr(*container.data()));
    }

    inline void Uniform(UniformMat4& u, glm::mat4 const& mat) noexcept
    {
        glUniformMatrix4fv(u.geti(), 1, false, glm::value_ptr(mat));
    }

    inline void Uniform(UniformMat4& u, nonstd::span<glm::mat4 const> ms) noexcept
    {
        static_assert(sizeof(glm::mat4) == 16 * sizeof(GLfloat));
        glUniformMatrix4fv(u.geti(), static_cast<GLsizei>(ms.size()), false, glm::value_ptr(ms.front()));
    }

    inline void Uniform(UniformMat4& u, UniformIdentityValueTag) noexcept
    {
        Uniform(u, glm::identity<glm::mat4>());
    }

    inline void Uniform(UniformVec2& u, glm::vec2 const& v) noexcept
    {
        glUniform2fv(u.geti(), 1, glm::value_ptr(v));
    }

    inline void Uniform(UniformVec2& u, nonstd::span<glm::vec2 const> vs) noexcept
    {
        static_assert(sizeof(glm::vec2) == 2 * sizeof(GLfloat));
        glUniform2fv(u.geti(), static_cast<GLsizei>(vs.size()), glm::value_ptr(vs.front()));
    }

    template<typename Container, size_t N>
    inline std::enable_if_t<std::is_same_v<glm::vec2, typename Container::value_type>, void>
        Uniform(UniformArray<glsl::vec2, N>& u, Container const& container) noexcept
    {
        glUniform2fv(u.geti(),
                     static_cast<GLsizei>(container.size()),
                     glm::value_ptr(container.data()));
    }

    inline void ClearColor(glm::vec4 const& v) noexcept
    {
        ClearColor(v[0], v[1], v[2], v[3]);
    }
}
