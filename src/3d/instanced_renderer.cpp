#include "instanced_renderer.hpp"

#include "src/3d/gl.hpp"
#include "src/3d/gl_glm.hpp"
#include "src/3d/model.hpp"
#include "src/3d/shaders/gouraud_mrt_shader.hpp"
#include "src/3d/shaders/edge_detection_shader.hpp"
#include "src/3d/shaders/normals_shader.hpp"

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include <cstddef>
#include <vector>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <iostream>

using namespace osc;

namespace {

    // helper method for making a render buffer (used in Render_target)
    gl::Render_buffer mk_renderbuffer(int samples, GLenum format, int w, int h) {
        gl::Render_buffer rv;
        gl::BindRenderBuffer(rv);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, w, h);
        return rv;
    }

    // draw targets written to by the renderer
    struct Render_target final {
        glm::ivec2 dims;
        int samples;

        // internally used for initial render pass
        gl::Render_buffer scene_msxaa_rb;
        gl::Render_buffer rims_msxaa_rb;
        gl::Render_buffer depth24stencil8_msxaa_rb;
        gl::Frame_buffer render_msxaa_fbo;

        // internally used to blit the solid rims (before edge-detection) into
        // a cheaper-to-sample not-multisampled texture
        gl::Texture_2d rims_tex;
        gl::Frame_buffer rims_tex_fbo;

        // these are the actual outputs
        gl::Texture_2d output_tex;
        gl::Texture_2d output_depth24stencil8_tex;
        gl::Frame_buffer output_fbo;

        Render_target(glm::ivec2 dims_, int samples_) :
            dims{dims_},
            samples{samples_},

            scene_msxaa_rb{mk_renderbuffer(samples, GL_RGBA, dims.x, dims.y)},
            rims_msxaa_rb{mk_renderbuffer(samples, GL_RED, dims.x, dims.y)},
            depth24stencil8_msxaa_rb{mk_renderbuffer(samples, GL_DEPTH24_STENCIL8, dims.x, dims.y)},
            render_msxaa_fbo{[this]() {
                gl::Frame_buffer rv;
                gl::BindFramebuffer(GL_FRAMEBUFFER, rv);
                gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, scene_msxaa_rb);
                gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, rims_msxaa_rb);
                gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, depth24stencil8_msxaa_rb);
                gl::BindFramebuffer(GL_FRAMEBUFFER, gl::window_fbo);
                return rv;
            }()},

            rims_tex{[this]() {
                gl::Texture_2d rv;
                gl::BindTexture(rv);
                gl::TexImage2D(rv.type, 0, GL_RED, dims.x, dims.y, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
                gl::TexParameteri(rv.type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // no mipmaps
                gl::TexParameteri(rv.type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // no mipmaps
                return rv;
            }()},
            rims_tex_fbo{[this]() {
                gl::Frame_buffer rv;
                gl::BindFramebuffer(GL_FRAMEBUFFER, rv);
                gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rims_tex, 0);
                gl::BindFramebuffer(GL_FRAMEBUFFER, gl::window_fbo);
                return rv;
            }()},

            output_tex{[this]() {
                gl::Texture_2d rv;
                gl::BindTexture(rv);
                gl::TexImage2D(rv.type, 0, GL_RGBA, dims.x, dims.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                gl::TexParameteri(rv.type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // no mipmaps
                gl::TexParameteri(rv.type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // no mipmaps
                return rv;
            }()},
            output_depth24stencil8_tex{[this]() {
                gl::Texture_2d rv;
                gl::BindTexture(rv);
                // https://stackoverflow.com/questions/27535727/opengl-create-a-depth-stencil-texture-for-reading
                gl::TexImage2D(rv.type, 0, GL_DEPTH24_STENCIL8, dims.x, dims.y, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
                return rv;
            }()},
            output_fbo{[this]() {
                gl::Frame_buffer rv;
                gl::BindFramebuffer(GL_FRAMEBUFFER, rv);
                gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, output_tex, 0);
                gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, output_depth24stencil8_tex, 0);
                gl::BindFramebuffer(GL_FRAMEBUFFER, gl::window_fbo);
                return rv;
            }()} {
        }
    };

    // GPU format of mesh instance
    struct GPU_meshinstance final {
        glm::mat4x3 model_xform;
        glm::mat3 normal_xform;
        Rgba32 rgba;
        GLubyte rim_intensity;
    };

    // GPU format of meshdata with no texcoords
    struct GPU_untextured_meshdata final {
        glm::vec3 pos;
        glm::vec3 norm;
    };

    // GPU format of meshdata with texcoords
    struct GPU_textured_meshdata final {
        glm::vec3 pos;
        glm::vec3 norm;
        glm::vec2 uv;
    };

    // create VAO for the Gouraud shader
    gl::Vertex_array create_Gouraud_vao(
            NewMesh const& mesh,
            gl::Array_buffer<GLubyte>& data,
            gl::Element_array_buffer<GLushort>& ebo,
            gl::Array_buffer<GPU_meshinstance, GL_DYNAMIC_DRAW>& instances) {

        static_assert(offsetof(GPU_untextured_meshdata, pos) == offsetof(GPU_textured_meshdata, pos));
        static_assert(offsetof(GPU_untextured_meshdata, norm) == offsetof(GPU_textured_meshdata, norm));
        static constexpr size_t offset_pos = offsetof(GPU_untextured_meshdata, pos);
        static constexpr size_t offset_norm = offsetof(GPU_untextured_meshdata, norm);
        using GMS = Gouraud_mrt_shader;

        size_t stride = mesh.texcoords.empty() ? sizeof(GPU_untextured_meshdata) : sizeof(GPU_textured_meshdata);

        gl::Vertex_array vao;
        gl::BindVertexArray(vao);

        // bind vertex data to (non-instanced) attrs
        gl::BindBuffer(data);
        gl::VertexAttribPointer(GMS::aPos, false, stride, offset_pos);
        gl::EnableVertexAttribArray(GMS::aPos);
        gl::VertexAttribPointer(GMS::aNormal, false, stride, offset_norm);
        gl::EnableVertexAttribArray(GMS::aNormal);
        if (!mesh.texcoords.empty()) {
            gl::VertexAttribPointer(GMS::aTexCoord, false, stride, offsetof(GPU_textured_meshdata, uv));
            gl::EnableVertexAttribArray(GMS::aTexCoord);
        }

        // bind EBO
        gl::BindBuffer(ebo);

        // bind instance data
        gl::BindBuffer(instances);

        gl::VertexAttribPointer(GMS::aModelMat, false, sizeof(GPU_meshinstance), offsetof(GPU_meshinstance, model_xform));
        gl::VertexAttribDivisor(GMS::aModelMat, 1);
        gl::EnableVertexAttribArray(GMS::aModelMat);

        gl::VertexAttribPointer(GMS::aNormalMat, false, sizeof(GPU_meshinstance), offsetof(GPU_meshinstance, normal_xform));
        gl::VertexAttribDivisor(GMS::aNormalMat, 1);
        gl::EnableVertexAttribArray(GMS::aNormalMat);

        // note: RGB is normalized CPU side ([0x00, 0xff]) and needs to be unpacked
        // into a float
        gl::VertexAttribPointer<gl::glsl::vec4, GL_UNSIGNED_BYTE>(GMS::aDiffuseColor, true, sizeof(GPU_meshinstance), offsetof(GPU_meshinstance, rgba));
        gl::VertexAttribDivisor(GMS::aDiffuseColor, 1);
        gl::EnableVertexAttribArray(GMS::aDiffuseColor);

        // note: RimIntensity is normalized from its CPU byte value into a float
        gl::VertexAttribPointer<gl::glsl::float_, GL_UNSIGNED_BYTE>(GMS::aRimIntensity, true, sizeof(GPU_meshinstance), offsetof(GPU_meshinstance, rim_intensity));
        gl::VertexAttribDivisor(GMS::aRimIntensity, 1);
        gl::EnableVertexAttribArray(GMS::aRimIntensity);

        gl::BindVertexArray();

        return vao;
    }

    // create VAO for the normals shader
    gl::Vertex_array create_Normals_vao(
            NewMesh const& mesh,
            gl::Array_buffer<GLubyte>& vbo,
            gl::Element_array_buffer<GLushort>& ebo) {

        static_assert(offsetof(GPU_untextured_meshdata, pos) == offsetof(GPU_textured_meshdata, pos));
        static_assert(offsetof(GPU_untextured_meshdata, norm) == offsetof(GPU_textured_meshdata, norm));
        static constexpr size_t offset_pos = offsetof(GPU_untextured_meshdata, pos);
        static constexpr size_t offset_norm = offsetof(GPU_untextured_meshdata, norm);
        using NS = Normals_shader;

        size_t stride = mesh.texcoords.empty() ? sizeof(GPU_untextured_meshdata) : sizeof(GPU_textured_meshdata);

        gl::Vertex_array vao;
        gl::BindVertexArray(vao);
        gl::BindBuffer(vbo);
        gl::BindBuffer(ebo);
        gl::VertexAttribPointer(NS::aPos, false, stride, offset_pos);
        gl::EnableVertexAttribArray(NS::aPos);
        gl::VertexAttribPointer(NS::aNormal, false, stride, offset_norm);
        gl::EnableVertexAttribArray(NS::aNormal);
        gl::BindVertexArray();
        return vao;
    }
}

// meshdata backend implementation
//
// effectively, preloads onto the GPU and preallocates space for the instance
// buffer
struct Instanceable_meshdata::Impl final {
    gl::Array_buffer<GLubyte> data;
    gl::Element_array_buffer<GLushort> indices;
    gl::Array_buffer<GPU_meshinstance, GL_DYNAMIC_DRAW> instances;
    gl::Vertex_array gouraud_vao;
    gl::Vertex_array normals_vao;

    Impl(gl::Array_buffer<GLubyte> data_,
         gl::Element_array_buffer<GLushort> indices_,
         gl::Array_buffer<GPU_meshinstance, GL_DYNAMIC_DRAW> instances_,
         gl::Vertex_array gouraud_vao_,
         gl::Vertex_array normals_vao_) :

        data{std::move(data_)},
        indices{std::move(indices_)},
        instances{std::move(instances_)},
        gouraud_vao{std::move(gouraud_vao_)},
        normals_vao{std::move(normals_vao_)} {
    }
};

// produced by "compiling" a CPU-side "striped" list of things to draw and
// pre-optimized for optimal rendering
//
// external users can't manipulate this
struct Instanced_drawlist::Impl final {
    std::vector<GPU_meshinstance> gpu_instances;
    std::vector<Instanceable_meshdata> meshdata;
    std::vector<std::shared_ptr<gl::Texture_2d>> textures;
    std::vector<int> order;  // used during construction to reorder elements
};

struct osc::Instanced_renderer::Impl final {
    Gouraud_mrt_shader gouraud;
    Edge_detection_shader edge_detect;
    Normals_shader normals_shader;
    Render_target rt;

    gl::Array_buffer<GPU_textured_meshdata> quad_vbo{[&]() {
        NewMesh m = gen_textured_quad();

        std::vector<GPU_textured_meshdata> swap;
        for (size_t i = 0; i < m.indices.size(); ++i) {
            unsigned short idx = m.indices[i];
            GPU_textured_meshdata& tv = swap.emplace_back();
            tv.pos = m.verts[idx];
            tv.norm = m.normals[idx];
            tv.uv = m.texcoords[idx];
        }

        return gl::Array_buffer<GPU_textured_meshdata>{swap};
    }()};

    gl::Vertex_array edgedetect_vao = [this]() {
        gl::Vertex_array rv;
        gl::BindVertexArray(rv);
        gl::BindBuffer(quad_vbo);
        gl::VertexAttribPointer(edge_detect.aPos, false, sizeof(GPU_textured_meshdata), offsetof(GPU_textured_meshdata, pos));
        gl::EnableVertexAttribArray(edge_detect.aPos);
        gl::VertexAttribPointer(edge_detect.aTexCoord, false, sizeof(GPU_textured_meshdata), offsetof(GPU_textured_meshdata, uv));
        gl::EnableVertexAttribArray(edge_detect.aTexCoord);
        return rv;
    }();

    Impl(glm::ivec2 dims, int samples) : rt{dims, samples} {
    }
};

// public API

osc::Instanceable_meshdata::Instanceable_meshdata(std::shared_ptr<Impl> impl) : m_Impl{impl} {
}

osc::Instanceable_meshdata::~Instanceable_meshdata() noexcept = default;

Instanceable_meshdata osc::upload_meshdata_for_instancing(NewMesh const& mesh) {
    if (mesh.verts.size() != mesh.normals.size()) {
        throw std::runtime_error{"mismatch between number of verts and number of normals in a mesh"};
    }

    if (!mesh.texcoords.empty() && mesh.texcoords.size() != mesh.verts.size()) {
        throw std::runtime_error{"mismatch between number of tex coords in the mesh and the number of verts"};
    }

    // un-stripe and upload the mesh data
    gl::Array_buffer<GLubyte> vbo;
    if (mesh.texcoords.empty()) {
        std::vector<GPU_untextured_meshdata> repacked;
        repacked.reserve(mesh.verts.size());
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            repacked.push_back(GPU_untextured_meshdata{mesh.verts[i], mesh.normals[i]});
        }
        vbo.assign(reinterpret_cast<GLubyte const*>(repacked.data()), sizeof(GPU_untextured_meshdata) * repacked.size());
    } else {
        std::vector<GPU_textured_meshdata> repacked;
        repacked.reserve(mesh.verts.size());
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            repacked.push_back(GPU_textured_meshdata{mesh.verts[i], mesh.normals[i], mesh.texcoords[i]});
        }
        vbo.assign(reinterpret_cast<GLubyte const*>(repacked.data()), sizeof(GPU_textured_meshdata) * repacked.size());
    }

    // make indices
    gl::Element_array_buffer<GLushort> ebo{mesh.indices};

    // preallocate instance buffer (used at render time)
    gl::Array_buffer<GPU_meshinstance, GL_DYNAMIC_DRAW> instances;

    // make Gouraud VAO
    gl::Vertex_array gouraud_vao = create_Gouraud_vao(mesh, vbo, ebo, instances);
    gl::Vertex_array normals_vao = create_Normals_vao(mesh, vbo, ebo);

    // allocate the shared handle
    auto impl = std::make_shared<Instanceable_meshdata::Impl>(
                std::move(vbo),
                std::move(ebo),
                std::move(instances),
                std::move(gouraud_vao),
                std::move(normals_vao));

    // wrap the handle in the externally-unmodifiable type
    return Instanceable_meshdata{impl};
}

