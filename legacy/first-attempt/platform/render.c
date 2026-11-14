/* Scene drawing: the rendered set's objects as textured triangles, from the NIF shapes of their models
   (tg_model_meshes), through an SDL3 GPU pipeline (Metal shading language source, compiled by SDL on the device). The camera is the
   set's active camera. Each shape has its base texture (TGA, BMP or DDS, decoded by image.c to RGBA, with mipmaps made on the GPU)
   times its material diffuse colour, plus its emissive colour.
   Approximations (marked): one fixed directional light plus a fixed ambient term (the set's lights are not used yet); no glow,
   specular or dark maps; texture alpha and NiAlphaProperty are ignored (all shapes opaque); 60 degree vertical field of
   view and near/far planes 1 and 1e6 (the camera's frustum values are not decoded yet); no depth sorting beyond the z-buffer.
   Combat effects: phaser beams, torpedo glows and cores, lens flares, the particle effects of Effects.py (explosion
   sheets, smoke, sparks: camera-facing quads with the sheet's frame, vertex colour and the blend modes the script names), the shield hit patches
   (a cap mesh with data/sphere.tga) and a starfield, drawn after the meshes. Effects bin by texture and blend mode, in the order they first
   appear; a texture-less sprite is the soft procedural sprite. Approximations (marked): the original draws the set's backdrop spheres (star and
   nebula textures), not done yet; the starfield is 2,500 fixed random stars at a large distance around the camera. */
#include <Python.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tg_models.h"
#include "image.h"
#include "tg_fx.h"

SDL_GPUDevice *pl_gpu(void);
SDL_Window *pl_window(void);
void *tg_render_camera(void);
void *tg_render_set(void);
void *tg_space_set_of(void *o);
int tg_space_hidden(void *o);
const char *tg_space_model_of(void *o);
void tg_space_pose(void *o, float *p, float *f, float *u, float *r, float *scale);
void tg_space_each(unsigned ct, void (*fn)(void *));

typedef struct { float pos[3], nrm[3], uv[2]; } Vtx;                    /* the TgMesh vertex layout */
typedef struct { float mvp[16]; float model[16]; float cloak[4]; float wo[4]; } VUni;   /* matrices column-major, as float4x4 in MSL (model: rotation only); cloak: visibility, mode (0 none), player ship, seed */
typedef struct { float diffuse[4]; float emissive[4]; float light[4]; float eye[4], origin[4]; float misc[4], specc[4]; float pl_pos[4][4], pl_col[4][4]; } FUni;   /* misc: glow intensity, the ship's Ks, shininess; specc: the material's specular colour */   /* eye: camera position; origin: the object's position */

typedef struct { Uint32 first, count; int mi; SDL_GPUTexture *tex, *glow, *glowa, *gloss; float diffuse[3], emissive[3], specular[3], shininess; } Part;   /* mi: the shape's index in tg_model_meshes */
typedef struct { char *name; SDL_GPUBuffer *buf; Uint32 nverts; Part *parts; int nparts; } Mesh;
static Mesh *meshes; static int nmeshes;
typedef struct { char *path; SDL_GPUTexture *tex; int w, h; } Tex;
static Tex *texs; static int ntexs;
static SDL_GPUTexture *white; static SDL_GPUSampler *sampler;
static SDL_GPUGraphicsPipeline *pipe, *alpha_pipe, *glow_pipe, *glowa_pipe, *spec_pipe, *dmg_pipe, *ring_pipe;   /* opaque meshes; translucent meshes (cloaking ships); glow and gloss maps (additive) */
static SDL_GPUTexture *depth; static Uint32 depth_w, depth_h;
void pl_draw_ui(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, Uint32 w, Uint32 h);     /* render_ui.c, drawn last */

/* per-frame state */
static SDL_GPUCommandBuffer *fcmd; static SDL_GPURenderPass *fpass;
static float view[16], vp[16];                                      /* row-major: view, then view * projection */
static float cam_aspect = 1.0f;                                     /* width / height of the last camera (set_camera) */
static int drawn_objects, drawn_tris;

/* effects: camera-facing quads binned by texture and blend kind (bin 0 is the additive soft procedural sprite); the bins are rebuilt every
   frame and packed into one vertex buffer, then drawn in the order they first appeared. Kinds: additive (one, one), alpha (source alpha, one
   minus source alpha), one / one minus source alpha (the sheets of ExplosionA), and plain alpha (no texture: lines). */
typedef struct { float pos[3], uv[2], col[4]; } FxVtx;
#define FX_MAX_VERTS 1500000
#define FX_BINS 40
#define PHASER_TILE 3.0f                                                /* world units per repeat of a phaser tube texture (assumed) */
enum { FXK_ADD, FXK_ALPHA, FXK_ONEINV, FXK_PLAIN, FXK_FLARE, FXK_BEAM, FXK_BACKDROP, FXK_PLAINADD };   /* FXK_PLAINADD: vertex colours only, source alpha / one, depth test (disruptor bolts) */   /* FXK_FLARE: source alpha, one; no depth test (lens flares) */
typedef struct { SDL_GPUTexture *tex; int kind, s, d, early; FxVtx *v; int n, cap, first; } FxBin;   /* s, d: the NetImmerse blend modes of a backdrop bin; early: drawn before the ships */
static SDL_GPUGraphicsPipeline *fx_pipe, *fx_tex_pipe, *fx_alpha_pipe, *fx_oneinv_pipe, *fx_plain_pipe, *fx_flare_pipe, *fx_beam_pipe, *fx_padd_pipe; static SDL_GPUBuffer *fx_buf; static SDL_GPUTransferBuffer *fx_tb;
static FxVtx *fx_v; static int fx_nv;
static FxBin fx_bins[FX_BINS]; static int fx_nbins = 1, fx_cur;
static SDL_GPUDevice *fx_dev;
static float cam_p[3], cam_f[3], cam_u[3], cam_r[3]; static float fx_pix_h = 720.0f;   /* window height in pixels (line widths) */
static float stars[2500][4];                                         /* direction and brightness */
static int fx_counts[3]; static int fx_off_beams, fx_off_shots, fx_off_spr, fx_off_patch;   /* QB_FXOFF=beams,shots,spr,patch: debugging switches */                                             /* beams, shots, particle sprites in the last frame */

