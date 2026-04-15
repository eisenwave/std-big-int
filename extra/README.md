<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# Debugger pretty printers

Python pretty printers that render `beman::big_int::basic_big_int<...>` (and the
`beman::big_int::big_int` alias) as a single base-10 decimal, regardless of
whether the value is in the in-place static buffer or on the heap.

The `int128_printer_*.py` files provide the same treatment for `int128_t` /
`uint128_t` high/low pairs used by the wide-ops helpers.

## Loading the printers

### LLDB

```lldb
(lldb) command script import extra/big_int_printer_lldb.py
(lldb) command script import extra/int128_printer_lldb.py
```

To load automatically on every session, add the same lines to `~/.lldbinit`
with absolute paths.

### GDB

```gdb
(gdb) source extra/big_int_printer_gdb.py
(gdb) source extra/int128_printer_gdb.py
```

To load automatically on every session, add the same lines to `~/.gdbinit`
with absolute paths.

## Formatting

The summary is always decimal with thousands separators. The packed fields
(`m_capacity`, `m_size_and_sign`, `m_storage`, `m_alloc`) remain expandable
underneath for raw inspection.

### Static (in-place) storage — `m_capacity == 0`

```
(beman::big_int::big_int) b = 12,345 {
  m_capacity = 0
  m_size_and_sign = 1
  m_storage = {
    limbs = ([0] = 12345)
  }
  m_alloc = {}
}

(beman::big_int::big_int) c = -12,345 {
  m_capacity = 0
  m_size_and_sign = 2147483649   # 0x80000001 = sign bit set, limb_count = 1
  m_storage = {
    limbs = ([0] = 12345)
  }
  m_alloc = {}
}
```

### Dynamic (heap) storage — `m_capacity > 0`

A value whose magnitude still fits in-place but whose storage was reserved to
the heap (e.g. after `reserve(100)`) — the summary is identical:

```
(beman::big_int::big_int) f = 7 {
  m_capacity = 100
  m_size_and_sign = 1
  m_storage = {
    data = 0x00000001005bd6e0
  }
  m_alloc = {}
}
```

A genuinely large value (four 64-bit limbs) — the printer follows the pointer
and concatenates the limbs little-endian:

```
(beman::big_int::big_int) g = 30,877,890,463,284,318,779,200,455,886,624,330,043,260,257,791,495,576,751,883,077,282,378,867,085,585 {
  m_capacity = 4
  m_size_and_sign = 4
  m_storage = {
    data = 0x00000001005bdb70
  }
  m_alloc = {}
}
```

### Edge cases

- Zero prints as `0` — the sign bit is never set for zero magnitude, so there
  is no `-0`.
- `INT64_MIN` (`-9,223,372,036,854,775,808`) is represented as sign bit set
  plus magnitude `0x8000000000000000`, and the printer reconstructs the signed
  decimal correctly.

## Caveats

- Only allocators whose `std::allocator_traits<A>::pointer` is a raw pointer
  are supported. Fancy-pointer allocators will fall through to the error
  string.
- These scripts are not auto-loaded by the build; load them manually or from
  your `.lldbinit` / `.gdbinit`.
