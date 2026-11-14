class HarnessError(Exception):
    """The original-side run stopped for a reason that is not a result mismatch."""


class UnstubbedTarget(HarnessError):
    """Control left the allowed functions for an address that has no stub."""


class UnmappedAccess(HarnessError):
    """The emulated code touched memory that is not mapped."""


class RunawayExecution(HarnessError):
    """The instruction limit was reached."""


class UnallowedVirtualCall(UnstubbedTarget):
    """Control left the allowed functions through a slot of a known vtable."""