osc::Instanced_drawlist::Instanced_drawlist() : m_Impl{new Impl{}} {
}

osc::Instanced_drawlist::~Instanced_drawlist() noexcept = default;

void osc::upload_inputs_to_drawlist(Drawlist_compiler_input const& inp, Instanced_drawlist& dl) {
    auto& impl = *dl.m_Impl;

    auto& instances = impl.gpu_instances;
    auto& meshdata = impl.meshdata;
    auto& textures = impl.textures;
    auto& order = impl.order;

    // clear any previous data
    instances.clear();
    meshdata.clear();
    textures.clear();
    order.clear();

    // write the output ordering into `order`
    order.reserve(inp.ninstances);
    for (size_t i = 0; i < inp.ninstances; ++i) {
        order.push_back(static_cast<int>(i));
    }
    auto draw_order = [&](int a, int b) {
        // order by meshid, which is how the instanced renderer batches
        return inp.meshes[a].m_Impl.get() < inp.meshes[b].m_Impl.get();
    };
    std::sort(order.begin(), order.end(), draw_order);

    // de-stripe the input data into a drawlist
    for (int o : order) {
        // set up instance data
        GPU_meshinstance& inst = instances.emplace_back();
        inst.model_xform = inp.model_xforms[o];
        inst.normal_xform = inp.normal_xforms[o];
        if (inp.colors) {
            inst.rgba = inp.colors[o];
        } else {
            inst.rgba = rgba32_from_u32(0xff0000ff);
        }
        if (inp.rim_intensity) {
            inst.rim_intensity = inp.rim_intensity[o];
        } else {
            inst.rim_intensity = 0x00;
        }

        // set up texture (if applicable - might not be textured)
        if (inp.textures && inp.textures[o]) {
            textures.emplace_back(inp.textures[o]);
        } else {
            textures.emplace_back(nullptr);
        }

        // set up mesh (required)
        meshdata.emplace_back(inp.meshes[o]);
    }
}

