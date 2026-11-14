#pragma once
#include <cstdint>
#include <cstring>

// The class name strings are returned as the address of the same string in the exe, so that a test compares addresses.
inline std::uint32_t ExeAddressOfString(const char* text)
{
    struct Row {
        const char* text;
        std::uint32_t address;
    };
    static const Row rows[] = {{"TGObject", 0x95b05c},           {"_p_TGObject", 0x9142b0},
                               {"TGObjectPtr", 0x95b270},        {"TGModelProperty", 0x95b00c},
                               {"_p_TGModelProperty", 0x914088}, {"TGModelPropertyPtr", 0x95b01c},
                               {"TGModelPropertySet", 0x8e5d1c},
                               {"_p_TGModelPropertySet", 0x8e5d30},
                               {"TGModelPropertySetPtr", 0x8e5d48},
                               {"SubsystemProperty", 0x959590},  {"_p_SubsystemProperty", 0x914050},
                               {"SubsystemPropertyPtr", 0x9595a4}};
    for (const Row& row : rows) {
        if (std::strcmp(text, row.text) == 0)
            return row.address;
    }
    return 0;
}