static const char msl_src[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VIn { float3 pos [[attribute(0)]]; float3 nrm [[attribute(1)]]; float2 uv [[attribute(2)]]; };\n"
    "struct VOut { float4 pos [[position]]; float3 nrm; float2 uv; float a; float3 wpos; };\n"
    "struct VU { float4x4 mvp; float4x4 model; float4 cloak; float4 wo; };\n"
    "struct FU { float4 diffuse; float4 emissive; float4 light; float4 eye; float4 origin; float4 misc; float4 specc; float4 pl_pos[4]; float4 pl_col[4]; };\n"
    "float3 beam_lights(float3 n, float3 wp, constant FU &u) {\n"
    "    float3 sum = float3(0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        if (u.pl_col[i].a <= 0.0) continue;\n"
    "        float3 d = u.pl_pos[i].xyz - wp; float dist = max(length(d), 1e-3);\n"
    "        sum += u.pl_col[i].rgb * max(dot(n, d / dist), 0.0) / max(u.pl_pos[i].w * dist, 1e-3);\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "vertex VOut vs_main(VIn in [[stage_in]], constant VU &u [[buffer(0)]]) {\n"
    "    VOut o;\n"
    "    o.pos = u.mvp * float4(in.pos, 1.0);\n"
    "    o.nrm = (u.model * float4(in.nrm, 0.0)).xyz;\n"
    "    o.uv = in.uv;\n"
    "    o.wpos = (u.model * float4(in.pos, 0.0)).xyz * u.wo.w + u.wo.xyz;\n"
    "    o.a = 1.0;\n"
    "    if (u.cloak.y > 0.5) {\n"
    "        float r = fract(sin(dot(in.pos, float3(12.9898, 78.233, 37.719)) + u.cloak.w) * 43758.5453) * 0.4;\n"
    "        float v = u.cloak.x, f = 0.0;\n"
    "        if (u.cloak.y < 1.5) f = ((v <= 0.4) ? r : (v - 0.4 + r)) * v;\n"
    "        else if (u.cloak.y < 2.5) f = ((1.4 * v - 0.4) + r) * v;\n"
    "        o.a = saturate(u.cloak.z > 0.5 ? f + 0.3 : f);\n"
    "    }\n"
    "    return o;\n"
    "}\n"
    "fragment float4 fs_main(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float3 t = tex.sample(smp, in.uv).rgb;\n"
    "    float d = max(dot(normalize(in.nrm), normalize(u.light.xyz)), 0.0);\n"
    "    return float4(saturate(t * (u.diffuse.rgb * (0.3 + 0.7 * d + beam_lights(normalize(in.nrm), in.wpos, u)) + u.emissive.rgb)), 1.0);\n"
    "}\n"
    "fragment float4 fs_spec(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float3 g = tex.sample(smp, in.uv).rgb;\n"
    "    float3 n = normalize(in.nrm);\n"
    "    float3 h = normalize(normalize(u.light.xyz) + normalize(u.eye.xyz - u.origin.xyz));\n"
    "    float s = (u.misc.z > 0.0 && dot(n, normalize(u.light.xyz)) > 0.0) ? pow(max(dot(n, h), 0.0), u.misc.z) : 0.0;\n"
    "    return float4(g * u.specc.rgb * (s * u.misc.y), 1.0);\n"
    "}\n"
    "fragment float4 fs_glow(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float3 t = tex.sample(smp, in.uv).rgb;\n"
    "    return float4(t * u.misc.x, 1.0);\n"
    "}\n"
    "fragment float4 fs_glowa(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float4 t = tex.sample(smp, in.uv);\n"
    "    return float4(t.rgb * t.a * u.misc.x, 1.0);\n"
    "}\n"
    "fragment float4 fs_dmg(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float3 t = tex.sample(smp, in.uv).rgb;\n"
    "    float sh = length(in.nrm);\n"
    "    float3 nn = sh > 1e-4 ? in.nrm / sh : float3(0.0, 0.0, 1.0);\n"
    "    float d = max(dot(nn, normalize(u.light.xyz)), 0.0);\n"
    "    return float4(saturate(t * (u.diffuse.rgb * (0.3 + 0.7 * d + beam_lights(nn, in.wpos, u)) + u.emissive.rgb)) * min(sh, 1.0), 1.0);\n"
    "}\n"
    "fragment float4 fs_ring(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float4 t = tex.sample(smp, in.uv);\n"
    "    if (t.a < 0.5) discard_fragment();\n"
    "    return float4(t.rgb * 0.8, 1.0);\n"
    "}\n"
    "fragment float4 fs_alpha(VOut in [[stage_in]], constant FU &u [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float3 t = tex.sample(smp, in.uv).rgb;\n"
    "    float d = max(dot(normalize(in.nrm), normalize(u.light.xyz)), 0.0);\n"
    "    return float4(saturate(t * (u.diffuse.rgb * (0.3 + 0.7 * d) + u.emissive.rgb)), in.a);\n"
    "}\n";

static const char fx_src[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };\n"
    "struct VOut { float4 pos [[position]]; float2 uv; float4 col; };\n"
    "struct VU { float4x4 vp; };\n"
    "vertex VOut fx_vs(VIn in [[stage_in]], constant VU &u [[buffer(0)]]) {\n"
    "    VOut o; o.pos = u.vp * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; return o;\n"
    "}\n"
    "fragment float4 fx_fs(VOut in [[stage_in]]) {\n"
    "    float a = saturate(1.0 - length(in.uv));\n"
    "    a = a * a;\n"
    "    return float4(in.col.rgb * in.col.a * a, 1.0);\n"
    "}\n"
    "fragment float4 tx_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float4 t = tex.sample(smp, in.uv);\n"
    "    return float4(in.col.rgb * in.col.a * t.rgb * t.a, 1.0);\n"
    "}\n"
    "fragment float4 tb_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float4 t = tex.sample(smp, in.uv);\n"
    "    return float4(in.col.rgb * t.rgb, in.col.a * t.a);\n"
    "}\n"
    "fragment float4 pl_fs(VOut in [[stage_in]]) {\n"
    "    return in.col;\n"
    "}\n";

static SDL_GPUShader *shader_src(SDL_GPUDevice *dev, const char *src, const char *entry, SDL_GPUShaderStage stage, Uint32 uniforms, Uint32 samplers)
{
    SDL_GPUShaderCreateInfo si; SDL_zero(si);
    si.code = (const Uint8 *)src; si.code_size = strlen(src); si.entrypoint = entry;
    si.format = SDL_GPU_SHADERFORMAT_MSL; si.stage = stage; si.num_uniform_buffers = uniforms; si.num_samplers = samplers;
    return SDL_CreateGPUShader(dev, &si);
}

static SDL_GPUShader *shader(SDL_GPUDevice *dev, const char *entry, SDL_GPUShaderStage stage, Uint32 uniforms, Uint32 samplers)
{
    SDL_GPUShaderCreateInfo si; SDL_zero(si);
    si.code = (const Uint8 *)msl_src; si.code_size = strlen(msl_src); si.entrypoint = entry;
    si.format = SDL_GPU_SHADERFORMAT_MSL; si.stage = stage; si.num_uniform_buffers = uniforms; si.num_samplers = samplers;
    return SDL_CreateGPUShader(dev, &si);
}

static int ensure_pipeline(SDL_GPUDevice *dev, SDL_Window *win)
{
    SDL_GPUShader *vs, *fs; SDL_GPUVertexBufferDescription vb; SDL_GPUVertexAttribute va[3];
    SDL_GPUColorTargetDescription ctd; SDL_GPUGraphicsPipelineCreateInfo pi;
    if (pipe) return 1;
    vs = shader(dev, "vs_main", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    fs = shader(dev, "fs_main", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!vs || !fs) return 0;
    SDL_zero(vb); vb.slot = 0; vb.pitch = sizeof(Vtx); vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(va);
    va[0].location = 0; va[0].buffer_slot = 0; va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; va[0].offset = 0;
    va[1].location = 1; va[1].buffer_slot = 0; va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; va[1].offset = 12;
    va[2].location = 2; va[2].buffer_slot = 0; va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; va[2].offset = 24;
    SDL_zero(ctd); ctd.format = SDL_GetGPUSwapchainTextureFormat(dev, win);
    SDL_zero(pi);
    pi.vertex_shader = vs; pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = va; pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.depth_stencil_state.enable_depth_test = true; pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pi.target_info.color_target_descriptions = &ctd; pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; pi.target_info.has_depth_stencil_target = true;
    pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    {   /* hull damage (tg_damage.c): the hull with the scorch riding in the normals' length, and the charred band with the damage texture */
        SDL_GPUShader *fd = shader(dev, "fs_dmg", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1), *fr = shader(dev, "fs_ring", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        if (fd) { pi.fragment_shader = fd; dmg_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi); SDL_ReleaseGPUShader(dev, fd); }
        if (fr) { pi.fragment_shader = fr; ring_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi); SDL_ReleaseGPUShader(dev, fr); }
        pi.fragment_shader = fs;
    }
    /* the translucent pipeline: the same meshes with alpha blending, no depth write (the alpha is light.w) */
    {
        SDL_GPUShader *fa = shader(dev, "fs_alpha", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        if (fa) {
            ctd.blend_state.enable_blend = true;
            ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            pi.fragment_shader = fa; pi.depth_stencil_state.enable_depth_write = false;
            alpha_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
            SDL_ReleaseGPUShader(dev, fa);
        }
    }
    /* the glow pass: the glow map added on top (one, one), depth test on, no depth write; the gloss pass is the same with its own shader */
    {
        SDL_GPUShader *fg = shader(dev, "fs_glow", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1), *fp = shader(dev, "fs_spec", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1), *fga = shader(dev, "fs_glowa", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        if (fg) {
            ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            pi.fragment_shader = fg;
            glow_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
            SDL_ReleaseGPUShader(dev, fg);
        }
        if (fga) {
            pi.fragment_shader = fga;
            glowa_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
            SDL_ReleaseGPUShader(dev, fga);
        }
        if (fp) {
            pi.fragment_shader = fp;
            spec_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
            SDL_ReleaseGPUShader(dev, fp);
        }
    }
    SDL_ReleaseGPUShader(dev, vs); SDL_ReleaseGPUShader(dev, fs);
    return pipe != NULL && alpha_pipe != NULL && glow_pipe != NULL && glowa_pipe != NULL && spec_pipe != NULL && dmg_pipe != NULL && ring_pipe != NULL;
}

/* the effect pipelines: depth test on, depth write off; the blend differs by kind. Their vertex and transfer buffers and the star directions are made once. */
static SDL_GPUGraphicsPipeline *fx_make_pipe(SDL_GPUDevice *dev, SDL_Window *win, SDL_GPUShader *vs, SDL_GPUShader *fs, int kind)
{
    SDL_GPUVertexBufferDescription vb; SDL_GPUVertexAttribute va[3]; SDL_GPUColorTargetDescription ctd; SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_zero(vb); vb.slot = 0; vb.pitch = sizeof(FxVtx); vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(va);
    va[0].location = 0; va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; va[0].offset = 0;
    va[1].location = 1; va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; va[1].offset = 12;
    va[2].location = 2; va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; va[2].offset = 20;
    SDL_zero(ctd); ctd.format = SDL_GetGPUSwapchainTextureFormat(dev, win);
    ctd.blend_state.enable_blend = true;
    if (kind == FXK_ADD) { ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; }
    else if (kind == FXK_FLARE || kind == FXK_BEAM) { ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; }
    else if (kind == FXK_ONEINV) { ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; }
    else { ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; }
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_zero(pi);
    pi.vertex_shader = vs; pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = va; pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.depth_stencil_state.enable_depth_test = kind != FXK_FLARE; pi.depth_stencil_state.enable_depth_write = false;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pi.target_info.color_target_descriptions = &ctd; pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; pi.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(dev, &pi);
}
/* the pipeline of a backdrop bin: the blend modes a script names (0 one, 1 zero, 2 source colour, 3 one minus it, 4 destination colour, 5 one minus it,
   6 source alpha, 7 one minus source alpha), no depth test, no depth write. Made on first use. */
static SDL_GPUBlendFactor ni_factor(int m)
{
    switch (m) { case 1: return SDL_GPU_BLENDFACTOR_ZERO; case 2: return SDL_GPU_BLENDFACTOR_SRC_COLOR; case 3: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case 4: return SDL_GPU_BLENDFACTOR_DST_COLOR; case 5: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR; case 6: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case 7: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; default: return SDL_GPU_BLENDFACTOR_ONE; }
}
static SDL_GPUGraphicsPipeline *bd_pipes[8][8]; static SDL_GPUShader *bd_vs, *bd_fs; static SDL_GPUDevice *bd_dev; static SDL_Window *bd_win;
static SDL_GPUGraphicsPipeline *bd_pipe(int sm, int dm)
{
    SDL_GPUVertexBufferDescription vb; SDL_GPUVertexAttribute va[3]; SDL_GPUColorTargetDescription ctd; SDL_GPUGraphicsPipelineCreateInfo pi;
    if (sm < 0 || sm > 7) sm = 0; if (dm < 0 || dm > 7) dm = 0;
    if (bd_pipes[sm][dm]) return bd_pipes[sm][dm];
    SDL_zero(vb); vb.slot = 0; vb.pitch = sizeof(FxVtx); vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(va);
    va[0].location = 0; va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; va[0].offset = 0;
    va[1].location = 1; va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; va[1].offset = 12;
    va[2].location = 2; va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; va[2].offset = 20;
    SDL_zero(ctd); ctd.format = SDL_GetGPUSwapchainTextureFormat(bd_dev, bd_win);
    ctd.blend_state.enable_blend = true; ctd.blend_state.src_color_blendfactor = ni_factor(sm); ctd.blend_state.dst_color_blendfactor = ni_factor(dm);
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD; ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_zero(pi); pi.vertex_shader = bd_vs; pi.fragment_shader = bd_fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = va; pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.depth_stencil_state.enable_depth_test = false; pi.depth_stencil_state.enable_depth_write = false;
    pi.target_info.color_target_descriptions = &ctd; pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; pi.target_info.has_depth_stencil_target = true;
    return bd_pipes[sm][dm] = SDL_CreateGPUGraphicsPipeline(bd_dev, &pi);
}
static int ensure_fx(SDL_GPUDevice *dev, SDL_Window *win)
{
    SDL_GPUShader *vs, *fs, *ts, *tb, *pl; SDL_GPUBufferCreateInfo bi; SDL_GPUTransferBufferCreateInfo ti; int i; unsigned seed = 12345u;
    if (fx_pipe) return 1;
    vs = shader_src(dev, fx_src, "fx_vs", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    fs = shader_src(dev, fx_src, "fx_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    ts = shader_src(dev, fx_src, "tx_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    tb = shader_src(dev, fx_src, "tb_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    pl = shader_src(dev, fx_src, "pl_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vs || !fs || !ts || !tb || !pl) return 0;
    fx_pipe = fx_make_pipe(dev, win, vs, fs, FXK_ADD);
    fx_tex_pipe = fx_make_pipe(dev, win, vs, ts, FXK_ADD);
    fx_alpha_pipe = fx_make_pipe(dev, win, vs, tb, FXK_ALPHA);
    fx_oneinv_pipe = fx_make_pipe(dev, win, vs, tb, FXK_ONEINV);
    fx_plain_pipe = fx_make_pipe(dev, win, vs, pl, FXK_ALPHA);
    fx_flare_pipe = fx_make_pipe(dev, win, vs, tb, FXK_FLARE);
    fx_beam_pipe = fx_make_pipe(dev, win, vs, tb, FXK_BEAM);
    fx_padd_pipe = fx_make_pipe(dev, win, vs, pl, FXK_BEAM);
    bd_vs = vs; bd_fs = tb; bd_dev = dev; bd_win = win;                 /* kept for the backdrop pipelines */
    SDL_ReleaseGPUShader(dev, fs); SDL_ReleaseGPUShader(dev, ts); SDL_ReleaseGPUShader(dev, pl);
    SDL_zero(bi); bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX; bi.size = sizeof(FxVtx) * FX_MAX_VERTS;
    fx_buf = SDL_CreateGPUBuffer(dev, &bi);
    SDL_zero(ti); ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = sizeof(FxVtx) * FX_MAX_VERTS;
    fx_tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    fx_v = malloc(sizeof(FxVtx) * FX_MAX_VERTS);
    for (i = 0; i < 2500; i++) {                                     /* uniform directions (fixed seed), brightness skewed to dim */
        float z, t, rr, b;
        seed = seed * 1103515245u + 12345u; z = (float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
        seed = seed * 1103515245u + 12345u; t = (float)(seed >> 8) / 16777216.0f * 6.2831853f;
        seed = seed * 1103515245u + 12345u; b = (float)(seed >> 8) / 16777216.0f;
        rr = sqrtf(1.0f - z * z); stars[i][0] = rr * cosf(t); stars[i][1] = rr * sinf(t); stars[i][2] = z; stars[i][3] = 0.25f + 0.75f * b * b * b;
    }
    return fx_pipe && fx_tex_pipe && fx_alpha_pipe && fx_oneinv_pipe && fx_plain_pipe && fx_flare_pipe && fx_beam_pipe && fx_padd_pipe && fx_buf && fx_tb && fx_v;
}

/* the bins: each frame starts empty. Bin 0 is the soft sprite (no texture, additive); another bin is made for each texture and blend kind on first use. */
static void fx_reset(void)
{
    int i; fx_nv = 0;
    for (i = 0; i < fx_nbins; i++) fx_bins[i].n = 0;
    fx_cur = 0;
}
static int fx_bin_of_kind(SDL_GPUTexture *t, int kind)
{
    int i;
    if (!t && kind == FXK_ADD) return 0;
    for (i = 1; i < fx_nbins; i++) if (fx_bins[i].tex == t && fx_bins[i].kind == kind) return i;
    if (fx_nbins >= FX_BINS) return 0;
    fx_bins[fx_nbins].tex = t; fx_bins[fx_nbins].kind = kind; return fx_nbins++;
}
static int fx_bin_of(SDL_GPUTexture *t) { return fx_bin_of_kind(t, FXK_ADD); }
static int fx_bin_backdrop(SDL_GPUTexture *t, int sm, int dm)
{
    int i;
    for (i = 1; i < fx_nbins; i++) if (fx_bins[i].kind == FXK_BACKDROP && fx_bins[i].tex == t && fx_bins[i].s == sm && fx_bins[i].d == dm) return i;
    if (fx_nbins >= FX_BINS) return 0;
    fx_bins[fx_nbins].tex = t; fx_bins[fx_nbins].kind = FXK_BACKDROP; fx_bins[fx_nbins].s = sm; fx_bins[fx_nbins].d = dm; fx_bins[fx_nbins].early = 1; return fx_nbins++;
}
/* the bin kind for the NetImmerse alpha modes a script names (0 one, 1 zero, 6 source alpha, 7 one minus source alpha): the sheets of ExplosionA
   are (one, one minus source alpha); anything else is standard alpha blending */
static int fx_kind_of_modes(int s, int d) { return (s == 0 && d == 7) ? FXK_ONEINV : (s == 0 && d == 0) ? FXK_ADD : FXK_ALPHA; }
/* a stock texture by its data path; NULL when the name is missing or the file does not decode (the soft sprite is used instead) */
SDL_GPUTexture *pl_texture_named(SDL_GPUDevice *dev, const char *name, int *w, int *h);
static SDL_GPUTexture *fx_tex(const char *name)
{
    int w = 1, h = 1; SDL_GPUTexture *t;
    if (!name || !*name || !fx_dev) return NULL;
    t = pl_texture_named(fx_dev, name, &w, &h);
    return (w > 1 || h > 1) ? t : NULL;
}
/* the next vertex in the current bin (NULL when the bin is full) */
static FxVtx *fx_next(void)
{
    FxBin *b = &fx_bins[fx_cur];
    if (b->n >= FX_MAX_VERTS) return NULL;
    if (b->n == b->cap) {
        int cap = b->cap ? b->cap * 2 : 4096; FxVtx *v = realloc(b->v, sizeof(FxVtx) * (size_t)cap);
        if (!v) return NULL;
        b->v = v; b->cap = cap;
    }
    return &b->v[b->n++];
}
static const float uv_full[4] = {0, 0, 1, 1};

/* a quad as two triangles: centre c, half axes x and y. uv is the texture rectangle (u0 v0 u1 v1); NULL uses the soft sprite (corners -1..1) */
static void fx_quad(const float *c, const float *x, const float *y, const float *col, const float *uv)
{
    static const float cu[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    int k, i;
    for (k = 0; k < 6; k++) {
        FxVtx *v = fx_next(); if (!v) return;
        for (i = 0; i < 3; i++) v->pos[i] = c[i] + x[i] * cu[k][0] + y[i] * cu[k][1];
        if (uv) { v->uv[0] = uv[0] + (cu[k][0] + 1) * 0.5f * (uv[2] - uv[0]); v->uv[1] = uv[3] + (cu[k][1] + 1) * 0.5f * (uv[1] - uv[3]); }   /* v runs down the picture, the quad's lower corners take the larger v */
        else { v->uv[0] = cu[k][0]; v->uv[1] = cu[k][1]; }
        memcpy(v->col, col, 16);
    }
}
static void fx_sprite(const float *c, float size, const float *col, const float *uv)    /* facing the camera */
{
    float x[3], y[3]; int i; for (i = 0; i < 3; i++) { x[i] = cam_r[i] * size; y[i] = cam_u[i] * size; } fx_quad(c, x, y, col, uv);
}
/* a stock texture sprite: its own bin; the soft sprite when the texture is missing */
static void fx_sprite_tex(const char *name, const float *c, float size, const float *col)
{
    SDL_GPUTexture *t = fx_tex(name);
    fx_cur = fx_bin_of(t); fx_sprite(c, size, col, t ? uv_full : NULL); fx_cur = 0;
}
/* a line from a to b of half width w, turned toward the camera; the uv runs across the line only, so the falloff is sideways */
static void fx_line(const float *a, const float *b, float w, const float *col)
{
    float d[3], e[3], s[3], l; int i, k;
    static const float cu[6][2] = {{-1, 0}, {1, 0}, {1, 1}, {-1, 0}, {1, 1}, {-1, 1}};
    for (i = 0; i < 3; i++) { d[i] = b[i] - a[i]; e[i] = (a[i] + b[i]) * 0.5f - cam_p[i]; }
    s[0] = d[1] * e[2] - d[2] * e[1]; s[1] = d[2] * e[0] - d[0] * e[2]; s[2] = d[0] * e[1] - d[1] * e[0];
    l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-6f) return;
    for (i = 0; i < 3; i++) s[i] *= w / l;
    for (k = 0; k < 6; k++) {
        FxVtx *v = fx_next(); const float *p = cu[k][1] > 0 ? b : a; if (!v) return;
        for (i = 0; i < 3; i++) v->pos[i] = p[i] + s[i] * cu[k][0];
        v->uv[0] = cu[k][0]; v->uv[1] = 0; memcpy(v->col, col, 16);
    }
}
/* a textured tube from a to b: u runs across the tube (0 to 1), v along it, one repeat every tile units. The phaser textures are opaque
   (24-bit TGA), so the tube fades to its edges with a vertex alpha that peaks at the centre line (a triangle profile, interpolated). */
static void fx_tube(const float *a, const float *b, float w, const float *col, float tile)
{
    float d[3], e[3], s[3], l, along; int i, seg, t, j;
    static const float across[3] = {-1, 0, 1};
    static const int corner[2][3][2] = {{{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {1, 1}, {0, 1}}};   /* two triangles: (across index, end 0 = a, 1 = b) */
    for (i = 0; i < 3; i++) { d[i] = b[i] - a[i]; e[i] = (a[i] + b[i]) * 0.5f - cam_p[i]; }
    s[0] = d[1] * e[2] - d[2] * e[1]; s[1] = d[2] * e[0] - d[0] * e[2]; s[2] = d[0] * e[1] - d[1] * e[0];
    l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-6f) return;
    for (i = 0; i < 3; i++) s[i] *= w / l;
    along = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / (tile > 0 ? tile : 1.0f);
    for (seg = 0; seg < 2; seg++) {                                  /* the left half, then the right half */
        for (t = 0; t < 2; t++) for (j = 0; j < 3; j++) {
            const float *p = corner[t][j][1] ? b : a; float ax = across[seg + corner[t][j][0]]; FxVtx *v = fx_next();
            if (!v) return;
            for (i = 0; i < 3; i++) v->pos[i] = p[i] + s[i] * ax;
            v->uv[0] = (ax + 1) * 0.5f; v->uv[1] = corner[t][j][1] ? along : 0;
            memcpy(v->col, col, 16); v->col[3] = col[3] * (1.0f - (ax < 0 ? -ax : ax));
        }
    }
}
/* a spark's tail: a line a to b about one pixel wide (at the distance of its middle) with a colour at each end, alpha blended */
static void fx_line2(const float *a, const float *b, const float *ca, const float *cb)
{
    float d[3], e[3], s[3], l, dist, w; int i, k;
    static const float cu[6][2] = {{-1, 0}, {1, 0}, {1, 1}, {-1, 0}, {1, 1}, {-1, 1}};
    for (i = 0; i < 3; i++) { d[i] = b[i] - a[i]; e[i] = (a[i] + b[i]) * 0.5f - cam_p[i]; }
    s[0] = d[1] * e[2] - d[2] * e[1]; s[1] = d[2] * e[0] - d[0] * e[2]; s[2] = d[0] * e[1] - d[1] * e[0];
    l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-9f) return;
    dist = e[0] * cam_f[0] + e[1] * cam_f[1] + e[2] * cam_f[2]; if (dist < 0.01f) dist = 0.01f;
    w = 0.75f * dist * 2.0f * 0.57735027f / fx_pix_h;               /* half a pixel and a half wide: the field of view is 60 degrees */
    for (i = 0; i < 3; i++) s[i] *= w / l;
    for (k = 0; k < 6; k++) {
        FxVtx *v = fx_next(); const float *p = cu[k][1] > 0 ? b : a; if (!v) return;
        for (i = 0; i < 3; i++) v->pos[i] = p[i] + s[i] * cu[k][0];
        v->uv[0] = 0; v->uv[1] = 0; memcpy(v->col, cu[k][1] > 0 ? cb : ca, 16);
    }
}
/* the shield hit patch (FUN_004ea690, FUN_004ea4a0, FUN_004ea3e0): a cap of the ship's enclosing ellipsoid, turned toward the hit direction. Polar angle
   (0.4 pi to 0.5 pi plus 0.4 pi) and longitude (plus or minus 0.4 pi) in a grid of PATCH_RES points; the texture runs over the grid; the vertex colour
   is the patch colour with a new random alpha for each vertex every frame times the fade. The grid is 128 by 128 as in the exe (detail level 3). */
#define PATCH_RES 128
static unsigned patch_rng;
static float patch_rand(void) { patch_rng = patch_rng * 1664525u + 1013904223u; return (float)(patch_rng >> 8) / 16777216.0f; }
static void fx_patch(const TgFxShieldPatch *q)
{
    static FxVtx grid[PATCH_RES * PATCH_RES]; int i, j, k; float d[3], ax[3] = {0, 0, 0}, u[3], v[3], l;
    const float C1 = 1.5707963f, C2 = 6.2831853f, C3 = 3.1415927f, size = 0.8f;
    float lat0 = C1 - C3 * size * 0.5f, lat_step = C3 * size / (float)(PATCH_RES - 1), lon0 = -(size * 0.5f * C2 * 0.5f), lon_step = size * 0.5f * C2 / (float)(PATCH_RES - 1);
    SDL_GPUTexture *t = fx_tex("data/sphere.tga");
    memcpy(d, q->dir, 12);
    if (fabsf(d[1]) <= fabsf(d[0])) { if (fabsf(d[2]) < fabsf(d[1])) ax[2] = 1.0f; else ax[1] = 1.0f; } else { if (fabsf(d[0]) <= fabsf(d[2])) ax[0] = 1.0f; else ax[2] = 1.0f; }
    u[0] = d[1] * ax[2] - d[2] * ax[1]; u[1] = d[2] * ax[0] - d[0] * ax[2]; u[2] = d[0] * ax[1] - d[1] * ax[0];
    l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (l > 1e-6f) for (k = 0; k < 3; k++) u[k] /= l;
    v[0] = u[1] * d[2] - u[2] * d[1]; v[1] = u[2] * d[0] - u[0] * d[2]; v[2] = u[0] * d[1] - u[1] * d[0];
    patch_rng = q->seed * 2654435761u + (unsigned)(SDL_GetTicks() * 7u);
    for (j = 0; j < PATCH_RES; j++) {
        float th = lat0 + (float)j * lat_step, sz = sinf(th), cz = cosf(th);
        for (i = 0; i < PATCH_RES; i++) {
            float lon = lon0 + (float)i * lon_step, px = sinf(lon) * sz, py = cosf(lon) * sz, pz = cz, m[3], e[3]; FxVtx *g = &grid[j * PATCH_RES + i];
            for (k = 0; k < 3; k++) m[k] = u[k] * px + d[k] * py + v[k] * pz;                          /* turn the cap toward the hit */
            for (k = 0; k < 3; k++) e[k] = m[k] * q->radii[k] + q->centre[k];                           /* scale to the ellipsoid, move to the box centre */
            for (k = 0; k < 3; k++) g->pos[k] = q->p[k] + q->scale * (e[0] * q->r[k] + e[1] * q->f[k] + e[2] * q->u[k]);
            g->uv[0] = (float)i / (float)(PATCH_RES - 1); g->uv[1] = (float)j / (float)(PATCH_RES - 1);
            g->col[0] = q->rgba[0]; g->col[1] = q->rgba[1]; g->col[2] = q->rgba[2]; g->col[3] = patch_rand() * q->fade * q->rgba[3];
        }
    }
    fx_cur = fx_bin_of_kind(t, t ? FXK_ALPHA : FXK_PLAIN);
    for (j = 1; j < PATCH_RES; j++) for (i = 1; i < PATCH_RES; i++) {
        int a = j * PATCH_RES + i, tri[6] = {a, a - PATCH_RES, a - 1, a - PATCH_RES, a - PATCH_RES - 1, a - 1};
        for (k = 0; k < 6; k++) { FxVtx *o = fx_next(); if (!o) { fx_cur = 0; return; } *o = grid[tri[k]]; }
    }
    fx_cur = 0;
}
/* a phaser beam as the exe builds it (PhaserBank::CreateBeam, initPhaserBeamMesh 004e40a0, FUN_004e5040): a half tube that opens toward the camera, 6 rows
   along the beam (tapers at both ends) by `sides` columns, twice: the shell and, scaled by CoreScale, the core. Widths: TaperRadius at the ends, MainRadius
   (times a factor for the distance to the camera: 0.5 to 4) inside. u runs along the beam (LengthTextureTilePerUnit, scrolling at TextureSpeed), v around
   (PerimeterTile). Vertex colour: the outer colour at the edges to the inner colour in the middle; alpha (f * sin(angle))^2 with f from 0.7 (seen from the
   side) to 1 (end on). Blend: source alpha, one; depth test, no write. The beam grows from the emitter in 0.35 s. Not done: the two point lights, the sweep
   of the start point (FUN_004e7200), the texture shift with the viewing angle. */
static float beam_dist_factor(float d)
{
    float f;
    if (d >= 10.0f) { f = 1.0f; if (d > 100.0f) { f = d * 0.01f; if (f > 4.0f) f = 4.0f; } }
    else { f = d * 0.1f; if (f < 0.5f) f = 0.5f; }
    return f;
}
static void fx_beam(const TgFxBeam *b, SDL_GPUTexture *t)
{
    float d[3], len, L, dir[3], taper, y[6], w[6], P[3], S[3], e[3], cf, grow = 0.35f; int i, k, r, blk, n = b->sides < 3 ? 3 : b->sides > 24 ? 24 : b->sides, grown;
    static FxVtx grid[2][6 * 24];
    for (i = 0; i < 3; i++) d[i] = b->b[i] - b->a[i];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (len < 1e-5f) return;
    for (i = 0; i < 3; i++) dir[i] = d[i] / len;
    grown = b->age >= grow; L = len * (grown ? 1.0f : b->age / grow); if (L < 1e-4f) return;
    taper = L * 0.5f * b->taper_ratio; if (taper < b->taper_min) taper = b->taper_min; if (taper > b->taper_max) taper = b->taper_max;
    y[0] = 0.0f; y[1] = taper * 0.5f; y[2] = taper;
    if (!grown) { if (L < taper + taper) taper = L * 0.35f; y[1] = taper * 0.5f; y[2] = taper; y[3] = (L - 2.0f * taper) + taper; y[4] = taper * 1.5f + (L - 2.0f * taper); y[5] = L; }
    else { if (L < taper) taper = L * 0.8f; y[1] = taper * 0.5f; y[2] = taper; y[3] = y[4] = y[5] = L; }
    {   float m[3], dm, d2, f2, f3, mid = taper;                       /* the widths: the distance factor at the end of the first taper and at row 3 */
        for (i = 0; i < 3; i++) { m[i] = b->a[i] + dir[i] * mid - cam_p[i]; } dm = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) / b->scale;
        for (i = 0; i < 3; i++) { m[i] = b->a[i] + dir[i] * y[3] - cam_p[i]; } d2 = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) / b->scale;
        f2 = b->radius * beam_dist_factor(dm); f3 = b->radius * beam_dist_factor(d2);
        w[0] = b->taper_r; w[2] = f2; w[1] = f2 - (f2 - w[0]) * 0.25f; w[3] = f3; w[4] = f3 - (f3 - w[0]) * 0.25f; w[5] = w[0]; }
    for (i = 0; i < 3; i++) e[i] = cam_p[i] - b->a[i];                 /* P: from the beam toward the camera, across it; S: the other side */
    { float al = e[0] * dir[0] + e[1] * dir[1] + e[2] * dir[2]; for (i = 0; i < 3; i++) P[i] = e[i] - dir[i] * al; }
    { float pl = sqrtf(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]); if (pl < 1e-6f) return; for (i = 0; i < 3; i++) P[i] /= pl; }
    S[0] = dir[1] * P[2] - dir[2] * P[1]; S[1] = dir[2] * P[0] - dir[0] * P[2]; S[2] = dir[0] * P[1] - dir[1] * P[0];
    cf = fabsf(cam_f[0] * dir[0] + cam_f[1] * dir[1] + cam_f[2] * dir[2]); cf = 0.7f + 0.3f * cf;
    for (blk = 0; blk < 2; blk++) {
        const float *c0 = b->col[blk * 2], *c1 = b->col[blk * 2 + 1];
        for (r = 0; r < 6; r++) for (k = 0; k < n; k++) {
            float th = 3.14159265f * (float)k / (float)(n - 1), s = sinf(th), c = cosf(th), ww = w[r] * (blk ? b->core_scale : 1.0f), tq = (float)(k < n - 1 - k ? k : n - 1 - k) / (0.5f * (float)(n - 1)), a = cf * s;
            FxVtx *g = &grid[blk][r * 24 + k]; int j;
            for (j = 0; j < 3; j++) g->pos[j] = b->a[j] + dir[j] * y[r] + ww * (c * S[j] + s * P[j]);
            g->uv[0] = y[r] / b->scale * b->ltile + b->age * b->speed; g->uv[1] = (float)k / (float)(n - 1) * b->ptile;
            for (j = 0; j < 3; j++) g->col[j] = c0[j] + (c1[j] - c0[j]) * tq;
            g->col[3] = a * a;
        }
    }
    fx_cur = fx_bin_of_kind(t, FXK_BEAM);
    for (blk = 0; blk < 2; blk++) for (r = 0; r < 5; r++) for (k = 0; k < n - 1; k++) {
        int q[6] = {r * 24 + k, (r + 1) * 24 + k, r * 24 + k + 1, (r + 1) * 24 + k, (r + 1) * 24 + k + 1, r * 24 + k + 1};
        for (i = 0; i < 6; i++) { FxVtx *o = fx_next(); if (!o) { fx_cur = 0; return; } *o = grid[blk][q[i]]; }
    }
    fx_cur = 0;
}
/* a backdrop sphere (BackdropSphere::BuildGeometry 0058db00, StarSphere::BuildGeometry 0058e520; tg_backdrop.c): a grid around the camera, the object's axes
   x right, y forward, z up; u with the longitude, v with the latitude, times the tiles */
static void fx_backdrop(const TgFxBackdrop *b)
{
    int rows = (int)sqrtf((float)(b->poly > 16 ? b->poly : 16) / 4.0f) + 1, cols = 2 * (rows - 1) + 1, i, j, k; const float PI = 3.14159265f;
    float lat0 = b->star ? 0.0f : PI * 0.5f - PI * b->vspan * 0.5f, lat_step = (b->star ? PI : PI * b->vspan) / (float)(rows - 1);
    float lon0 = b->star ? -PI : -(2.0f * PI * b->hspan * 0.5f), lon_step = (b->star ? 2.0f * PI : 2.0f * PI * b->hspan) / (float)(cols - 1);
    static FxVtx grid[64 * 128]; SDL_GPUTexture *t = fx_tex(b->tex);
    if (!t || rows > 64 || cols > 128) return;
    for (j = 0; j < rows; j++) for (i = 0; i < cols; i++) {
        float th = lat0 + (float)j * lat_step, lon = lon0 + (float)i * lon_step, x = sinf(lon) * sinf(th), y = cosf(lon) * sinf(th), z = cosf(th); FxVtx *g = &grid[j * cols + i];
        for (k = 0; k < 3; k++) g->pos[k] = cam_p[k] + b->radius * (x * b->r[k] + y * b->f[k] + z * b->u[k]);
        g->uv[0] = (float)i / (float)(cols - 1) * b->htile; g->uv[1] = (float)j / (float)(rows - 1) * (b->star ? 0.5f : 1.0f) * b->vtile;
        g->col[0] = g->col[1] = g->col[2] = g->col[3] = 1.0f;
    }
    fx_cur = fx_bin_backdrop(t, b->sblend, b->dblend);
    for (j = 1; j < rows; j++) for (i = 1; i < cols; i++) {
        int a = j * cols + i, tri[6] = {a, a - cols, a - 1, a - cols, a - cols - 1, a - 1};
        for (k = 0; k < 6; k++) { FxVtx *o = fx_next(); if (!o) { fx_cur = 0; return; } *o = grid[tri[k]]; }
    }
    fx_cur = 0;
}

/* one vertex of the current bin */
static void fx_vtx(const float *pos, float u, float v, const float *col)
{
    FxVtx *o = fx_next(); if (!o) return;
    memcpy(o->pos, pos, 12); o->uv[0] = u; o->uv[1] = v; memcpy(o->col, col, 16);
}
/* a quad given by its four corners (uv (0,0) (0,1) (1,1) (1,0), v down the picture) */
static void fx_quad4(const float v[4][3], const float *col)
{
    static const float uv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}}; static const int tri[6] = {0, 1, 2, 0, 2, 3}; int k;
    for (k = 0; k < 6; k++) fx_vtx(v[tri[k]], uv[tri[k]][0], uv[tri[k]][1], col);
}
/* The disruptor bolt (FUN_004dd250 builds it as a TGFuzzyTriShape): an outer shell and an inner core, each a spindle of `sides` columns. Ring k sits at
   y = L * ringpos[k] - L/2 with radius W * ringrad[k]; the tips are at y = +L/2 (front) and -L/2 (back). The core has radius in_r and length in_l times
   those, kept at the front tip. Vertex colours only, source alpha / one, depth test on, no depth write; the back faces are dropped (NetImmerse culls them).
   Triangles as the exe's index lists: front fan (tip, ring0[j+1], ring0[j]), quads (r[k-1][j], r[k-1][j+1], r[k][j+1]) and (r[k][j], r[k-1][j], r[k][j+1]),
   back fan (back tip, r3[s-1-(j+1)%s], r3[s-1-j]). */
static void fx_disruptor(const TgFxShot *t)
{
    static const float ringpos[4] = {0.9927273f, 0.9727273f, 0.9272727f, 0.7272727f}, ringrad[4] = {0.3846154f, 0.7692308f, 1.0f, 0.6538462f};
    int s = t->sides > 2 ? t->sides : 3, layer, j, k, m, i; float L = t->len, W = t->width; static float P[2][4 * 64 + 2][3];
    if (s > 64) s = 64;
    fx_cur = fx_bin_of_kind(NULL, FXK_PLAINADD);
    for (layer = 0; layer < 2; layer++) {
        float rs = layer ? t->in_r : 1.0f, ls = layer ? t->in_l : 1.0f, anchor = (1.0f - ls) * L * 0.5f; const float *col = layer ? t->core : t->shell;
        float (*Q)[3] = P[layer];                                        /* 0 front tip, 1 back tip, 2 + k * s + j the rings */
        #define LOC(out, x, y, z) for (m = 0; m < 3; m++) (out)[m] = t->p[m] + (x) * t->r[m] + (y) * t->f[m] + (z) * t->u[m]
        LOC(Q[0], 0.0f, L * 0.5f, 0.0f); LOC(Q[1], 0.0f, -L * 0.5f * ls + anchor, 0.0f);
        for (k = 0; k < 4; k++) for (j = 0; j < s; j++) {
            float a = 6.2831853f * (float)j / (float)s, rad = W * ringrad[k] * rs, y = (L * ringpos[k] - L * 0.5f) * ls + anchor;
            LOC(Q[2 + k * s + j], cosf(a) * rad, y, sinf(a) * rad);
        }
        #undef LOC
        for (j = 0; j < s; j++) {
            int j1 = (j + 1) % s, tri[3 * 2 * 3 + 6][3]; int n = 0;
            tri[n][0] = 0; tri[n][1] = 2 + j1; tri[n][2] = 2 + j; n++;
            for (k = 1; k < 4; k++) {
                int a = 2 + (k - 1) * s + j, b = 2 + (k - 1) * s + j1, c = 2 + k * s + j1, d = 2 + k * s + j;
                tri[n][0] = a; tri[n][1] = b; tri[n][2] = c; n++; tri[n][0] = d; tri[n][1] = a; tri[n][2] = c; n++;
            }
            tri[n][0] = 1; tri[n][1] = 2 + 3 * s + (s - 1 - j1); tri[n][2] = 2 + 3 * s + (s - 1 - j); n++;
            for (i = 0; i < n; i++) {
                const float *A = Q[tri[i][0]], *B = Q[tri[i][1]], *C = Q[tri[i][2]]; float ab[3], ac[3], nn[3], d = 0;
                for (m = 0; m < 3; m++) { ab[m] = B[m] - A[m]; ac[m] = C[m] - A[m]; }
                nn[0] = ab[1] * ac[2] - ab[2] * ac[1]; nn[1] = ab[2] * ac[0] - ab[0] * ac[2]; nn[2] = ab[0] * ac[1] - ab[1] * ac[0];
                for (m = 0; m < 3; m++) d += nn[m] * (cam_p[m] - A[m]);
                if (d <= 0.0f) continue;                                 /* a back face */
                fx_vtx(A, 0, 0, col); fx_vtx(B, 0, 0, col); fx_vtx(C, 0, 0, col);
            }
        }
    }
    fx_cur = 0;
}
/* The end glows of a phaser beam (initPhaserLightQuad 004e60c0, FUN_004e6e90, FUN_004e75b0): a camera-facing node with two quads, 1.5 and 1.0 units wide before
   the node's scale; the outer takes the beam's second colour (+0xe4), the inner its fourth (+0x104). The scale is the beam's MainRadius at the start of the
   beam and 4.5 times it at the hit point. Source alpha / one with no depth test. The alpha of every vertex is 1 minus an occlusion value that a ray from the
   camera to the node drives: up while something blocks it, down otherwise, 5 a second (FUN_004e6810). */
typedef struct { const void *key; int end; float occ; int init; double seen; } GlowState;
extern int tg_fx_blocked(void *set, const float *a, const float *b);
static float fx_dt;                                                      /* seconds since the previous effect build (real time, at most 0.1) */
static void fx_beam_glow(void *set, const TgFxBeam *b, int end, SDL_GPUTexture *tex)
{
    static GlowState tab[128]; GlowState *g = NULL; int i, k, layer; double t = (double)SDL_GetTicks(); float c[3], q[4][3], col[4], blocked, to[3], R;
    const float *pos = end ? b->b : b->a;
    for (i = 0; i < 128; i++) if (tab[i].key == b->key && tab[i].end == end && t - tab[i].seen < 500.0) { g = &tab[i]; break; }
    if (!g) {
        for (i = 0; i < 128; i++) if (!tab[i].key || t - tab[i].seen >= 500.0) { g = &tab[i]; break; }
        if (!g) g = &tab[0];
        g->key = b->key; g->end = end; g->init = 0;
    }
    g->seen = t;
    for (k = 0; k < 3; k++) to[k] = cam_p[k] + (pos[k] - cam_p[k]) * 0.995f;       /* stop just short of the point (it lies on a hull) */
    blocked = tg_fx_blocked(set, cam_p, to) ? 1.0f : 0.0f;
    if (!g->init) { g->occ = blocked; g->init = 1; }
    else { g->occ += (blocked > 0.5f ? 1.0f : -1.0f) * 5.0f * fx_dt; if (g->occ < 0.0f) g->occ = 0.0f; if (g->occ > 1.0f) g->occ = 1.0f; }
    if (g->occ >= 1.0f) return;
    for (k = 0; k < 3; k++) c[k] = pos[k];
    R = b->radius * (end ? 4.5f : 1.0f);
    fx_cur = fx_bin_of_kind(tex, tex ? FXK_FLARE : FXK_ADD);
    for (layer = 0; layer < 2; layer++) {
        float h = (layer ? 0.5f : 0.75f) * R; memcpy(col, b->col[layer ? 3 : 1], 12); col[3] = 1.0f - g->occ;
        for (k = 0; k < 3; k++) {
            q[0][k] = c[k] - cam_r[k] * h + cam_u[k] * h; q[1][k] = c[k] - cam_r[k] * h - cam_u[k] * h;
            q[2][k] = c[k] + cam_r[k] * h - cam_u[k] * h; q[3][k] = c[k] + cam_r[k] * h + cam_u[k] * h;
        }
        if (tex) fx_quad4((const float (*)[3])q, col); else fx_sprite(c, h, col, NULL);
    }
    fx_cur = 0;
}
/* the bin for a tube or sprite texture: a textured line when the texture loaded, the procedural line otherwise */
static TgFxBeam beams[256]; static int fx_nbeams;                       /* this frame's beams (their lights shine on the firing ships) */
static void fx_build(void *set)
{
    static TgFxBackdrop bds[16]; int nbd; static TgFxShot shots[512]; static TgFxLens lenses[8]; static TgFxSprite sprites[4096];
    int nb, ns, ne, nl, np, i, k; float c[4], p[3];
    fx_reset();
    nbd = tg_fx_backdrops(set, bds, 16);                             /* the set's own star and nebula spheres, drawn first */
    for (i = 0; i < nbd; i++) fx_backdrop(&bds[i]);
    for (i = 0; i < (nbd ? 0 : 2500); i++) {                         /* no backdrops in the set: a field of fixed random stars, 50,000 units out */
        float b = stars[i][3]; c[0] = b * 0.9f; c[1] = b * 0.92f; c[2] = b; c[3] = 1.0f;
        for (k = 0; k < 3; k++) p[k] = cam_p[k] + stars[i][k] * 50000.0f;
        fx_sprite(p, 50000.0f * 0.0025f, c, NULL);
    }
    { const char *off = getenv("QB_FXOFF"); fx_off_beams = off && strstr(off, "beams"); fx_off_shots = off && strstr(off, "shots"); fx_off_spr = off && strstr(off, "spr"); fx_off_patch = off && strstr(off, "patch"); }
    nb = fx_off_beams ? 0 : tg_fx_beams(set, beams, 256); fx_nbeams = nb;
    { static Uint64 last_t; Uint64 now = SDL_GetTicks(); fx_dt = last_t ? (float)(now - last_t) * 0.001f : 0.0f; if (fx_dt > 0.1f) fx_dt = 0.1f; last_t = now; }
    for (i = 0; i < nb; i++) {
        TgFxBeam *b = &beams[i]; SDL_GPUTexture *t = fx_tex(b->tex), *gl = fx_tex("data/Textures/Tactical/PhaserLights.tga");
        if (!t) t = fx_tex("data/phaser.tga");
        if (t) fx_beam(b, t);
        fx_beam_glow(set, b, 0, gl); fx_beam_glow(set, b, 1, gl);        /* the glow nodes at both ends (see fx_beam_glow) */
    }
    ns = fx_off_shots ? 0 : tg_fx_shots(set, shots, 512);
    if (getenv("QB_FXTEST")) {                                         /* debug: a disruptor bolt and a torpedo in front of the camera */
        TgFxShot *t = &shots[ns++]; int q; memset(t, 0, sizeof *t); t->kind = 2; t->sides = 12; t->len = 2.0f; t->width = 0.2f; t->in_r = 0.5f; t->in_l = 0.8f;
        for (q = 0; q < 3; q++) { t->p[q] = cam_p[q] + cam_f[q] * 6.0f - cam_r[q] * 1.5f; t->f[q] = cam_r[q]; t->u[q] = cam_u[q]; t->r[q] = -cam_f[q]; }
        t->shell[0] = 0.01f; t->shell[1] = 1.0f; t->shell[2] = 0.01f; t->shell[3] = 1.0f; t->core[0] = 0.64f; t->core[1] = 1.0f; t->core[2] = 0.64f; t->core[3] = 1.0f;
    }
    for (i = 0; i < ns; i++) {
        TgFxShot *t = &shots[i]; float sh[4], co[4];
        memcpy(sh, t->shell, 16); memcpy(co, t->core, 16);
        if (t->kind == 2) fx_disruptor(t);                         /* the spindle with its core */
        else if (t->kind == 1) {                                   /* torpedo: the glow quad (material alpha 0.8) then the core quad, source alpha / one minus source alpha */
            SDL_GPUTexture *gt = fx_tex(t->glow_tex), *ct = fx_tex(t->core_tex);
            sh[3] = 0.8f; co[3] = 1.0f;
            if (gt) { fx_cur = fx_bin_of_kind(gt, FXK_ALPHA); fx_sprite(t->p, t->glow_size * 0.5f, sh, uv_full); fx_cur = 0; } else fx_sprite(t->p, t->glow_size * 0.5f, sh, NULL);
            if (ct) { fx_cur = fx_bin_of_kind(ct, FXK_ALPHA); fx_sprite(t->p, t->core_size * 0.5f, co, uv_full); fx_cur = 0; } else fx_sprite(t->p, t->core_size * 0.5f, co, NULL);
        } else { sh[3] = co[3] = 1.0f; fx_sprite(t->p, t->glow_size, sh, NULL); fx_sprite(t->p, t->core_size, co, NULL); }
    }
    { static TgFxRay rays[1024]; int nr = fx_off_shots ? 0 : tg_fx_rays(set, rays, 1024);        /* the flare rays of the torpedoes: additive quads, not depth written */
      for (i = 0; i < nr; i++) { SDL_GPUTexture *rt = fx_tex(rays[i].tex); fx_cur = fx_bin_of_kind(rt, rt ? FXK_BEAM : FXK_PLAINADD); fx_quad4((const float (*)[3])rays[i].v, rays[i].col); fx_cur = 0; } }
    if (getenv("QB_FXDUMP") && ns > 0) { static int sd; int q; if (sd++ < 4) for (q = 0; q < ns; q++) fprintf(stderr, "[shot] kind %d pos %.1f %.1f %.1f core %.3f glow %.3f len %.2f ctex %s gtex %s shell %.2f %.2f %.2f core %.2f %.2f %.2f\n", shots[q].kind, shots[q].p[0], shots[q].p[1], shots[q].p[2], shots[q].core_size, shots[q].glow_size, shots[q].len, shots[q].core_tex ? shots[q].core_tex : "-", shots[q].glow_tex ? shots[q].glow_tex : "-", shots[q].shell[0], shots[q].shell[1], shots[q].shell[2], shots[q].core[0], shots[q].core[1], shots[q].core[2]); }
    nl = tg_fx_lenses(set, cam_p, cam_f, lenses, 8);                 /* lens flares: fans on the plane 5 units out toward the source */
    for (i = 0; i < nl; i++) {
        TgFxLens *L = &lenses[i]; float dr = L->dir[0] * cam_r[0] + L->dir[1] * cam_r[1] + L->dir[2] * cam_r[2], du = L->dir[0] * cam_u[0] + L->dir[1] * cam_u[1] + L->dir[2] * cam_u[2];
        for (k = 0; k < L->n; k++) {
            float cc[3], R = 5.0f * L->dot * L->size[k], col[4] = {1, 1, 1, L->alpha}, ox = 5.0f * (1.0f - L->pos[k]) * dr, oy = 5.0f * (1.0f - L->pos[k]) * du;
            int w = L->wedges[k] > 2 ? L->wedges[k] : 3, j, m; SDL_GPUTexture *t = fx_tex(L->tex[k]);
            for (m = 0; m < 3; m++) cc[m] = cam_p[m] + cam_f[m] * 5.0f * L->dot + cam_r[m] * ox + cam_u[m] * oy;
            fx_cur = fx_bin_of_kind(t, FXK_FLARE);
            for (j = 0; j < w; j++) {                                  /* wedge j: the centre and two rim points; each wedge shows the whole texture */
                float a0 = 6.2831853f * (float)j / (float)w, a1 = 6.2831853f * (float)(j + 1) / (float)w, r0 = 1.0f + L->amp[k] * sinf(L->freq[k] * L->time + a0), r1 = 1.0f + L->amp[k] * sinf(L->freq[k] * L->time + a1);
                float pv[3][3], uvs[3][2] = {{0.5f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
                for (m = 0; m < 3; m++) { pv[0][m] = cc[m]; pv[1][m] = cc[m] + R * r0 * (cosf(a0) * cam_r[m] + sinf(a0) * cam_u[m]); pv[2][m] = cc[m] + R * r1 * (cosf(a1) * cam_r[m] + sinf(a1) * cam_u[m]); }
                for (m = 0; m < 3; m++) { FxVtx *v = fx_next(); if (!v) break; memcpy(v->pos, pv[m], 12); v->uv[0] = uvs[m][0]; v->uv[1] = uvs[m][1]; memcpy(v->col, col, 16); }
            }
            fx_cur = 0;
        }
    }
    {   static TgFxShieldPatch patches[24]; int npatch = fx_off_patch ? 0 : tg_fx_shield_patches(set, patches, 24);                 /* shield hits: before the particles */
        for (i = 0; i < npatch; i++) fx_patch(&patches[i]); }
    ne = fx_off_spr ? 0 : tg_fx_sprites(set, sprites, 4096);                          /* Effects.py: explosion sheets, smoke, spark heads */
    if (getenv("QB_FXDUMP") && ne > 20) {                           /* debugging: the sprites of one frame, with their place on the screen */
        static int dumped; int q, k; static float W = 1280.0f, H = 720.0f; extern double tg_game_time;
        if (!dumped && tg_game_time >= atof(getenv("QB_FXDUMP"))) for (dumped = 1, q = 0; q < ne; q++) {
            float clip[4], sx, sy, px;
            for (k = 0; k < 4; k++) clip[k] = vp[k * 4 + 0] * sprites[q].p[0] + vp[k * 4 + 1] * sprites[q].p[1] + vp[k * 4 + 2] * sprites[q].p[2] + vp[k * 4 + 3];
            if (clip[3] <= 0) continue;
            sx = (clip[0] / clip[3] * 0.5f + 0.5f) * W; sy = (0.5f - clip[1] / clip[3] * 0.5f) * H; px = sprites[q].size / clip[3] * 1.7320508f * H * 0.5f;
            if (px > 1.5f) fprintf(stderr, "[spr] %s screen %.0f %.0f px %.0f rgba %.2f %.2f %.2f %.2f uv %.3f %.3f %.3f %.3f blend %d/%d\n", sprites[q].tex, sx, sy, px, sprites[q].rgba[0], sprites[q].rgba[1], sprites[q].rgba[2], sprites[q].rgba[3], sprites[q].uv[0], sprites[q].uv[1], sprites[q].uv[2], sprites[q].uv[3], sprites[q].sblend, sprites[q].dblend);
        }
    }
    for (i = 0; i < ne; i++) {
        TgFxSprite *q = &sprites[i]; SDL_GPUTexture *t = fx_tex(q->tex);
        if (getenv("QB_FXSKIPTEX") && q->tex && strstr(q->tex, getenv("QB_FXSKIPTEX"))) continue;
        fx_cur = fx_bin_of_kind(t, fx_kind_of_modes(q->sblend, q->dblend));
        fx_sprite(q->p, q->size * 0.5f, q->rgba, t ? q->uv : NULL);
        fx_cur = 0;
    }
    {   static TgFxLine lines[4096]; int nline = tg_fx_spark_lines(set, lines, 4096);                           /* spark tails */
        fx_cur = fx_bin_of_kind(NULL, FXK_PLAIN);
        for (i = 0; i < nline; i++) fx_line2(lines[i].a, lines[i].b, lines[i].ca, lines[i].cb);
        fx_cur = 0; }
    fx_counts[0] = nb; fx_counts[1] = ns; fx_counts[2] = ne;
}

/* packs the bins into the vertex buffer in bin order; each bin's first vertex is recorded for its draw */
static void fx_pack(void)
{
    int i; fx_nv = 0;
    for (i = 0; i < fx_nbins; i++) {
        FxBin *b = &fx_bins[i]; int n = b->n;
        if (n > FX_MAX_VERTS - fx_nv) n = FX_MAX_VERTS - fx_nv;
        b->first = fx_nv; b->n = n;
        if (n) memcpy(fx_v + fx_nv, b->v, sizeof(FxVtx) * (size_t)n);
        fx_nv += n;
    }
}

static int ensure_depth(SDL_GPUDevice *dev, Uint32 w, Uint32 h)
{
    SDL_GPUTextureCreateInfo ti;
    if (depth && depth_w == w && depth_h == h) return 1;
    if (depth) SDL_ReleaseGPUTexture(dev, depth);
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D; ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET; ti.width = w; ti.height = h;
    ti.layer_count_or_depth = 1; ti.num_levels = 1; ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    depth = SDL_CreateGPUTexture(dev, &ti); depth_w = w; depth_h = h;
    return depth != NULL;
}

/* a vertex buffer from CPU data: staged in an upload buffer and copied on its own command buffer (submitted before the frame) */
static SDL_GPUBuffer *upload(SDL_GPUDevice *dev, const void *data, Uint32 size)
{
    SDL_GPUBufferCreateInfo bi; SDL_GPUTransferBufferCreateInfo ti; SDL_GPUBuffer *b; SDL_GPUTransferBuffer *t;
    SDL_GPUCommandBuffer *cmd; SDL_GPUCopyPass *cp; SDL_GPUTransferBufferLocation src; SDL_GPUBufferRegion dst; void *m;
    SDL_zero(bi); bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX; bi.size = size;
    b = SDL_CreateGPUBuffer(dev, &bi);
    SDL_zero(ti); ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = size;
    t = SDL_CreateGPUTransferBuffer(dev, &ti);
    if (!b || !t) return NULL;
    m = SDL_MapGPUTransferBuffer(dev, t, false); memcpy(m, data, size); SDL_UnmapGPUTransferBuffer(dev, t);
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    cp = SDL_BeginGPUCopyPass(cmd);
    src.transfer_buffer = t; src.offset = 0; dst.buffer = b; dst.offset = 0; dst.size = size;
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, t);
    return b;
}

/* an RGBA texture with a full mip chain: the pixels go to level 0 on its own command buffer, then the GPU makes the other levels */
static SDL_GPUTexture *upload_texture(SDL_GPUDevice *dev, const unsigned char *px, int w, int h)
{
    SDL_GPUTextureCreateInfo ti; SDL_GPUTransferBufferCreateInfo bi; SDL_GPUTexture *t; SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd; SDL_GPUCopyPass *cp; SDL_GPUTextureTransferInfo src; SDL_GPUTextureRegion dst; void *m;
    Uint32 levels = 1, size = (Uint32)w * (Uint32)h * 4;
    while ((w >> levels) || (h >> levels)) levels++;
    SDL_zero(ti); ti.type = SDL_GPU_TEXTURETYPE_2D; ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;     /* colour target: needed to generate mipmaps */
    ti.width = (Uint32)w; ti.height = (Uint32)h; ti.layer_count_or_depth = 1; ti.num_levels = levels; ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    t = SDL_CreateGPUTexture(dev, &ti);
    SDL_zero(bi); bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; bi.size = size;
    tb = SDL_CreateGPUTransferBuffer(dev, &bi);
    if (!t || !tb) { if (t) SDL_ReleaseGPUTexture(dev, t); if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb); return NULL; }
    m = SDL_MapGPUTransferBuffer(dev, tb, false); memcpy(m, px, size); SDL_UnmapGPUTransferBuffer(dev, tb);
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src); src.transfer_buffer = tb; src.pixels_per_row = (Uint32)w; src.rows_per_layer = (Uint32)h;
    SDL_zero(dst); dst.texture = t; dst.w = (Uint32)w; dst.h = (Uint32)h; dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, t);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return t;
}

