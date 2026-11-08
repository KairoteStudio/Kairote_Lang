# Native VM bytecode

`SelfHost/krtc source.krt target vm -o program` writes real bytecode to `program`
and the compatibility sidecar `program.ebc`. Run either file with
`SelfHost/krtc run-vm program`. The interpreter and serializers are written
in Kairote and run inside the native compiler process. VM compilation does not
invoke ArkLink, a C compiler, Python, or the seed compiler.

The VM executes native integer widths, float32/float64, int128/uint128 values,
locals and references, branches, function and indirect calls, globals and their
initializers, classes and arrays, memory allocation, strings, exceptions, and
Linux syscalls. EBC v4 also supports lexical closures through the same typed
callback values as free functions. Process exit status is the value returned by `main`; stdout and
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
Modules without aggregate metadata or closure environment instructions continue
to use version 1 or 2.

## EBC version 4

Version 4 adds tagged closure values and opcode `73`, `closure.environment`.
The header, 28-byte function records, 24-byte instructions and data layout are
identical to version 3, with version `4` at header offset 4. A module containing
opcode `73` is serialized as version 4 even when it has no aggregate values.

A free function or noncapturing lambda is a positive handle: its function index
plus one. A capturing lambda is a 64-bit word with the high bit set. Clearing
that bit gives the address of a 16-byte descriptor holding the function handle
at offset 0 and environment address at offset 8. An indirect call resolves the
handle and saves the environment in a separate frame field. Opcode `73` pushes
that environment; it must be the first instruction of its function and both
operands must be zero. Direct/free function calls use environment zero.

The environment does not consume a source parameter or the aggregate hidden
result pointer. The existing 128-source-parameter limit and aggregate result
metadata remain unchanged. Descriptor and environment memory are checked by
the interpreter before use; expired descriptors or invalid handles report
`E_VM`. The compiler's ordinary IR performs cell sharing, scope cleanup and
explicit callback deletion in both native and VM execution.

The interpreter still reads versions 1, 2 and 3 with their original callable
rules. Versions 2 and 3 reject opcode `73`; their indirect calls accept ordinary
function handles, not tagged closure descriptors. New bytecode does not change
the meaning of old chunks. Closure syntax, ownership and borrow boundaries are
documented in [Closures.md](Closures.md).

## EBC version 5

Version 5 adds runtime type descriptors for boxing, checked casts, interface
dispatch and typed exceptions. It keeps the 32-byte header, 28-byte function
records and 24-byte instructions. After the ordinary module data, a runtime
footer contains these little-endian fields:

| Field | Storage |
| --- | --- |
| Footer magic `0x3554524b` (`KRT5`) | 32 bits |
| Runtime blob byte length | 32 bits |
| Type descriptor count | 32 bits |
| Relocation count | 32 bits |
| Descriptor offsets within the blob | One signed 32-bit offset per type |
| Relocations | Three signed 32-bit fields per record: offset, kind, target |
| Runtime blob | The declared number of bytes, with relocation pointers zero |

A relocation patches one 64-bit pointer. Kind `0` targets an offset in the same
blob; kind `1` targets a descriptor by type index; kind `2` targets a VM function
handle, the function index plus one. Negative external function targets are
rejected. The blob is limited to 16 MiB and 65,536 descriptors; the complete
artifact remains limited to 64 MiB.

Descriptors occupy 96 bytes at 16-byte aligned offsets. Their 64-bit fields are
hash, canonical key length, key pointer, type kind, value storage size, alignment,
base descriptor, interface count, interface table pointer, method count, method
table pointer and flags, in that order. Method entries occupy 40 bytes: method
key hash, key length, key pointer, function handle and receiver adjustment.
Boxed struct methods use a 16-byte adjustment to reach the value payload.
Canonical bytes determine type and method equality; a hash only selects lookup
candidates.

The reader validates exact lengths, every descriptor and table range, storage
and alignment, type/base/interface relationships, key hashes, function targets,
relocation ownership and zero pointer fields before patching. Shared identical
key ranges are permitted; overlapping unrelated records and partial key aliases
are rejected. It copies the validated blob into an aligned runtime allocation,
then applies relocations. This region can be read during execution but cannot
be stored through, cleared, overwritten by a value copy or explicitly freed by
the program. The interpreter releases it after execution.

Opcode `74` pushes a descriptor address: `args` is a valid type index and `extra`
is zero. Opcode `75` enters a global initialization guard, consuming its address
and returning whether this invocation acquired initialization. Both operands
are zero. Opcode `76` finishes that guard; `args` is zero and `extra` is one for
success or zero to reset after an exception. VM guard execution is single
threaded. Earlier versions reject these instructions.

## EBC version 6

Version 6 keeps version 5's serialization layout and footer magic. Its version
field distinguishes the wide integer value convention: int128 and uint128 have
16-byte inline storage in locals, parameters, fields, arrays, captures and
returns. Value arguments copy their bytes; wide results use the aggregate hidden
result pointer. Each wide-producing instruction receives fixed temporary space
in its current call frame. A repeated operation reuses that space, and frame
exit releases it with the frame rather than accumulating heap allocations.
Reads take a value snapshot, including when a later operand modifies the source.

The compiler currently emits version 6 for runtime descriptors, initialization
guard instructions or wide operations. Version 6 rejects the legacy wide-field
reference opcode `69` and requires packed inline storage for wide increment
opcode `67`. Versions 1–5 remain readable with their original wide convention;
their pointer-backed wide temporaries retain the old immutable allocation
semantics. Reading a newer artifact does not silently change an older file's
value ABI.

[test_vm_runtime_types.py](../../Test/SelfHost/test_vm_runtime_types.py) checks
descriptor dispatch, checked conversions, malformed footers and relocations,
read-only metadata, all serialized blob alignment residues and an actual legacy
version 5 wide-alias program. [test_wide_lifetimes.py](../../Test/SelfHost/test_wide_lifetimes.py)
checks value snapshots, references, calls, captures and repeated arithmetic in
native and VM execution.

Project builds cache the primary bytecode and its `.ebc` sidecar. Cache restore
recreates missing outputs; `clean` removes an output only when its hash still
matches the last successful build.
