/* UI overlay: the engine's draw list (tg_ui_draw.c) as alpha-blended textured quads, drawn after the scene in a render
   pass that keeps the scene (load, no depth). The graphics mode's pixels are fitted into the target with their aspect kept
   (letterboxed). Textures come from render.c (pl_texture_named: decoded once, cached by the resolved file name), the sampler from
   pl_sampler. Approximations (marked): no clipping to parent panes; the scene's sampler (mipmaps, 8x anisotropy) is used for UI; one
   draw call per quad. */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tg_ui_draw.h"

SDL_GPUDevice *pl_gpu(void);
SDL_Window *pl_window(void);
SDL_GPUTexture *pl_texture_named(SDL_GPUDevice *dev, const char *name, int *w, int *h);
SDL_GPUSampler *pl_sampler(void);

#define UI_MAX 8192
typedef struct { float pos[2], uv[2], col[4]; } UiVtx;                 /* 6 vertices per quad, pos in normalized device units */

static SDL_GPUGraphicsPipeline *ui_pipe; static SDL_GPUBuffer *ui_buf; static SDL_GPUTransferBuffer *ui_tb;
static TgUiQuad *ui_quads; static UiVtx *ui_v; static SDL_GPUTexture **ui_tex;

static const char ui_src[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };\n"
    "struct VOut { float4 pos [[position]]; float2 uv; float4 col; };\n"
    "vertex VOut ui_vs(VIn in [[stage_in]]) {\n"
    "    VOut o; o.pos = float4(in.pos, 0.0, 1.0); o.uv = in.uv; o.col = in.col; return o;\n"
    "}\n"
    "fragment float4 ui_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    return tex.sample(smp, in.uv) * in.col;\n"
    "}\n";

static SDL_GPUShader *ui_shader(SDL_GPUDevice *dev, const char *entry, SDL_GPUShaderStage stage, Uint32 samplers)
{
    SDL_GPUShaderCreateInfo si; SDL_zero(si);
    si.code = (const Uint8 *)ui_src; si.code_size = strlen(ui_src); si.entrypoint = entry;
    si.format = SDL_GPU_SHADERFORMAT_MSL; si.stage = stage; si.num_samplers = samplers;
    return SDL_CreateGPUShader(dev, &si);
}