/* textures by real path, shared by all models (stock ships share the SharedTextures files); a file that does not decode draws white */
static SDL_GPUTexture *texture_of(SDL_GPUDevice *dev, const char *path)
{
    int i, w, h; unsigned char *px; char err[128]; SDL_GPUTexture *t = NULL;
    if (!path) return white;
    for (i = 0; i < ntexs; i++) if (!strcmp(texs[i].path, path)) return texs[i].tex ? texs[i].tex : white;
    if ((px = img_load(path, &w, &h, err, sizeof err))) { t = upload_texture(dev, px, w, h); free(px); }
    else if (getenv("APPC_TRACE")) fprintf(stderr, "[render] texture %s: %s\n", path, err);
    texs = realloc(texs, (ntexs + 1) * sizeof *texs); texs[ntexs].path = strdup(path); texs[ntexs].tex = t; texs[ntexs].w = t ? w : 1; texs[ntexs].h = t ? h : 1; ntexs++;
    return t ? t : white;
}

/* a texture by the name a script gave (UI atlases): resolved over the data roots, cached like the others; its size in pixels (1 x 1 when missing) */
const char *tg_asset_resolve(const char *path);
SDL_GPUTexture *pl_texture_named(SDL_GPUDevice *dev, const char *name, int *w, int *h)
{
    const char *rp = name ? tg_asset_resolve(name) : NULL; SDL_GPUTexture *t = texture_of(dev, rp); int i;
    *w = *h = 1;
    for (i = 0; rp && i < ntexs; i++) if (!strcmp(texs[i].path, rp)) { *w = texs[i].w; *h = texs[i].h; break; }
    return t;
}
SDL_GPUSampler *pl_sampler(void) { return sampler; }