osc::Instanced_renderer::Instanced_renderer() :
    m_Impl{new Impl{glm::ivec2{1, 1}, 1}} {
}

osc::Instanced_renderer::Instanced_renderer(glm::ivec2 dims, int samples) :
    m_Impl{new Impl{dims, samples}} {
}

osc::Instanced_renderer::~Instanced_renderer() noexcept = default;

glm::ivec2 osc::Instanced_renderer::dims() const noexcept {
    return m_Impl->rt.dims;
}

glm::vec2 osc::Instanced_renderer::dimsf() const noexcept {
    return m_Impl->rt.dims;
}

void osc::Instanced_renderer::set_dims(glm::ivec2 d) {
    Render_target& rt = m_Impl->rt;

    if (rt.dims == d) {
        return;  // no change
    }

    // else: remake buffers
    rt = Render_target{d, rt.samples};
}

float osc::Instanced_renderer::aspect_ratio() const noexcept {
    glm::ivec2 const& dimsi = m_Impl->rt.dims;
    glm::vec2 dimsf{dimsi};
    return dimsf.x/dimsf.y;

}

int osc::Instanced_renderer::msxaa_samples() const noexcept {
    return m_Impl->rt.samples;
}

void osc::Instanced_renderer::set_msxaa_samples(int samps) {
    Render_target& rt = m_Impl->rt;

    if (rt.samples == samps) {
        return; // no change
    }

    // else: remake buffers
    rt = Render_target{rt.dims, samps};
}

