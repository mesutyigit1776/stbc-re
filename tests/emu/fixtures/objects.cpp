/* Native objects for the object self-tests.
 *
 * The exe-side layout that the tests describe (32-bit):
 *   Node    0x14 bytes: vtbl 0x00, name 0x04, weight 0x08, next 0x0C, tag 0x10
 *   Special 0x18 bytes: Node, extra 0x14
 *   Vec3    0x0C bytes: x 0x00, y 0x04, z 0x08
 *   Holder  0x24 bytes: vtbl 0x00, left 0x04, right 0x08, count 0x0C, items 0x10, label 0x14, pos 0x18
 * The native classes carry more members (the class name, 64-bit pointers); the field table
 * lists only the members that the tests compare.
 */
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>

#include "bc_object.h"

static std::map<std::string, long> live;

struct Object {
    explicit Object(const char *name, bool counted = true) : cls(name), counted(counted) {
        if (counted) live[cls]++;
    }
    virtual ~Object() {
        if (counted) live[cls]--;
    }
    const char *cls;
    bool counted;
};

struct Node : Object {
    explicit Node(const char *name = "Node", bool counted = true) : Object(name, counted) {}
    virtual int weight_scale() { return 1; }
    char *name = nullptr;
    float weight = 0;
    Node *next = nullptr;
    int32_t tag = 0;
};

struct Special : Node {
    Special() : Node("Special") {}
    Node *extra = nullptr;
};

struct Vec3 {
    float x, y, z;
};

struct Holder : Object {
    Holder() : Object("Holder") {}
    Node *left = nullptr;
    Node *right = nullptr;
    int32_t count = 0;
    Node **items = nullptr;
    wchar_t *label = nullptr;
    Vec3 pos = {0, 0, 0};
};

BC_FIELDS(Node)
    BC_FIELD(Node, name) BC_FIELD(Node, weight) BC_FIELD(Node, next) BC_FIELD(Node, tag)
BC_FIELDS_END

BC_FIELDS(Special)
    BC_FIELD(Special, name) BC_FIELD(Special, weight) BC_FIELD(Special, next) BC_FIELD(Special, tag)
    BC_FIELD(Special, extra)
BC_FIELDS_END

BC_FIELDS(Vec3)
    BC_FIELD(Vec3, x) BC_FIELD(Vec3, y) BC_FIELD(Vec3, z)
BC_FIELDS_END

BC_FIELDS(Holder)
    BC_FIELD(Holder, left) BC_FIELD(Holder, right) BC_FIELD(Holder, count) BC_FIELD(Holder, items)
    BC_FIELD(Holder, label) BC_FIELD(Holder, pos)
BC_FIELDS_END

extern "C" {

Node *bc_new_Node() { return new Node(); }
Node *bc_new_Special() { return new Special(); }
Holder *bc_new_Holder() { return new Holder(); }

const char *bc_class_name(const Object *obj) { return obj->cls; }
int64_t bc_live_count(const char *cls) { return live[cls]; }

/* One global node. The function builds it again, as a new object, on each call. */
alignas(Node) static unsigned char global_storage[sizeof(Node)];
static Node *global_node = nullptr;

Node *fx_init_global() {
    if (global_node) global_node->~Node();
    global_node = new (global_storage) Node("Node", false);
    return global_node;
}

uint32_t fx_noop(void *) { return 0; }

uint32_t fx_tag_list(Node *head, uint32_t base) {
    for (Node *n = head; n; n = n->next) n->tag = base++;
    return base;
}

/* Known defect: writes the weight, not the tag. */
uint32_t fx_tag_list_wrong_field(Node *head, uint32_t base) {
    for (Node *n = head; n; n = n->next) n->weight = (float)base++;
    return base;
}

static Node *make_tagged_node() {
    Node *node = new Node();
    node->tag = 99;
    return node;
}

void fx_append_node(Node *head) {
    Node *tail = head;
    while (tail->next) tail = tail->next;
    tail->next = make_tagged_node();
}

/* Known defect: makes a second node and drops it. */
void fx_append_node_leak(Node *head) {
    fx_append_node(head);
    make_tagged_node();
}

/* Known defect: links the new node after the head, not at the end. */
void fx_append_node_second(Node *head) {
    Node *node = make_tagged_node();
    node->next = head->next;
    head->next = node;
}

/* Known defect: the right pointer gets a copy of the left node, not the same node. */
void fx_unshare(Holder *holder) {
    Node *copy = new Node();
    copy->tag = holder->left->tag;
    holder->right = copy;
}

void fx_bump_global() { global_node->tag++; }

int32_t fx_get_tag(Node *node) { return node->tag; }
void fx_set_tag(Node *node, int32_t tag) { node->tag = tag; }

}