/* a glow map: the texture when it decodes, NULL when the file is missing (the part then has no glow pass) */
static SDL_GPUTexture *glow_texture(SDL_GPUDevice *dev, const char *path)
{
    int i;
    if (!path) return NULL;
    texture_of(dev, path);
    for (i = 0; i < ntexs; i++) if (!strcmp(texs[i].path, path)) return texs[i].tex;
    return NULL;
}
static int ensure_textures(SDL_GPUDevice *dev)
{
    static const unsigned char one[4] = {255, 255, 255, 255}; SDL_GPUSamplerCreateInfo si;
    if (!white) white = upload_texture(dev, one, 1, 1);
    if (!sampler) {
        SDL_zero(si);
        si.min_filter = SDL_GPU_FILTER_LINEAR; si.mag_filter = SDL_GPU_FILTER_LINEAR; si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;   /* NIF clamp modes are not read */
        si.max_lod = 1000.0f; si.enable_anisotropy = true; si.max_anisotropy = 8.0f;
        sampler = SDL_CreateGPUSampler(dev, &si);
    }
    return white && sampler;
}

/* one vertex buffer per model, one draw per shape */
static Mesh *mesh_of(SDL_GPUDevice *dev, const char *name)
{
    int i, n = 0; Uint32 total = 0; const TgMesh *tm; Vtx *v; Mesh *m;
    for (i = 0; i < nmeshes; i++) if (!strcmp(meshes[i].name, name)) return meshes[i].buf ? &meshes[i] : NULL;
    meshes = realloc(meshes, (nmeshes + 1) * sizeof *meshes); m = &meshes[nmeshes++]; memset(m, 0, sizeof *m); m->name = strdup(name);
    tm = tg_model_meshes(name, &n);
    if (!tm || n <= 0) return NULL;
    for (i = 0; i < n; i++) total += (Uint32)tm[i].nverts;
    if (!total) return NULL;
    v = malloc(sizeof(Vtx) * total);
    m->parts = calloc((size_t)n, sizeof *m->parts);
    for (i = 0, total = 0; i < n; i++) {
        Part *p = &m->parts[m->nparts];
        if (tm[i].nverts <= 0) continue;
        memcpy(v + total, tm[i].v, sizeof(Vtx) * (size_t)tm[i].nverts);
        p->first = total; p->count = (Uint32)tm[i].nverts; total += p->count; p->mi = i;
        p->tex = texture_of(dev, tm[i].texture);
        p->glow = glow_texture(dev, tm[i].glow); p->glowa = glow_texture(dev, tm[i].glowa);
        p->gloss = glow_texture(dev, tm[i].gloss);
        memcpy(p->specular, tm[i].specular, sizeof p->specular); p->shininess = tm[i].shininess;
        memcpy(p->diffuse, tm[i].diffuse, sizeof p->diffuse); memcpy(p->emissive, tm[i].emissive, sizeof p->emissive);
        if (!tm[i].texture) { int k; for (k = 0; k < 3; k++) if (p->diffuse[k] < 0.25f) p->diffuse[k] = 0.25f; }   /* untextured: keep it visible */
        m->nparts++;
    }
    m->buf = upload(dev, v, (Uint32)(sizeof(Vtx) * total));
    m->nverts = total;
    free(v);
    return m->buf ? m : NULL;
}