/* the pipeline (for the window's colour format), the vertex and upload buffers, and the CPU arrays; made once */
static int ensure_ui(SDL_GPUDevice *dev, SDL_Window *win)
{
    SDL_GPUShader *vs, *fs; SDL_GPUVertexBufferDescription vb; SDL_GPUVertexAttribute va[3]; SDL_GPUColorTargetDescription ctd;
    SDL_GPUGraphicsPipelineCreateInfo pi; SDL_GPUBufferCreateInfo bi; SDL_GPUTransferBufferCreateInfo ti;
    if (ui_pipe) return 1;
    vs = ui_shader(dev, "ui_vs", SDL_GPU_SHADERSTAGE_VERTEX, 0);
    fs = ui_shader(dev, "ui_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
    if (!vs || !fs) return 0;
    SDL_zero(vb); vb.slot = 0; vb.pitch = sizeof(UiVtx); vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(va);
    va[0].location = 0; va[0].buffer_slot = 0; va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; va[0].offset = 0;
    va[1].location = 1; va[1].buffer_slot = 0; va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; va[1].offset = 8;
    va[2].location = 2; va[2].buffer_slot = 0; va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; va[2].offset = 16;
    SDL_zero(ctd); ctd.format = SDL_GetGPUSwapchainTextureFormat(dev, win);
    ctd.blend_state.enable_blend = true;
    ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_zero(pi);
    pi.vertex_shader = vs; pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = va; pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.depth_stencil_state.enable_depth_test = false; pi.depth_stencil_state.enable_depth_write = false;
    pi.target_info.color_target_descriptions = &ctd; pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = false;
    ui_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    SDL_ReleaseGPUShader(dev, vs); SDL_ReleaseGPUShader(dev, fs);
    SDL_zero(bi); bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX; bi.size = sizeof(UiVtx) * 6 * UI_MAX;
    ui_buf = SDL_CreateGPUBuffer(dev, &bi);
    SDL_zero(ti); ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = sizeof(UiVtx) * 6 * UI_MAX;
    ui_tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    ui_quads = malloc(sizeof(TgUiQuad) * UI_MAX); ui_v = malloc(sizeof(UiVtx) * 6 * UI_MAX); ui_tex = malloc(sizeof(SDL_GPUTexture *) * UI_MAX);
    return ui_pipe && ui_buf && ui_tb && ui_quads && ui_v && ui_tex;
}

/* writes the two triangles of one quad: corners in device units, texture coordinates in 0..1 */
static void put_quad(UiVtx *v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float *col, int rot, int mirror, const float *fr)
{
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    float px[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    float c[4][2]; int k, j;
    /* corner j (top left, top right, bottom right, bottom left) shows the source corner j - rot (clockwise turns), then the mirror */
    for (j = 0; j < 4; j++) { int sj = (j + 4 - (rot & 3)) & 3; c[j][0] = uv[sj][0]; c[j][1] = uv[sj][1]; }
    if (mirror & 1) { float t[2] = {c[0][0], c[0][1]}; c[0][0] = c[1][0]; c[0][1] = c[1][1]; c[1][0] = t[0]; c[1][1] = t[1];
                      t[0] = c[3][0]; t[1] = c[3][1]; c[3][0] = c[2][0]; c[3][1] = c[2][1]; c[2][0] = t[0]; c[2][1] = t[1]; }
    if (mirror & 2) { float t[2] = {c[0][0], c[0][1]}; c[0][0] = c[3][0]; c[0][1] = c[3][1]; c[3][0] = t[0]; c[3][1] = t[1];
                      t[0] = c[1][0]; t[1] = c[1][1]; c[1][0] = c[2][0]; c[1][1] = c[2][1]; c[2][0] = t[0]; c[2][1] = t[1]; }
    if (fr[0] > 0 || fr[1] > 0 || fr[2] < 1 || fr[3] < 1) {       /* cut to the clip rectangle: fractions of the quad, the texture coordinates follow */
        float nx[4] = {fr[0], fr[2], fr[2], fr[0]}, ny[4] = {fr[1], fr[1], fr[3], fr[3]}, cc[4][2], np[4][2];
        for (j = 0; j < 4; j++) {
            float fx = nx[j], fy = ny[j]; int e;
            for (e = 0; e < 2; e++) cc[j][e] = (1 - fx) * (1 - fy) * c[0][e] + fx * (1 - fy) * c[1][e] + fx * fy * c[2][e] + (1 - fx) * fy * c[3][e];
            np[j][0] = px[0][0] + (px[1][0] - px[0][0]) * fx; np[j][1] = px[0][1] + (px[3][1] - px[0][1]) * fy;
        }
        memcpy(c, cc, sizeof c); memcpy(px, np, sizeof px);
    }
    for (k = 0; k < 6; k++) {
        UiVtx *o = &v[k]; j = order[k];
        o->pos[0] = px[j][0]; o->pos[1] = px[j][1]; o->uv[0] = c[j][0]; o->uv[1] = c[j][1];
        memcpy(o->col, col, sizeof o->col);
    }
}

/* draws the UI over a colour target of size W x H (already holding the scene). Does nothing when there is no UI. */
void pl_draw_ui(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, Uint32 W, Uint32 H)
{
    SDL_GPUDevice *dev = pl_gpu(); SDL_Window *win = pl_window(); SDL_GPUColorTargetInfo ct; SDL_GPURenderPass *pass;
    SDL_GPUTransferBufferLocation src; SDL_GPUBufferRegion dst; SDL_GPUCopyPass *cp; SDL_GPUBufferBinding bind;
    int n, i, mw = 0, mh = 0, tw, th; float s, ox, oy; void *m;
    if (!dev || !win || !cmd || !target || !W || !H || !ensure_ui(dev, win)) return;
    n = tg_ui_quads(ui_quads, UI_MAX, &mw, &mh);
    if (n <= 0 || mw <= 0 || mh <= 0) return;
    s = fminf((float)W / (float)mw, (float)H / (float)mh);                         /* mode pixels to target pixels */
    ox = ((float)W - (float)mw * s) * 0.5f; oy = ((float)H - (float)mh * s) * 0.5f;
    for (i = 0; i < n; i++) {
        TgUiQuad *q = &ui_quads[i];
        float x0 = ox + q->x * s, y0 = oy + q->y * s, x1 = x0 + q->w * s, y1 = y0 + q->h * s;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        ui_tex[i] = pl_texture_named(dev, q->tex, &tw, &th);
        if (q->tex) {       /* the source rectangle, kept half a texel inside so that filtering does not take in the neighbouring artwork */
            float ix = q->grow && q->sw > 16 ? 0.5f : 0.0f, iy = q->grow && q->sh > 16 ? 0.5f : 0.0f;
            u0 = (q->sx + ix) / tw; v0 = (q->sy + iy) / th; u1 = (q->sx + q->sw - ix) / tw; v1 = (q->sy + q->sh - iy) / th;
        }
        if (!q->tex || q->grow) { x0 = floorf(x0 + 0.02f); y0 = floorf(y0 + 0.02f); x1 = ceilf(x1 - 0.02f); y1 = ceilf(y1 - 0.02f); }   /* abutting pieces then meet exactly */
        float fr[4] = {0, 0, 1, 1};
        if (q->clip[2] > q->clip[0] && x1 > x0 && y1 > y0) {
            float cx0 = ox + q->clip[0] * s, cy0 = oy + q->clip[1] * s, cx1 = ox + q->clip[2] * s, cy1 = oy + q->clip[3] * s;
            fr[0] = fminf(fmaxf((cx0 - x0) / (x1 - x0), 0.0f), 1.0f); fr[2] = fminf(fmaxf((cx1 - x0) / (x1 - x0), 0.0f), 1.0f);
            fr[1] = fminf(fmaxf((cy0 - y0) / (y1 - y0), 0.0f), 1.0f); fr[3] = fminf(fmaxf((cy1 - y0) / (y1 - y0), 0.0f), 1.0f);
        }
        put_quad(&ui_v[i * 6], 2.0f * x0 / W - 1.0f, 1.0f - 2.0f * y0 / H, 2.0f * x1 / W - 1.0f, 1.0f - 2.0f * y1 / H, u0, v0, u1, v1, q->col, q->rot, q->mirror, fr);
        if (q->angle != 0.0f) {                                                      /* turned clockwise about the quad's centre (in pixels) */
            float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, ca = cosf(q->angle), sa = sinf(q->angle); int k;
            for (k = 0; k < 6; k++) {
                UiVtx *vt = &ui_v[i * 6 + k]; float px = (vt->pos[0] + 1.0f) * 0.5f * W - cx, py = (1.0f - vt->pos[1]) * 0.5f * H - cy, nx = px * ca - py * sa, ny = px * sa + py * ca;
                vt->pos[0] = 2.0f * (cx + nx) / W - 1.0f; vt->pos[1] = 1.0f - 2.0f * (cy + ny) / H;
            }
        }
    }
    m = SDL_MapGPUTransferBuffer(dev, ui_tb, true);
    memcpy(m, ui_v, sizeof(UiVtx) * 6 * (size_t)n); SDL_UnmapGPUTransferBuffer(dev, ui_tb);
    cp = SDL_BeginGPUCopyPass(cmd);
    src.transfer_buffer = ui_tb; src.offset = 0; dst.buffer = ui_buf; dst.offset = 0; dst.size = (Uint32)(sizeof(UiVtx) * 6 * (size_t)n);
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    SDL_zero(ct); ct.texture = target; ct.load_op = SDL_GPU_LOADOP_LOAD; ct.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, ui_pipe);
    SDL_zero(bind); bind.buffer = ui_buf; bind.offset = 0; SDL_BindGPUVertexBuffers(pass, 0, &bind, 1);
    for (i = 0; i < n; i++) {
        SDL_GPUTextureSamplerBinding tb; tb.texture = ui_tex[i]; tb.sampler = pl_sampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
        SDL_DrawGPUPrimitives(pass, 6, 1, (Uint32)(6 * i), 0);
    }
    SDL_EndGPURenderPass(pass);
}
