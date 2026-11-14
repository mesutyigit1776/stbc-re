"""Run single functions of stbc.exe in Unicorn and compare them with native code.

A test registers a function that returns a Case:

    @register("area.name")
    def case():
        return Case(Original(0x860440, 0x7B, "cdecl", [Buf(io="in")], U32),
                    Native(build_fixture("lib.c"), "native_symbol"),
                    inputs.single([b"abc\0"]), cw=DEFAULT_CW, ulps={"ret": 0})
"""
from .case import Case, check_exe_hash, exe_sha256
from .compare import describe_cw
from .graph import compare_graphs
from .layout import (CSTR, VTBL, WSTR, Accessor, Array, ClassDef, Inline, Member, Model, NativeApi, Ptr)
from .objectcase import ObjectCase, Pre, Probe, Scenario, SetupCall
from .objects import Built, Global, GraphReader, Inner, Obj, ObjectBuilder, Root
from .sides import GuestSide, NativeSide
from .errors import (HarnessError, RunawayExecution, UnallowedVirtualCall, UnmappedAccess,
                     UnstubbedTarget)
from .machine import CW_24BIT, DEFAULT_CW, SCRATCH, Machine, Original
from .native import Native, build_fixture
from .registry import expect_error, register
from .stubs import Stub, free_stub, malloc_stub, pool_stubs
from .types import (F32, F64, I8, I16, I32, I64, PTR, U8, U16, U32, U64, VOID, Buf, Raw, St0)
from . import inputs