void osc::Instanced_renderer::render(Render_params const& p, Instanced_drawlist const& dl) {
    Impl& impl = *m_Impl;
    Render_target& rt = impl.rt;
    Instanced_drawlist::Impl& dimpl = *dl.m_Impl;

    gl::Viewport(0, 0, rt.dims.x, rt.dims.y);
    gl::BindFramebuffer(GL_FRAMEBUFFER, rt.render_msxaa_fbo);

    // clear render buffers
    gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl::ClearColor(p.background_rgba);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::DrawBuffer(GL_COLOR_ATTACHMENT1);
    gl::ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT);

    // set wireframe mode on if requested
    if (p.flags & DrawcallFlags_WireframeMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    // draw scene
    if (p.flags & DrawcallFlags_DrawSceneGeometry) {
        Gouraud_mrt_shader& shader = impl.gouraud;

        // setup per-render params
        gl::DrawBuffers(GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1);
        gl::UseProgram(shader.program);
        gl::Uniform(shader.uProjMat, p.projection_matrix);
        gl::Uniform(shader.uViewMat, p.view_matrix);
        gl::Uniform(shader.uLightDir, p.light_dir);
        gl::Uniform(shader.uLightColor, p.light_rgb);
        gl::Uniform(shader.uViewPos, p.view_pos);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnablei(GL_BLEND, 0);  // COLOR0
        glDisablei(GL_BLEND, 1);  // COLOR1

        size_t pos = 0;
        size_t ninsts = dimpl.gpu_instances.size();
        GPU_meshinstance const* insts = dimpl.gpu_instances.data();
        Instanceable_meshdata const* meshes = dimpl.meshdata.data();
        std::shared_ptr<gl::Texture_2d> const* textures = dimpl.textures.data();

        // iterate through all instances
        while (pos < ninsts) {
            Instanceable_meshdata::Impl* mesh = meshes[pos].m_Impl.get();
            gl::Texture_2d* tex = textures[pos].get();

            // group adjacent elements with the same mesh + texture
            size_t end = pos + 1;
            while (end < ninsts && meshes[end].m_Impl.get() == mesh && textures[end].get() == tex) {
                ++end;
            }

            // setup texture (if necessary)
            if (tex) {
                gl::Uniform(shader.uIsTextured, true);
                gl::ActiveTexture(GL_TEXTURE0);
                gl::BindTexture(*tex);
                gl::Uniform(shader.uSampler0, gl::texture_index<GL_TEXTURE0>());
            } else {
                gl::Uniform(shader.uIsTextured, false);
            }

            // upload instance data to GPU
            mesh->instances.assign(insts + pos, end - pos);

            // draw
            gl::BindVertexArray(mesh->gouraud_vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                mesh->indices.sizei(),
                gl::index_type(mesh->indices),
                nullptr,
                static_cast<GLsizei>(end - pos));
            gl::BindVertexArray();

            pos = end;
        }
    }

    if (p.flags & DrawcallFlags_ShowMeshNormals) {
        Normals_shader& shader = impl.normals_shader;
        gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl::UseProgram(shader.program);
        gl::Uniform(shader.uProjMat, p.projection_matrix);
        gl::Uniform(shader.uViewMat, p.view_matrix);

        for (size_t i = 0; i < dimpl.gpu_instances.size(); ++i) {
            gl::Uniform(shader.uModelMat, dimpl.gpu_instances[i].model_xform);
            gl::Uniform(shader.uNormalMat, dimpl.gpu_instances[i].normal_xform);
            gl::BindVertexArray(dimpl.meshdata[i].m_Impl->normals_vao);
            gl::DrawElements(GL_TRIANGLES, dimpl.meshdata[i].m_Impl->indices.sizei(), gl::index_type(dimpl.meshdata[i].m_Impl->indices), nullptr);
        }
        gl::BindVertexArray();
    }

    if (p.flags & DrawcallFlags_WireframeMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // blit scene to output texture
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, rt.render_msxaa_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, rt.output_fbo);
    gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl::BlitFramebuffer(0, 0, rt.dims.x, rt.dims.y, 0, 0, rt.dims.x, rt.dims.y, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);


    // handle rim highlights (if necessary)
    if (p.flags & DrawcallFlags_DrawRims) {

        // blit rims from MSXAAed (expensive to sample) texture to a standard
        // not-MSXAAed texture
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, rt.render_msxaa_fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, rt.rims_tex_fbo);
        gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl::BlitFramebuffer(0, 0, rt.dims.x, rt.dims.y, 0, 0, rt.dims.x, rt.dims.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // set shader to write directly to output
        gl::BindFramebuffer(GL_FRAMEBUFFER, rt.output_fbo);
        gl::DrawBuffer(GL_COLOR_ATTACHMENT0);

        // setup edge-detection shader
        Edge_detection_shader& shader = impl.edge_detect;
        gl::UseProgram(shader.p);
        gl::Uniform(shader.uModelMat, gl::identity_val);
        gl::Uniform(shader.uViewMat, gl::identity_val);
        gl::Uniform(shader.uProjMat, gl::identity_val);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(rt.rims_tex);
        gl::Uniform(shader.uSampler0, gl::texture_index<GL_TEXTURE0>());
        gl::Uniform(shader.uRimRgba, {1.0f, 0.4f, 0.0f, 0.85f});
        gl::Uniform(shader.uRimThickness, 2.0f / std::max(rt.dims.x, rt.dims.y));

        // draw edges, directly writing into output texture
        gl::Enable(GL_BLEND);
        gl::Disable(GL_DEPTH_TEST);
        gl::BindVertexArray(impl.edgedetect_vao);
        gl::DrawArrays(GL_TRIANGLES, 0, impl.quad_vbo.sizei());
        gl::BindVertexArray();
        gl::Enable(GL_DEPTH_TEST);
    }

    gl::BindFramebuffer(GL_FRAMEBUFFER, gl::window_fbo);
}

gl::Frame_buffer const& osc::Instanced_renderer::output_fbo() const noexcept {
    return m_Impl->rt.output_fbo;
}

gl::Frame_buffer& osc::Instanced_renderer::output_fbo() noexcept {
    return m_Impl->rt.output_fbo;
}

gl::Texture_2d const& osc::Instanced_renderer::output_texture() const noexcept {
    return m_Impl->rt.output_tex;
}

gl::Texture_2d& osc::Instanced_renderer::output_texture() noexcept {
    return m_Impl->rt.output_tex;
}

gl::Texture_2d const& osc::Instanced_renderer::depth_texture() const noexcept {
    return m_Impl->rt.output_depth24stencil8_tex;
}

gl::Texture_2d& osc::Instanced_renderer::depth_texture() {
    return m_Impl->rt.output_depth24stencil8_tex;
}
