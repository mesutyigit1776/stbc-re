#pragma once
#include <cstddef>

struct NiColorA {
    float r, g, b, a;

    NiColorA() = default;
    NiColorA(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
};

struct TGColorA : NiColorA {
    TGColorA() : NiColorA(0.0f, 0.0f, 0.0f, 0.0f) {}

    float GetR() const { return r; }
    float GetG() const { return g; }
    float GetB() const { return b; }
    float GetA() const { return a; }
    void SetR(float v) { r = v; }
    void SetG(float v) { g = v; }
    void SetB(float v) { b = v; }
    void SetA(float v) { a = v; }
    void SetRGBA(float nr, float ng, float nb, float na);
    void ScaleRGB(float scale);
    void Copy(NiColorA color);
};

static_assert(sizeof(NiColorA) == 16, "NiColorA size");
static_assert(offsetof(NiColorA, r) == 0 && offsetof(NiColorA, g) == 4 && offsetof(NiColorA, b) == 8 &&
                  offsetof(NiColorA, a) == 12,
              "NiColorA layout");
static_assert(sizeof(TGColorA) == 16, "TGColorA size");
