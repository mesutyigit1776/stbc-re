#include "SubsystemProperty.h"
#include "TGModelProperty.h"
#include "TGModelPropertySet.h"

// Enters the creators of the classes into the class factory table, in the order of the exe (E0088): first the code
// at 0x43bdf0 (SubsystemProperty and the other classes), then the code at 0x6e8980 (TGModelProperty and
// TGModelPropertySet). The table exists before this runs (0x6f08b0).
void RegisterClassCreators()
{
    SubsystemProperty::RegisterCreator();
    TGModelProperty::RegisterCreator();
    TGModelPropertySet::RegisterCreator();
}
