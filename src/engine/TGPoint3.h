#pragma once
#include "NiPoint3.h"

struct TGMatrix3;
struct TGStream;

struct TGPoint3 : NiPoint3 {
    TGPoint3() = default;
    TGPoint3(float x, float y, float z) : NiPoint3(x, y, z) {}

    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetZ() const { return z; }
    void SetX(float v) { x = v; }
    void SetY(float v) { y = v; }
    void SetZ(float v) { z = v; }
    void SetXYZ(float nx, float ny, float nz);
    void Add(const TGPoint3& other);
    void Subtract(const TGPoint3& other);
    void Scale(float scale);
    void UnitizeVector();
    double VectorLength() const;

    void Set(float nx, float ny, float nz);
    TGPoint3* Cross(TGPoint3* out, const TGPoint3* other) const;
    TGPoint3* UnitCross(TGPoint3* out, const TGPoint3* other) const;
    TGPoint3* GetAlignedComponent(TGPoint3* out, const TGPoint3* other) const;
    TGPoint3* GetPerpendicularComponent(TGPoint3* out, const TGPoint3* other) const;
    void MultMatrix(const TGMatrix3* m);
    void MultMatrixLeft(const TGMatrix3* m);
    void LoadBinary(TGStream* stream);
    void SaveBinary(TGStream* stream) const;

    static TGPoint3* GetRandomUnitVector(TGPoint3* out);
    static TGPoint3* GetModelForward(TGPoint3* out);
    static TGPoint3* GetModelBackward(TGPoint3* out);
    static TGPoint3* GetModelUp(TGPoint3* out);
    static TGPoint3* GetModelDown(TGPoint3* out);
    static TGPoint3* GetModelRight(TGPoint3* out);
    static TGPoint3* GetModelLeft(TGPoint3* out);
    static void UnitizeVectors(TGPoint3* vectors, unsigned count);

    static TGPoint3 modelForward;
    static TGPoint3 modelBackward;
    static TGPoint3 modelUp;
    static TGPoint3 modelDown;
    static TGPoint3 modelRight;
    static TGPoint3 modelLeft;
    static TGPoint3 zero;

    static void InitModelForward();
    static void InitModelBackward();
    static void InitModelUp();
    static void InitModelDown();
    static void InitModelRight();
    static void InitModelLeft();
    static void InitZero();
};

static_assert(sizeof(TGPoint3) == 12, "TGPoint3 size");