/* row-major 4x4 products */
static void mul(const float *a, const float *b, float *r)
{
    int i, j, k; float t[16];
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) { t[i * 4 + j] = 0; for (k = 0; k < 4; k++) t[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j]; }
    memcpy(r, t, sizeof t);
}

extern float tg_ship_specular_ks(void *o);
/* GPU buffers of damaged geometry (tg_damage.c): kind 0 the replacement for a shape's triangles, 1 the charred band, 2 a piece of debris. Keyed by the owner and the
   shape; remade when the version changes. */
typedef struct { const void *owner; int mi, kind, version; SDL_GPUBuffer *buf; int n; } DmgBuf;
static DmgBuf dmgbufs[512]; static int ndmgbufs;
static DmgBuf *dmg_buffer(const void *owner, int mi, int kind, int version, const float *v, int nverts)
{
    int i; DmgBuf *b = NULL;
    for (i = 0; i < ndmgbufs; i++) if (dmgbufs[i].owner == owner && dmgbufs[i].mi == mi && dmgbufs[i].kind == kind) { b = &dmgbufs[i]; break; }
    if (!b) {
        if (ndmgbufs < 512) b = &dmgbufs[ndmgbufs++]; else b = &dmgbufs[0];
        if (b->buf) { SDL_ReleaseGPUBuffer(pl_gpu(), b->buf); b->buf = NULL; }
        b->owner = owner; b->mi = mi; b->kind = kind; b->version = -1;
    }
    if (b->version != version) {
        if (b->buf) { SDL_ReleaseGPUBuffer(pl_gpu(), b->buf); b->buf = NULL; }
        b->n = nverts; b->version = version;
        if (nverts > 0) b->buf = upload(pl_gpu(), v, (Uint32)(sizeof(Vtx) * (size_t)nverts));
    }
    return b;
}
static void bind_vb(SDL_GPUBuffer *buf) { SDL_GPUBufferBinding bind; SDL_zero(bind); bind.buffer = buf; SDL_BindGPUVertexBuffers(fpass, 0, &bind, 1); }
static void set_frag(const FUni *fu) { SDL_PushGPUFragmentUniformData(fcmd, 0, fu, sizeof *fu); }
/* The two lights of each beam are effects of the firing ship's node (PhaserBank::CreateBeam 00572800, FUN_004e6e90, FUN_004e75b0; ObjectClass slot 0xd8 attaches a
   light to the object's node), so they light the ship that fires: one at the emitter with dimmer 0.6 and one at the hit point with dimmer 1.0, colour the beam's
   second colour, attenuation 1 / (2 * MainRadius * distance), no constant and no square term. The ship takes at most four. */
