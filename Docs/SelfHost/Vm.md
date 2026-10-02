# Native VM bytecode

`SelfHost/krtc source.krt target vm -o program` writes real bytecode to `program`
and the compatibility sidecar `program.ebc`. Run either file with
`SelfHost/krtc run-vm program`. The interpreter and serializers are written
in Kairote and run inside the native compiler process. VM compilation does not
invoke ArkLink, a C compiler, Python, or the seed compiler.

The VM executes native integer widths, float32/float64, boxed int128/uint128,
locals and references, branches, function and indirect calls, globals and their
initializers, classes and arrays, memory allocation, strings, exceptions, and
Linux syscalls. Process exit status is the value returned by `main`; stdout and
stderr follow the program's syscalls. A VM entry function takes no arguments.
Precompiled KRO objects and external KRO calls are rejected because EBC does not
contain a native linker table.

Bytecode is a program, with access to Linux syscalls. The interpreter is not a
sandbox. It validates file lengths, version, metadata, instruction operands and
control-flow stack depths, and tracks VM memory so invalid addresses and expired
references produce `E_VM` diagnostics.

## EBC version 1

The magic is little-endian `0x45534243`, followed by a 16-bit version `1`, a
32-bit code length, the opcode bytes, one signed 32-bit source line per code
byte, and a 32-bit constant count. Constants begin with a 32-bit tag: boolean
`0` followed by a byte; null `1`; number `2` followed by IEEE double bytes;
string `4` followed by a 16-bit byte length and UTF-8 bytes. Object constants
cannot be reconstructed and are rejected.

Constant-return entry functions are emitted in this seed-compatible format
when their integer result is exactly representable as a double. The native
interpreter also reads seed-produced v1 chunks and executes scalar arithmetic,
comparisons, locals, jumps, and returns. V1 has no function table, so global
name lookup and named function calls are rejected. Number printing uses exact
decimal rounding to six significant digits in general format (`%g`).

## EBC version 2

Version 2 preserves the native stack IR and exact 64-bit operands. All integer
fields are little-endian. The 32-byte header contains:

| Offset | Field |
| --- | --- |
| 0 | 32-bit magic `0x45534243` |
| 4 | 16-bit version `2` |
| 6 | 16-bit reserved zero |
| 8 | 32-bit instruction count |
| 12 | 32-bit function count |
| 16 | Signed 32-bit entry function index |
| 20 | 32-bit global slot count |
| 24 | Signed 32-bit initializer function index, or `-1` |
| 28 | 32-bit data byte length |

Each function then occupies 12 bytes: signed 32-bit start instruction, local
slot count, and parameter count. Each instruction occupies 24 bytes: 32-bit
`KrtNativeOp`, 32-bit reserved zero, signed 64-bit `args`, signed 64-bit `extra`.
Raw module data follows the instruction table. Branch targets are instruction
indices; function targets are indices in the function table. Function addresses
are VM handles, not native machine-code addresses.

Limits match the compiler IR: 16,777,216 instructions, 16,384 functions, 128
parameters per function including an instance receiver, and 262,144 data bytes. Serialized files are limited to
64 MiB; larger bytecode modules report `E_VM`. Execution allows 262,144 operand
slots and 4,096 active calls. Memory allocation and syscalls retain the native
Linux semantics. Native IR optimizations run before bytecode serialization.

## EBC version 3

Version 3 adds inline aggregate storage and the value return ABI for structs
and fixed arrays, including generic specializations. Its header,
instructions and data use the version 2 formats, with version `3` at offset 4.
Each function record occupies 28 bytes:

| Offset | Signed 32-bit field |
| --- | --- |
| 0 | First instruction |
| 4 | Scalar local slot count |
| 8 | Source parameter count |
| 12 | Inline frame arena byte size |
| 16 | Aggregate return byte size, or zero |
| 20 | Aggregate return alignment |
| 24 | Hidden result local slot, or `-1` |

An aggregate return adds a first physical argument pointing to storage owned by
the caller. Source parameters keep their original local indices and the limit
of 128, including an instance receiver. Calls can therefore pass 129 physical
arguments. The hidden pointer is saved separately in its declared local slot.

Opcode `70` produces an address at a nonnegative byte offset in the frame's
inline arena (`args` is the offset, `extra` is the storage size). Opcode `71`
copies overlapping byte ranges correctly; opcode `72` zeroes a byte range.
For both memory operations, `args` is the byte size and `extra` is the alignment.
Copies are explicit for struct and fixed-array assignment, value parameters and
returns. Frame arenas are aligned to 16 bytes and expire when their call exits;
the interpreter checks the complete source and destination ranges for copies.
Modules without this metadata continue to use version 1 or 2.

Project builds cache the primary bytecode and its `.ebc` sidecar. Cache restore
recreates missing outputs; `clean` removes an output only when its hash still
matches the last successful build.
