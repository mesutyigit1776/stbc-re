#include "ColorA.h"

#include "X87.h"

using x87::Reg;

// inline in the Appc wrapper of TGColorA.SetRGBA (0x5eebb0)
void TGColorA::SetRGBA(float nr, float ng, float nb, float na)
{
    r = nr;
    g = ng;
    b = nb;
    a = na;
}

// inline in the Appc wrapper of TGColorA.ScaleRGB (0x5eed30)
void TGColorA::ScaleRGB(float scale)
{
    r = StoreF(Reg(r) * scale);
    g = StoreF(Reg(g) * scale);
    b = StoreF(Reg(b) * scale);
}

// 0x5eed10
void TGColorA::Copy(NiColorA color)
{
    r = color.r;
    g = color.g;
    b = color.b;
    a = color.a;
}