static void beam_lights_of(void *o, FUni *fu)
{
    int i, k, nl = 0;
    memset(fu->pl_pos, 0, sizeof fu->pl_pos); memset(fu->pl_col, 0, sizeof fu->pl_col);
    for (i = 0; i < fx_nbeams && nl + 2 <= 4; i++) {
        const TgFxBeam *b = &beams[i]; float l = 2.0f * b->radius;
        if (b->owner != o || l <= 1e-6f) continue;
        for (k = 0; k < 2; k++, nl++) {
            const float *pos = k ? b->b : b->a; float dim = k ? 1.0f : 0.6f;
            fu->pl_pos[nl][0] = pos[0]; fu->pl_pos[nl][1] = pos[1]; fu->pl_pos[nl][2] = pos[2]; fu->pl_pos[nl][3] = l;
            fu->pl_col[nl][0] = b->col[1][0] * dim; fu->pl_col[nl][1] = b->col[1][1] * dim; fu->pl_col[nl][2] = b->col[1][2] * dim; fu->pl_col[nl][3] = 1.0f;
        }
    }
}
static int draw_pass;                                                   /* 0: opaque meshes; 1: translucent meshes (a cloaking ship), after them */
static void draw_one(void *o)
{
    const char *model = tg_space_model_of(o); Mesh *m; float p[3], f[3], u[3], r[3], s = 1.0f, M[16], R[16], alpha, cl[4];
    VUni vu; FUni fu; int i, j;
    if (!model || tg_space_hidden(o) || tg_space_set_of(o) != tg_render_set()) return;
    {   float v = 1.0f; int mode = 0, pl = 0; alpha = 1.0f; memset(cl, 0, sizeof cl);
        if (tg_ship_cloak_view(o, &v, &mode, &pl)) {
            alpha = 0.5f; cl[0] = v; cl[1] = (float)mode; cl[2] = pl ? 1.0f : 0.0f; cl[3] = (float)(SDL_GetTicks() % 100000) * 0.37f;
            if (mode == 3 && !pl) return;                        /* another ship that is cloaked: not drawn */
        }
        if ((alpha < 1.0f) != (draw_pass == 1)) return; }
    m = mesh_of(pl_gpu(), model);
    if (!m) return;
    SDL_BindGPUGraphicsPipeline(fpass, draw_pass ? alpha_pipe : pipe);
    tg_space_pose(o, p, f, u, r, &s);
    if (s <= 0) s = 1.0f;
    /* model -> world: x = right, y = forward, z = up (the axes of world_to_model), scaled, then translated */
    for (i = 0; i < 3; i++) {
        M[i * 4 + 0] = r[i] * s; M[i * 4 + 1] = f[i] * s; M[i * 4 + 2] = u[i] * s; M[i * 4 + 3] = p[i];
        M[12 + i] = 0;
    }
    M[12] = 0; M[13] = 0; M[14] = 0; M[15] = 1;
    mul(vp, M, R);
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) vu.mvp[j * 4 + i] = R[i * 4 + j];       /* transpose to column-major */
    SDL_zero(vu.model);
    for (i = 0; i < 3; i++) { vu.model[0 * 4 + i] = r[i]; vu.model[1 * 4 + i] = f[i]; vu.model[2 * 4 + i] = u[i]; }   /* columns: the axes */
    vu.model[15] = 1; memcpy(vu.cloak, cl, sizeof cl); vu.wo[0] = p[0]; vu.wo[1] = p[1]; vu.wo[2] = p[2]; vu.wo[3] = s;
    SDL_PushGPUVertexUniformData(fcmd, 0, &vu, sizeof vu);
    {
        SDL_GPUBufferBinding bind; SDL_zero(bind); bind.buffer = m->buf; bind.offset = 0;
        SDL_BindGPUVertexBuffers(fpass, 0, &bind, 1);
    }
    fu.light[0] = 0.4f; fu.light[1] = 0.3f; fu.light[2] = 0.85f; fu.light[3] = alpha;   /* fixed world light (approximation); light.w is the opacity */
    for (i = 0; i < 3; i++) { fu.eye[i] = cam_p[i]; fu.origin[i] = p[i]; } fu.eye[3] = fu.origin[3] = 0;
    beam_lights_of(o, &fu);
    {   float ks = tg_ship_specular_ks(o); const TgDamageGeom *dg = tg_damage_geom(o);
    for (i = 0; i < m->nparts; i++) {
        Part *pt = &m->parts[i]; SDL_GPUTextureSamplerBinding tb; SDL_GPUBuffer *vb = m->buf; Uint32 first = pt->first, count = pt->count; int dmg = 0;
        if (dg && pt->mi < dg->nmesh && dg->mesh[pt->mi].touched) {      /* hull damage: this shape's triangles come from the damage buffer */
            DmgBuf *db = dmg_buffer(o, pt->mi, 0, dg->version, dg->mesh[pt->mi].base, dg->mesh[pt->mi].nbase);
            vb = db->buf; first = 0; count = (Uint32)db->n; dmg = 1; if (vb) bind_vb(vb);
        }
        fu.diffuse[0] = pt->diffuse[0]; fu.diffuse[1] = pt->diffuse[1]; fu.diffuse[2] = pt->diffuse[2]; fu.diffuse[3] = 1;
        fu.emissive[0] = pt->emissive[0]; fu.emissive[1] = pt->emissive[1]; fu.emissive[2] = pt->emissive[2]; fu.emissive[3] = 0;
        fu.misc[0] = 1.0f; fu.misc[1] = ks; fu.misc[2] = pt->shininess; fu.misc[3] = 0;      /* glow intensity (glow maps on), Ks, shininess */
        fu.specc[0] = pt->specular[0]; fu.specc[1] = pt->specular[1]; fu.specc[2] = pt->specular[2]; fu.specc[3] = 0;
        set_frag(&fu);
        tb.texture = pt->tex; tb.sampler = sampler;
        SDL_BindGPUFragmentSamplers(fpass, 0, &tb, 1);
        if (dmg && !draw_pass) SDL_BindGPUGraphicsPipeline(fpass, dmg_pipe);
        if (count && vb) SDL_DrawGPUPrimitives(fpass, count, 1, first, 0);
        if (dmg && !draw_pass) SDL_BindGPUGraphicsPipeline(fpass, pipe);
        if (!draw_pass && count && vb && (pt->glow || pt->glowa || (pt->gloss && ks > 0.0f && pt->shininess > 0.0f))) {   /* the glow, glow alpha and gloss passes: added on top (opaque ships only) */
            SDL_GPUTextureSamplerBinding gb; SDL_zero(gb); gb.sampler = sampler;
            if (pt->glow) { gb.texture = pt->glow; SDL_BindGPUGraphicsPipeline(fpass, glow_pipe); SDL_BindGPUFragmentSamplers(fpass, 0, &gb, 1); SDL_DrawGPUPrimitives(fpass, count, 1, first, 0); }
            if (pt->glowa) { gb.texture = pt->glowa; SDL_BindGPUGraphicsPipeline(fpass, glowa_pipe); SDL_BindGPUFragmentSamplers(fpass, 0, &gb, 1); SDL_DrawGPUPrimitives(fpass, count, 1, first, 0); }
            if (pt->gloss && ks > 0.0f && pt->shininess > 0.0f) { gb.texture = pt->gloss; SDL_BindGPUGraphicsPipeline(fpass, spec_pipe); SDL_BindGPUFragmentSamplers(fpass, 0, &gb, 1); SDL_DrawGPUPrimitives(fpass, count, 1, first, 0); }
            SDL_BindGPUGraphicsPipeline(fpass, pipe);
        }
        if (dg && !draw_pass && pt->mi < dg->nmesh && dg->mesh[pt->mi].nring > 0) {         /* the charred band round the holes */
            static SDL_GPUTexture *dtex; DmgBuf *rb = dmg_buffer(o, pt->mi, 1, dg->version, dg->mesh[pt->mi].ring, dg->mesh[pt->mi].nring); int w, h;
            if (!dtex) dtex = pl_texture_named(pl_gpu(), "data/Textures/Effects/Damage.tga", &w, &h);
            if (rb->buf && dtex) {
                SDL_GPUTextureSamplerBinding rt; SDL_zero(rt); rt.texture = dtex; rt.sampler = sampler;
                bind_vb(rb->buf); SDL_BindGPUGraphicsPipeline(fpass, ring_pipe); SDL_BindGPUFragmentSamplers(fpass, 0, &rt, 1);
                SDL_DrawGPUPrimitives(fpass, (Uint32)rb->n, 1, 0, 0);
                SDL_BindGPUGraphicsPipeline(fpass, pipe);
            }
        }
        if (dmg || (dg && pt->mi < dg->nmesh && dg->mesh[pt->mi].nring > 0)) bind_vb(m->buf);
    }
    }
    drawn_objects++; drawn_tris += (int)m->nverts / 3;
}

