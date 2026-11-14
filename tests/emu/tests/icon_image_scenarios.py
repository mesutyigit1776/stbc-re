"""Scenarios and cases of the image chain, shared by the tests (icon_image_chain.py) and by the planted defects
(icon_image_defects.py). This module registers no test."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from emuharness import Inner  # noqa: E402
from icon_image_common import (CHAIN_SETUP, chain_case, counters_probe, dd_image, device, image_list_global,  # noqa: E402
                               image_scenario, log_probe, raw_bytes_probe, raw_image, renderer_list_global,
                               scratch_raw_global, slot_array, surface, surface_record, texture_format, tga_state_probe)


def tga(width, height, depth, seed=1):
    from icon_image_files import make_tga
    return make_tga(2 if depth != 8 else 3, width, height, depth, seed=seed)


SLOT_SETS = {
    "all": [10, 11, 12, 13, 14, 15, 16],
    "sparse": [None, 11, None, 13, None, 15, None],
    "last": [None] * 6 + [16],
    "none": [],
    "paletted": [(10, 1), (11, 1), (12, 1), (13, 1), (14, 1), (15, 1), (16, 1)],
}


def surface_roots(image, slots, dev=None, null_slots=False):
    roots = {"image": image, "dev": dev if dev is not None else device(3), "s": Obj("SurfaceCell"), "f": Obj("FormatCell")}
    roots["slots"] = slot_array(slots) if not null_slots else None
    return roots


def get_surface_scenario(image, slots, label, bit2=0, count=1, file=b"", null_slots=False, dev=None, **config):
    roots = surface_roots(image, slots, dev, null_slots)
    args = [Root("image"), Root("dev"), 0 if null_slots else Inner("slots", "slot0"), bit2, count, Root("s"), Root("f")]
    return image_scenario(roots, args, [], [scratch_raw_global()], label, file=file, device=config)


GET_SURFACE_ARGS = [PTR, PTR, PTR, U32, U32, PTR, PTR]


def get_surface_case(symbol, scenarios, **kw):
    return chain_case(0x7C6370, 0x7C6482, "thiscall", GET_SURFACE_ARGS, U8, symbol, scenarios,
                      setup_calls=CHAIN_SETUP, **kw)


def search_scenarios():
    scenarios = []
    dev = device(3)
    other = device(4)
    for slots in ("all", "sparse", "last"):
        for index in range(7):
            if SLOT_SETS[slots][index] is None:
                continue
            fid = SLOT_SETS[slots][index]
            for dirty in (0, 1):
                for ok in (1, 0):
                    records = [surface_record(surface(100 + index), fid, dev, dirty)]
                    scenarios.append(get_surface_scenario(
                        dd_image(flags=1, records=records), SLOT_SETS[slots],
                        "%s slots, a record for slot %d, dirty %d, refill %d" % (slots, index, dirty, ok), dev=dev,
                        refill_ok=ok))
    for bit2 in (0, 1):
        # the record is for another device, for another format, and there are several records
        scenarios.append(get_surface_scenario(dd_image(flags=1, records=[surface_record(surface(1), 10, other)]), SLOT_SETS["all"],
                                              "a record of another device", bit2=bit2, file=tga(4, 4, 24), dev=dev))
        scenarios.append(get_surface_scenario(dd_image(flags=1, records=[surface_record(surface(1), 99, dev)]), SLOT_SETS["all"],
                                              "a record of another format", bit2=bit2, file=tga(4, 4, 24), dev=dev))
        scenarios.append(get_surface_scenario(
            dd_image(flags=1, records=[surface_record(surface(1), 99, dev), surface_record(surface(2), 12, dev, 1),
                                       surface_record(surface(3), 11, dev)]), SLOT_SETS["all"],
            "three records, the second is dirty", bit2=bit2, file=tga(4, 4, 24), dev=dev))
    scenarios.append(get_surface_scenario(dd_image(flags=1, records=[surface_record(surface(1), 10, dev)]), SLOT_SETS["all"],
                                          "slots that are null", null_slots=True, dev=dev))
    return scenarios


def creation_scenarios():
    scenarios = []
    for slots in ("all", "sparse", "last", "none", "paletted"):
        for mask in (0, 1, 5, 6, 7):
            for depth in (24, 32):
                for flags in (0, 2):
                    for bit2 in (0, 1):
                        scenarios.append(get_surface_scenario(
                            dd_image(flags=flags, mask=mask), SLOT_SETS[slots],
                            "%s slots, mask %d, %d bits, flags %d, bit2 %d" % (slots, mask, depth, flags, bit2), bit2=bit2,
                            file=tga(5, 3, depth, mask)))
    return scenarios


def error_scenarios():
    scenarios = []
    base = SLOT_SETS["all"]
    file = tga(4, 4, 24)
    scenarios.append(get_surface_scenario(dd_image(name=None, raw=None), base, "no name and no raw image"))
    scenarios.append(get_surface_scenario(dd_image(name=None, raw=raw_image(8, 8, 1), flags=0), base, "no name, a raw image"))
    scenarios.append(get_surface_scenario(dd_image(name="Data/missing.tga"), base, "a file that does not open",
                                          stream_open=0))
    scenarios.append(get_surface_scenario(dd_image(name="Data/a.xyz"), base, "an extension that is not known", file=file))
    scenarios.append(get_surface_scenario(dd_image(name="Data/a"), base, "a name without a dot", file=file))
    scenarios.append(get_surface_scenario(dd_image(stream_data=True), base, "stream data", count=2))
    scenarios.append(get_surface_scenario(dd_image(stream_data=True), base, "stream data that the device refuses", stream_data_ok=0))
    for field in ("create_ok", "fill_ok"):
        scenarios.append(get_surface_scenario(dd_image(), base, field + " is false", file=file, **{field: 0}))
    for config in ({"palette_result": 1}, {"set_palette_result": 5}, {"fill_ok": 0}):
        for fmt in (0, 1, 2):
            raw = raw_image(4, 4, fmt, palette=(fmt == 0))
            scenarios.append(get_surface_scenario(dd_image(name=None, raw=raw), SLOT_SETS["paletted"],
                                                  "a paletted format, raw format %d, %s" % (fmt, config), **config))
    scenarios.append(get_surface_scenario(dd_image(name=None, raw=raw_image(4, 4, 0, palette=True)), SLOT_SETS["paletted"],
                                          "a paletted format, raw format 0, no device", dev=Obj("DdDevice", serial=3)))
    return scenarios


def load_file_scenario(image, label, given=None, slots=None, bit2=0, count=1, file=b"", dev=None, **config):
    roots = {"image": image, "dev": dev if dev is not None else device(3), "s": Obj("SurfaceCell"), "f": Obj("FormatCell"),
             "given": given, "slots": slot_array(slots) if slots is not None else None}
    args = [Root("image"), Root("dev"), Root("f"), count, Root("s"), Root("given"),
            Inner("slots", "slot0") if slots is not None else 0, bit2]
    return image_scenario(roots, args, [], [scratch_raw_global()], label, file=file, device=config)


LOAD_FILE_ARGS = [PTR, PTR, PTR, U32, PTR, PTR, PTR, U32]


def load_file_case(symbol, scenarios, **kw):
    return chain_case(0x7C5C60, 0x7C6079, "thiscall", LOAD_FILE_ARGS, U8, symbol, scenarios, setup_calls=CHAIN_SETUP, **kw)


def load_file_scenarios():
    scenarios = []
    for paletted in (0, 1):
        for fmt_id in (5, 9):
            for depth in (24, 32):
                scenarios.append(load_file_scenario(dd_image(), "given format %d, paletted %d, %d bits" % (fmt_id, paletted, depth),
                                                    given=texture_format(fmt_id, paletted), file=tga(6, 2, depth)))
    for width, height in ((1, 1), (2, 2), (3, 5), (4, 4), (8, 2), (16, 16), (7, 1), (1, 9), (64, 32), (100, 100), (256, 4)):
        scenarios.append(load_file_scenario(dd_image(), "size %d by %d" % (width, height), given=texture_format(5),
                                            file=tga(width, height, 24, width)))
    # a raw image of the image itself, with each format and each palette state
    for fmt in (0, 1, 2, 3):
        for palette in (True, False):
            for paletted in (0, 1):
                raw = raw_image(5, 3, fmt, palette=palette, count=2)
                scenarios.append(load_file_scenario(dd_image(name=None, raw=raw), "own raw image, format %d, paletted %d" % (fmt, paletted),
                                                    given=texture_format(5, paletted)))
    # the image has a name and a raw image: the raw image of the image is replaced for the call
    scenarios.append(load_file_scenario(dd_image(raw=raw_image(9, 9, 1, count=1)), "a name and a raw image", given=texture_format(5),
                                        file=tga(2, 2, 24)))
    scenarios.append(load_file_scenario(dd_image(name="Data/none.tga"), "no file", given=texture_format(5), stream_open=0))
    scenarios.append(load_file_scenario(dd_image(), "a given format that is null", given=None, file=tga(2, 2, 24)))
    scenarios.append(load_file_scenario(dd_image(flags=1, records=[surface_record(surface(5), 5, device(3))]), "an image with a record",
                                        given=texture_format(5), file=tga(2, 2, 24)))
    for config in ({"create_ok": 0}, {"fill_ok": 0}):
        scenarios.append(load_file_scenario(dd_image(), "the device refuses %s" % (config,), given=texture_format(5), file=tga(2, 2, 24),
                                            **config))
    return scenarios


def texture_manager(slots, flags=0, dev=None):
    members = {"slot%d" % i: (texture_format(*(entry if isinstance(entry, tuple) else (entry,))) if entry is not None else None)
               for i, entry in enumerate((list(slots) + [None] * 7)[:7])}
    return Obj("TextureManager", flags=flags, device=dev, **members)


def renderer(slots, flags=0, dev=None):
    return Obj("NiD3DRender", textureManager=texture_manager(slots, flags, dev))


def accept_scenarios(root):
    """Scenarios for the renderer (root 'renderer') or the texture manager (root 'manager')."""
    scenarios = []
    dev = device(3)
    for slots in ("all", "sparse", "none", "paletted"):
        for flags in (0, 4, 1, 0xFFFFFFFF):
            for image_flags in (0, 1):
                for depth in (24, 32):
                    target = renderer(SLOT_SETS[slots], flags, dev) if root == "renderer" else texture_manager(SLOT_SETS[slots], flags, dev)
                    image = dd_image(flags=image_flags, mask=7 if slots != "sparse" else 5)
                    roots = {root: target, "image": image, "dev": dev}
                    scenarios.append(image_scenario(roots, [Root(root), Root("image")], [], [scratch_raw_global()],
                                                    "%s slots, manager flags %#x, image flags %d, %d bits" % (slots, flags, image_flags, depth),
                                                    file=tga(6, 4, depth, flags)))
    # an image that has a record already, so that the search finds it
    for dirty in (0, 1):
        for ok in (1, 0):
            target = renderer(SLOT_SETS["all"], 0, dev) if root == "renderer" else texture_manager(SLOT_SETS["all"], 0, dev)
            image = dd_image(flags=1, records=[surface_record(surface(40), 12, dev, dirty)])
            roots = {root: target, "image": image}
            scenarios.append(image_scenario(roots, [Root(root), Root("image")], [], [scratch_raw_global()],
                                            "a record, dirty %d, refill %d" % (dirty, ok), file=tga(2, 2, 24), device={"refill_ok": ok}))
    # an image that has no source
    target = renderer(SLOT_SETS["all"], 0, dev) if root == "renderer" else texture_manager(SLOT_SETS["all"], 0, dev)
    scenarios.append(image_scenario({root: target, "image": dd_image(name=None)}, [Root(root), Root("image")], [],
                                    [scratch_raw_global()], "an image without a name"))
    return scenarios


def create_scenario(path, label, renderers, mode=1, mask=7, kind=2, file=b"", factory=True, flag2=0, flag4=0, **config):
    roots = {"renderers": None}
    args = [narrow(path) if path is not None else 0, mode, mask, kind]
    return image_scenario(roots, args, [], [renderer_list_global(renderers), scratch_raw_global(), image_list_global()], label,
                          file=file, factory=factory, flag2=flag2, flag4=flag4, device=config)


def create_renderers(count, dev_serials=(3, 3), slots="all", flags=0):
    return [renderer(SLOT_SETS[slots], flags, device(dev_serials[i % len(dev_serials)])) for i in range(count)]


def create_scenarios():
    scenarios = []
    file = tga(12, 5, 24, 3)
    for count in (0, 1, 2, 3):
        scenarios.append(create_scenario("Data/Icons/a.tga", "%d renderers" % count, create_renderers(count), file=file))
    scenarios.append(create_scenario("Data/Icons/a.tga", "two renderers with two devices", create_renderers(2, (3, 4)), file=file))
    scenarios.append(create_scenario("Data/Icons/a.tga", "no factory", create_renderers(1), file=file, factory=False))
    scenarios.append(create_scenario(None, "no path", create_renderers(1), file=file))
    scenarios.append(create_scenario(None, "no path and no factory", create_renderers(1), file=file, factory=False))
    for mode in (0, 1, 2, 3, 255):
        for kind in (0, 1, 2, 3):
            for flag2, flag4 in ((0, 0), (1, 0), (0, 1), (1, 1)):
                scenarios.append(create_scenario("Data/a.tga", "mode %d, kind %d, enables %d %d" % (mode, kind, flag2, flag4),
                                                 create_renderers(1), mode=mode, kind=kind, flag2=flag2, flag4=flag4, file=file))
    for mask in (0, 5, 7, 8, 100):
        scenarios.append(create_scenario("Data/a.tga", "mask %#x" % mask, create_renderers(1), mask=mask, file=file))
    # the renderer refuses the image
    for config in ({"create_ok": 0}, {"fill_ok": 0}, {"stream_open": 0}):
        for count in (1, 2):
            scenarios.append(create_scenario("Data/a.tga", "refusal %s, %d renderers" % (config, count), create_renderers(count),
                                             file=file, **config))
    for slots in ("none", "sparse", "last"):
        scenarios.append(create_scenario("Data/a.tga", "%s slots" % slots, create_renderers(1, slots=slots), file=file))
    # the sizes of the files
    for width, height, depth in ((1, 1, 24), (8, 8, 24), (8, 8, 32), (64, 32, 24), (100, 60, 32), (128, 128, 24), (3, 200, 8)):
        scenarios.append(create_scenario("Data/Icons/b.TGA", "file %dx%d, %d bits" % (width, height, depth), create_renderers(1),
                                         file=tga(width, height, depth, width)))
    scenarios.append(create_scenario("file://Data/Icons/c.tga", "a path with a prefix", create_renderers(1), file=file))
    scenarios.append(create_scenario("Data/Icons/c.bmp", "a bmp file", create_renderers(1), file=file))
    scenarios.append(create_scenario("Data/Icons/c.rgba", "an sgi file", create_renderers(1), file=file, sgi_ok=0))
    scenarios.append(create_scenario("Data/Icons/c.nif", "a nif file", create_renderers(1), file=file))
    return scenarios


def loader_scenario(path, label, file=b"", raw=None, **config):
    roots = {"raw": raw or raw_image(0, 0, 1)}
    return image_scenario(roots, [narrow(path) if path is not None else 0, Root("raw")], [], [], label, file=file, device=config)


LOADER_NAMES = ["Data/a.tga", "Data/a.TGA", "Data/a.Tga", "file://Data/a.tga", "file://a.tga", "a.tga", ".tga", "x.y.tga", "Data.d/a.tga",
                "Data/a.bmp", "Data/a.BMP", "Data/a.rgb", "Data/a.rgba", "Data/a.int", "Data/a.inta", "Data/a.RGBA", "Data/a.sgi",
                "Data/a.nif", "Data/a.NIF", "file://Data/a.nif", "Data/a", "Data/a.", "Data.d/a", "", "file://", "file", "a.tgaa", "a.tg",
                "x" * 250 + ".tga", "x" * 251 + ".tga", "file://" + "y" * 247 + ".tga"]


def loader_scenarios(null_path):
    scenarios = []
    file = tga(5, 3, 24, 8)
    if null_path:
        return [loader_scenario(None, "no path", file=file)]
    for name in LOADER_NAMES:
        for config in ({}, {"stream_open": 0}, {"bmp_ok": 0, "sgi_ok": 0}):
            scenarios.append(loader_scenario(name, "%r %s" % (name[:40], config), file=file, **config))
    scenarios.append(loader_scenario("a.tga", "a file that the loader refuses", file=b"\0" * 18))
    return scenarios


def raw_file_scenario(path, label, file=b"", **config):
    return image_scenario({}, [narrow(path)], [], [scratch_raw_global()], label, file=file, device=config)


# The static initializers and the functions at the end of the program ---------------------------------------------------------

from emuharness import Global  # noqa: E402
from icon_image_common import exit_probe  # noqa: E402
from prop_common import trap_probe  # noqa: E402

INIT_PROBES = [log_probe(), trap_probe(), counters_probe(), exit_probe()]


def init_case(first, last, symbol, scenarios, **kw):
    return chain_case(first, last, "cdecl", [], None, symbol, scenarios, setup_calls=CHAIN_SETUP, probes=INIT_PROBES, **kw)


def old_list(kind):
    """A global list that holds something, so that the initializer shows that it makes the list empty."""
    node = Obj(kind + "Node", data=None, next=None, prev=None)
    block = Obj(kind + "Block", nextBlock=None, nodes=node)
    return Obj(kind + "List", count=1, head=node, tail=node, freeNodes=None, blocks=block, blockSize=9)


def image_list_init_scenarios():
    return [image_scenario({}, [], [], [image_list_global()], "a blank list"),
            image_scenario({}, [], [], [Global("imageList", IMAGE_LIST, old_list("ImageAll"), "bc_init_global_ImageAllList")],
                           "a list with a node")]


def renderer_list_init_scenarios():
    return [image_scenario({}, [], [], [renderer_list_global([])], "a blank list"),
            image_scenario({}, [], [], [Global("rendererList", RENDERER_LIST, old_list("Renderer"), "bc_init_global_RendererList")],
                           "a list with a node")]


def scratch_raw_init_scenarios():
    blank = Global("scratch", SCRATCH_RAW, Obj("RawImagePointer", image=None), "bc_init_global_RawImagePointer")
    return [image_scenario({}, [], [], [blank], "a blank pointer", object_count=n) for n in (0, 9)]


def release_image_list_scenarios():
    scenarios = []
    for count in (0, 1, 3):
        for guard in (0, 1):
            pre = [Pre("create", []) for _ in range(count)]
            scenarios.append(image_scenario({}, [], pre, [image_list_global()], "%d images, guard %d" % (count, guard),
                                            guards=(guard, 0)))
    return scenarios


def release_renderer_list_scenarios():
    scenarios = []
    for guard in (0, 1, 2, 3):
        scenarios.append(image_scenario({}, [], [], [Global("rendererList", RENDERER_LIST, old_list("Renderer"), "bc_init_global_RendererList")],
                                        "a list with a node, guards %d" % guard, guards=(0, guard)))
        scenarios.append(image_scenario({}, [], [], [renderer_list_global([])], "an empty list, guards %d" % guard,
                                        guards=(0, guard)))
    return scenarios


def release_scratch_scenarios():
    scenarios = []
    for count in (1, 2, 5):
        for guard in (0, 1, 2, 3):
            scenarios.append(image_scenario({}, [], [], [scratch_raw_global(raw_image(0, 0, 1, count=count))],
                                            "count %d, guards %d" % (count, guard), guards=(0, guard)))
    scenarios.append(image_scenario({}, [], [], [Global("scratch", SCRATCH_RAW, Obj("RawImagePointer", image=None),
                                                         "bc_init_global_RawImagePointer")], "no raw image"))
    return scenarios


# The destructors ---------------------------------------------------------------------------------------------------------

def raw_destroy_scenarios():
    scenarios = []
    for state in ((0, 8, 4), (1, 8, 4), (2, 8, 4), (5, 8, 4), (1, 0, 0)):
        for flags in (0, 1, 2, 3):
            scenarios.append(image_scenario({"raw": Obj("NiRawImageData")}, [Root("raw"), flags],
                                            [Pre("ctor", [Root("raw")]), Pre("allocate", [Root("raw")] + list(state))], [],
                                            "state %s, flags %d" % (state, flags), object_count=5))
    scenarios.append(image_scenario({"raw": Obj("NiRawImageData")}, [Root("raw"), 1], [Pre("ctor", [Root("raw")])], [],
                                    "a new image", object_count=1))
    return scenarios


def image_destroy_scenarios():
    scenarios = []
    for name in ("Data/a.tga", "", None):
        for raw_count in (None, 1, 2):
            for flags in (0, 1):
                raw = None if raw_count is None else raw_image(4, 4, 1, count=raw_count)
                image = Obj("NiImage", refCount=0, name=name, rawImage=raw, width=4, height=4, mipLevels=1, id=3, reserved20=0,
                            flags=0, mask=7, scale=128.5)
                scenarios.append(image_scenario({"image": image}, [Root("image"), flags], [], [],
                                                "name %r, raw image count %s, flags %d" % (name, raw_count, flags), object_count=5))
    return scenarios


def destroyed_model(cls):
    """The destructor of the exe gives the object the vtable of the base class (0x898b94). For the check after the call this
    vtable stands for the class."""
    from icon_image_common import Model, image_classes, native_api, LIB
    model = Model(image_classes(), native_api(LIB))
    model.by_vtable[0x898B94] = cls
    return model


# The texture property ------------------------------------------------------------------------------------------------------

def property_obj(cls, name="Data/a", extra=None, controller=None, texture=None, flags=0, refs=0):
    members = dict(refCount=refs, name=name, extraData=extra, controller=controller)
    if cls == "NiTextureProperty":
        members.update(flags=flags, texture=texture)
    return Obj(cls, **members)


def property_states(cls):
    states = []
    for name in ("Data/a", "", None):
        for extra in (None, 1, 2):
            for controller in (None, 1, 3):
                for texture in ((None,) if cls == "NiObjectNET" else (None, 1, 2)):
                    states.append((name, extra, controller, texture))
    return states


def property_build(cls, state, flags=0):
    name, extra, controller, texture = state
    raw = lambda count: None if count is None else raw_image(2, 2, 1, count=count)
    image = None if texture is None else dd_image(name="x", count=texture, image_id=9)
    return property_obj(cls, name, raw(extra), raw(controller), image, flags)


def property_constructor_scenarios(cls):
    return [image_scenario({"p": Obj(cls)}, [Root("p")], [], [], "object count %d" % n, object_count=n) for n in (0, 4, 99)]


def property_destroy_scenarios(cls, with_flags):
    scenarios = []
    for state in property_states(cls):
        for flags in ((0, 1) if with_flags else (None,)):
            args = [Root("p")] + ([flags] if with_flags else [])
            scenarios.append(image_scenario({"p": property_build(cls, state, 7)}, args, [], [],
                                            "state %s flags %s" % (state, flags), object_count=5))
    return scenarios
