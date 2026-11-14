#pragma once
#include <cstddef>
#include "TGPoint3.h"

// Nine floats, row-major.
struct TGMatrix3 {
    float m[9];

    TGPoint3* MatrixTimesPoint(TGPoint3* out, const TGPoint3* p) const;
    static TGPoint3* PointTimesMatrix(TGPoint3* out, const TGPoint3* p, const TGMatrix3* matrix);
};

static_assert(sizeof(TGMatrix3) == 36, "TGMatrix3 size");