/* Debris: a piece of a ship, drawn like the ship's own shapes (its textures and materials by the shape index) with the piece's pose; its buffers are cached by the piece's id. */
static void draw_debris(void *set)
{
    static TgFxDebris items[64]; int n = tg_fx_debris(set, items, 64), k, i, j; (void)set;
    for (k = 0; k < n; k++) {
        TgFxDebris *d = &items[k]; Mesh *m = mesh_of(pl_gpu(), d->model); float M[16], R[16]; VUni vu; FUni fu;
        if (!m) continue;
        SDL_BindGPUGraphicsPipeline(fpass, dmg_pipe);
        for (i = 0; i < 3; i++) { M[i * 4 + 0] = d->r[i] * d->scale; M[i * 4 + 1] = d->f[i] * d->scale; M[i * 4 + 2] = d->u[i] * d->scale; M[i * 4 + 3] = d->p[i]; M[12 + i] = 0; }
        M[12] = 0; M[13] = 0; M[14] = 0; M[15] = 1; mul(vp, M, R);
        for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) vu.mvp[j * 4 + i] = R[i * 4 + j];
        SDL_zero(vu.model); for (i = 0; i < 3; i++) { vu.model[0 * 4 + i] = d->r[i]; vu.model[1 * 4 + i] = d->f[i]; vu.model[2 * 4 + i] = d->u[i]; } vu.model[15] = 1; memset(vu.cloak, 0, sizeof vu.cloak); vu.wo[0] = d->p[0]; vu.wo[1] = d->p[1]; vu.wo[2] = d->p[2]; vu.wo[3] = d->scale;
        SDL_PushGPUVertexUniformData(fcmd, 0, &vu, sizeof vu);
        fu.light[0] = 0.4f; fu.light[1] = 0.3f; fu.light[2] = 0.85f; fu.light[3] = 1.0f;
        for (i = 0; i < 3; i++) { fu.eye[i] = cam_p[i]; fu.origin[i] = d->p[i]; } fu.eye[3] = fu.origin[3] = 0; fu.misc[0] = 1.0f; fu.misc[1] = fu.misc[2] = fu.misc[3] = 0; memset(fu.specc, 0, sizeof fu.specc); memset(fu.pl_pos, 0, sizeof fu.pl_pos); memset(fu.pl_col, 0, sizeof fu.pl_col);
        for (j = 0; j < d->nparts; j++) {
            Part *pt = NULL; DmgBuf *db; SDL_GPUTextureSamplerBinding tb;
            for (i = 0; i < m->nparts; i++) if (m->parts[i].mi == d->parts[j].mesh) { pt = &m->parts[i]; break; }
            if (!pt) continue;
            db = dmg_buffer((const void *)(size_t)(d->id * 64u + 1u), d->parts[j].mesh, 2, 1, d->parts[j].v, d->parts[j].nv);
            if (!db->buf || !db->n) continue;
            fu.diffuse[0] = pt->diffuse[0]; fu.diffuse[1] = pt->diffuse[1]; fu.diffuse[2] = pt->diffuse[2]; fu.diffuse[3] = 1;
            fu.emissive[0] = pt->emissive[0]; fu.emissive[1] = pt->emissive[1]; fu.emissive[2] = pt->emissive[2]; fu.emissive[3] = 0;
            set_frag(&fu); bind_vb(db->buf);
            tb.texture = pt->tex; tb.sampler = sampler; SDL_BindGPUFragmentSamplers(fpass, 0, &tb, 1);
            SDL_DrawGPUPrimitives(fpass, (Uint32)db->n, 1, 0, 0);
        }
        drawn_objects++;
    }
    SDL_BindGPUGraphicsPipeline(fpass, pipe);
}

