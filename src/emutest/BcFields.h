#pragma once
#include <stddef.h>
#include <stdint.h>

/* Field table of a native class: the harness reads the offsets at run time.
 *
 *   BC_FIELDS(Node)
 *     BC_FIELD(Node, name)
 *     BC_FIELD(Node, next)
 *   BC_FIELDS_END
 */
struct BcField {
    const char *name;
    uint32_t offset;
};

#define BC_FIELDS(cls) extern "C" const BcField bc_fields_##cls[] = {
#define BC_FIELD(cls, member) {#member, (uint32_t)offsetof(cls, member)},
#define BC_FIELDS_END {nullptr, 0}};
