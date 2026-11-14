#include "TGMatrix3.h"

using x87::Reg;

// 0x459cb0
TGPoint3* TGMatrix3::MatrixTimesPoint(TGPoint3* out, const TGPoint3* p) const
{
    Reg px = p->x, py = p->y, pz = p->z;
    Reg sumZ = (Reg(m[8]) * pz + Reg(m[6]) * px) + Reg(m[7]) * py;
    Reg sumY = (Reg(m[5]) * pz + Reg(m[3]) * px) + Reg(m[4]) * py;
    Reg sumX = (Reg(m[2]) * pz + Reg(m[1]) * py) + px * Reg(m[0]);
    out->x = StoreF(sumX);
    out->y = StoreF(sumY);
    out->z = StoreF(sumZ);
    return out;
}

// 0x459d10
TGPoint3* TGMatrix3::PointTimesMatrix(TGPoint3* out, const TGPoint3* p, const TGMatrix3* matrix)
{
    const float* m = matrix->m;
    Reg px = p->x, py = p->y, pz = p->z;
    Reg sumZ = (Reg(m[8]) * pz + Reg(m[5]) * py) + Reg(m[2]) * px;
    Reg sumY = (Reg(m[7]) * pz + Reg(m[4]) * py) + Reg(m[1]) * px;
    Reg sumX = (Reg(m[6]) * pz + Reg(m[3]) * py) + px * Reg(m[0]);
    out->x = StoreF(sumX);
    out->y = StoreF(sumY);
    out->z = StoreF(sumZ);
    return out;
}
