"""Conversion of x87 80-bit values and ULP distances."""
import struct
from fractions import Fraction


def float80_to_fraction(raw: bytes):
    """Return a Fraction, or 'nan', '+inf', '-inf', for 10 bytes of an extended value."""
    mantissa = int.from_bytes(raw[:8], "little")
    se = int.from_bytes(raw[8:10], "little")
    sign = -1 if se & 0x8000 else 1
    exp = se & 0x7FFF
    if exp == 0x7FFF:
        if mantissa << 1 & ((1 << 64) - 1):
            return "nan"
        return "-inf" if sign < 0 else "+inf"
    if exp == 0:
        exp = 1
    return sign * Fraction(mantissa) * Fraction(2) ** (exp - 16383 - 63)


def round_fraction(value, mant_bits: int, exp_bits: int) -> int:
    """Round to nearest even into an IEEE binary format. Return the bit pattern as an int."""
    bias = (1 << (exp_bits - 1)) - 1
    total = 1 + exp_bits + mant_bits
    sign_bit = 1 << (total - 1)
    inf_bits = ((1 << exp_bits) - 1) << mant_bits
    if value == "nan":
        return inf_bits | (1 << (mant_bits - 1))
    if value == "+inf":
        return inf_bits
    if value == "-inf":
        return sign_bit | inf_bits
    sign = sign_bit if value < 0 else 0
    value = abs(value)
    if value == 0:
        return sign
    exp = value.numerator.bit_length() - value.denominator.bit_length()
    if Fraction(2) ** exp > value:
        exp -= 1
    exp = max(exp, 1 - bias)
    scaled = value / Fraction(2) ** (exp - mant_bits)
    q, r = divmod(scaled.numerator, scaled.denominator)
    if 2 * r > scaled.denominator or (2 * r == scaled.denominator and q & 1):
        q += 1
    if q >> (mant_bits + 1):
        q >>= 1
        exp += 1
    if q >> mant_bits == 0:
        return sign | q
    if exp + bias >= (1 << exp_bits) - 1:
        return sign | inf_bits
    return sign | ((exp + bias) << mant_bits) | (q & ((1 << mant_bits) - 1))


def float80_to_bits(raw: bytes, mant_bits: int, exp_bits: int) -> int:
    if int.from_bytes(raw[:8], "little") == 0 and int.from_bytes(raw[8:10], "little") & 0x7FFF == 0:
        # A zero keeps its sign: a Fraction cannot hold a negative zero.
        return (raw[9] >> 7) << (mant_bits + exp_bits)
    return round_fraction(float80_to_fraction(raw), mant_bits, exp_bits)


def float_to_ordered(bits: int, nbits: int) -> int:
    sign = 1 << (nbits - 1)
    return sign - (bits & (sign - 1)) if bits & sign else sign + bits


def ulp_distance(a_bits: int, b_bits: int, nbits: int):
    """Distance in ULPs; None if either value is NaN."""
    if is_nan(a_bits, nbits) or is_nan(b_bits, nbits):
        return None
    return abs(float_to_ordered(a_bits, nbits) - float_to_ordered(b_bits, nbits))


def is_nan(bits: int, nbits: int) -> bool:
    mant_bits = 23 if nbits == 32 else 52
    exp_mask = ((1 << (nbits - 1 - mant_bits)) - 1) << mant_bits
    return (bits & exp_mask) == exp_mask and (bits & ((1 << mant_bits) - 1)) != 0


def bits_to_float(bits: int, nbits: int) -> float:
    if nbits == 32:
        return struct.unpack("<f", bits.to_bytes(4, "little"))[0]
    return struct.unpack("<d", bits.to_bytes(8, "little"))[0]


def describe_float80(raw: bytes) -> str:
    value = float80_to_fraction(raw)
    if isinstance(value, str):
        return value
    return repr(float(value)) if abs(value) < Fraction(2) ** 1000 else "huge"