/* view and projection of the rendered set's camera (world -> camera axes: x right, y up, z forward) */
static int set_camera(void *cam, Uint32 w, Uint32 h)
{
    float p[3], f[3], u[3], r[3], s, V[16], P[16], fy, asp, nr = 1.0f, fa = 1e6f, A, B, t[16];
    int i;
    if (!cam) return 0;
    tg_space_pose(cam, p, f, u, r, &s);
    memcpy(cam_p, p, 12); memcpy(cam_f, f, 12); memcpy(cam_u, u, 12); memcpy(cam_r, r, 12);
    for (i = 0; i < 16; i++) V[i] = 0;
    V[0] = r[0]; V[1] = r[1]; V[2] = r[2];  V[3] = -(r[0] * p[0] + r[1] * p[1] + r[2] * p[2]);
    V[4] = u[0]; V[5] = u[1]; V[6] = u[2];  V[7] = -(u[0] * p[0] + u[1] * p[1] + u[2] * p[2]);
    V[8] = f[0]; V[9] = f[1]; V[10] = f[2]; V[11] = -(f[0] * p[0] + f[1] * p[1] + f[2] * p[2]);
    V[15] = 1;
    fy = 1.0f / tanf(60.0f * 3.14159265f / 360.0f); asp = (float)w / (float)h; cam_aspect = asp;
    A = fa / (fa - nr); B = -nr * fa / (fa - nr);
    for (i = 0; i < 16; i++) P[i] = 0;
    P[0] = fy / asp; P[5] = fy; P[10] = A; P[11] = B; P[14] = 1.0f;
    mul(P, V, t);                                       /* P * V: columns of the view are the world axes */
    memcpy(vp, t, sizeof t);
    (void)view;
    return 1;
}

/* draws the rendered set into a colour target of size w x h (the window's swapchain or an offscreen texture) */
static int draw_frame(SDL_GPUTexture *color, Uint32 w, Uint32 h, double cr, double cg, double cb, int *objects, int *tris)
{
    SDL_GPUDevice *dev = pl_gpu(); SDL_GPUColorTargetInfo ct; SDL_GPUDepthStencilTargetInfo dt; void *cam;
    if (!ensure_depth(dev, w, h)) return 0;
    drawn_objects = 0; drawn_tris = 0;
    cam = tg_render_camera();
    SDL_zero(ct); ct.texture = color; ct.clear_color.r = (float)cr; ct.clear_color.g = (float)cg; ct.clear_color.b = (float)cb; ct.clear_color.a = 1.0f;
    ct.load_op = SDL_GPU_LOADOP_CLEAR; ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_zero(dt); dt.texture = depth; dt.clear_depth = 1.0f; dt.load_op = SDL_GPU_LOADOP_CLEAR; dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    fx_nv = 0; fx_dev = dev; fx_pix_h = (float)h;
    if (cam && set_camera(cam, w, h)) {                                /* effect vertices go up before the render pass */
        fx_build(tg_render_set());
        fx_pack();
        if (fx_nv) {
            SDL_GPUCopyPass *cp; SDL_GPUTransferBufferLocation src; SDL_GPUBufferRegion dst; void *m = SDL_MapGPUTransferBuffer(dev, fx_tb, true);
            memcpy(m, fx_v, sizeof(FxVtx) * (size_t)fx_nv); SDL_UnmapGPUTransferBuffer(dev, fx_tb);
            cp = SDL_BeginGPUCopyPass(fcmd);
            src.transfer_buffer = fx_tb; src.offset = 0; dst.buffer = fx_buf; dst.offset = 0; dst.size = (Uint32)(sizeof(FxVtx) * (size_t)fx_nv);
            SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            SDL_EndGPUCopyPass(cp);
        }
    } else cam = NULL;
    fpass = SDL_BeginGPURenderPass(fcmd, &ct, 1, &dt);
    if (cam) {
        float vpc[16]; int i, j, early, bound = 0; SDL_GPUBufferBinding bind;
        for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) vpc[j * 4 + i] = vp[i * 4 + j];
        for (early = 1; early >= 0; early--) {                       /* the backdrops first, then the ships, then the other effects */
            if (!early) {
                draw_pass = 0; tg_space_each(0x8002, draw_one);
                draw_debris(tg_render_set());                      /* the pieces thrown off damaged ships (tg_damage.c) */
                draw_pass = 1; tg_space_each(0x8002, draw_one);     /* the translucent ships, drawn after the opaque meshes */
                draw_pass = 0; bound = 0;
            }
            if (!fx_nv) continue;
            for (i = 0; i < fx_nbins; i++) {
                FxBin *b = &fx_bins[i];
                if (!b->n || b->early != early) continue;
                if (!bound) { SDL_PushGPUVertexUniformData(fcmd, 0, vpc, sizeof vpc); SDL_zero(bind); bind.buffer = fx_buf; SDL_BindGPUVertexBuffers(fpass, 0, &bind, 1); bound = 1; }
                if (b->kind == FXK_BACKDROP) {
                    SDL_GPUTextureSamplerBinding tb; SDL_zero(tb); tb.texture = b->tex; tb.sampler = sampler;
                    SDL_BindGPUGraphicsPipeline(fpass, bd_pipe(b->s, b->d)); SDL_BindGPUFragmentSamplers(fpass, 0, &tb, 1);
                } else if (!b->tex) SDL_BindGPUGraphicsPipeline(fpass, b->kind == FXK_PLAIN ? fx_plain_pipe : b->kind == FXK_PLAINADD ? fx_padd_pipe : fx_pipe);
                else {
                    SDL_GPUTextureSamplerBinding tb; SDL_zero(tb);
                    tb.texture = b->tex; tb.sampler = sampler;
                    SDL_BindGPUGraphicsPipeline(fpass, b->kind == FXK_FLARE ? fx_flare_pipe : b->kind == FXK_BEAM ? fx_beam_pipe : b->kind == FXK_ALPHA ? fx_alpha_pipe : b->kind == FXK_ONEINV ? fx_oneinv_pipe : fx_tex_pipe);
                    SDL_BindGPUFragmentSamplers(fpass, 0, &tb, 1);
                }
                SDL_DrawGPUPrimitives(fpass, (Uint32)b->n, 1, (Uint32)b->first, 0);
            }
        }
    }
    SDL_EndGPURenderPass(fpass); fpass = NULL;
    pl_draw_ui(fcmd, color, w, h);
    if (objects) *objects = drawn_objects;
    if (tris) *tris = drawn_tris;
    return 1;
}

/* the rendered set, from the window: draws one frame. Returns 1 when the frame was drawn, 0 when there is nothing to draw. */
int pl_render_scene(double cr, double cg, double cb, int *objects, int *tris)
{
    SDL_GPUDevice *dev = pl_gpu(); SDL_Window *win = pl_window();
    SDL_GPUTexture *swap = NULL; Uint32 w = 0, h = 0; int ok;
    if (objects) *objects = 0;
    if (tris) *tris = 0;
    if (!dev || !win) return 0;
    if (!ensure_pipeline(dev, win) || !ensure_textures(dev) || !ensure_fx(dev, win)) return 0;
    fcmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!fcmd) return 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(fcmd, win, &swap, &w, &h) || !swap) {
        SDL_SubmitGPUCommandBuffer(fcmd); fcmd = NULL; return 0;
    }
    ok = draw_frame(swap, w, h, cr, cg, cb, objects, tris);
    SDL_SubmitGPUCommandBuffer(fcmd); fcmd = NULL;
    return ok;
}

/* one frame of the rendered set into a 1280 x 720 offscreen texture, read back and written as a raw BGRA file (rows top to bottom).
   For checks and screenshots without a screen capture permission. Returns the number of objects drawn, or -1 on failure. */
int pl_snapshot(const char *path, int *tris)
{
    SDL_GPUDevice *dev = pl_gpu(); SDL_Window *win = pl_window(); SDL_GPUTextureCreateInfo ti; SDL_GPUTexture *tex;
    SDL_GPUTransferBufferCreateInfo bi; SDL_GPUTransferBuffer *tb; SDL_GPUCommandBuffer *cmd; SDL_GPUCopyPass *cp;
    SDL_GPUTextureRegion reg; SDL_GPUTextureTransferInfo info; SDL_GPUTextureFormat fmt; Uint32 W = 1280, H = 720, n = 0;
    int objs = 0, tr = 0; void *m; FILE *f;
    if (tris) *tris = 0;
    if (!dev || !win || !ensure_pipeline(dev, win) || !ensure_textures(dev) || !ensure_fx(dev, win)) return -1;
    fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);
    SDL_zero(ti); ti.type = SDL_GPU_TEXTURETYPE_2D; ti.format = fmt; ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width = W; ti.height = H; ti.layer_count_or_depth = 1; ti.num_levels = 1; ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tex = SDL_CreateGPUTexture(dev, &ti);
    SDL_zero(bi); bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; bi.size = W * H * 4;
    tb = SDL_CreateGPUTransferBuffer(dev, &bi);
    if (!tex || !tb) return -1;
    fcmd = SDL_AcquireGPUCommandBuffer(dev);
    draw_frame(tex, W, H, 0.02, 0.02, 0.05, &objs, &tr);
    SDL_SubmitGPUCommandBuffer(fcmd); fcmd = NULL;
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(reg); reg.texture = tex; reg.w = W; reg.h = H; reg.d = 1;
    SDL_zero(info); info.transfer_buffer = tb; info.pixels_per_row = W; info.rows_per_layer = H;
    SDL_DownloadFromGPUTexture(cp, &reg, &info);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(dev);
    m = SDL_MapGPUTransferBuffer(dev, tb, false);
    f = fopen(path, "wb");
    if (f && m) { fprintf(f, "%u %u\n", W, H); fwrite(m, 1, W * H * 4, f); n = 1; }
    if (f) fclose(f);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUTexture(dev, tex);
    if (tris) *tris = tr;
    {   int i, k, parts = 0, plain = 0;
        for (i = 0; i < nmeshes; i++) for (k = 0; k < meshes[i].nparts; k++) { parts++; if (meshes[i].parts[k].tex == white) plain++; }
        fprintf(stderr, "[render] %d models, %d shapes, %d without a texture, %d texture files\n", nmeshes, parts, plain, ntexs);
    }
    return n ? objs : -1;
}

/* beams, projectiles and particle sprites drawn in the last frame (for the host's checks) */
void pl_fx_counts(int *beams, int *shots, int *booms) { *beams = fx_counts[0]; *shots = fx_counts[1]; *booms = fx_counts[2]; }
