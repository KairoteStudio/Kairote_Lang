# Native struct values

Structs contain inline value storage. Their layout is shared by semantic binding,
SSA machine code and VM bytecode; a struct is not a reference-class allocation.

```kairote
struct Pair { int64 x; int64 y; }
Pair Change(Pair value) { value.x += 10; return value; }
void Update(ref Pair value) { value.y += 1; }
int32 main() {
    Pair original = default(Pair);
    original.x = 7;
    Pair result = Change(original);
    Update(ref original);
    return original.x == 7 && original.y == 1 && result.x == 17 ? 0 : 1;
}
```

## Layout and storage

Fields use their actual scalar width and natural alignment, up to 16 bytes for
128-bit integers. Nested structs, generic specializations and fixed arrays use
their concrete layouts. A struct has no object-identity prefix; empty structs
occupy one byte. Tail padding aligns the complete value. `sizeof`, field
addresses, pointer arithmetic and array element strides use these physical
sizes. Reference classes retain their identity prefix and existing scalar slots;
embedded structs and fixed arrays occupy inline bytes.

```kairote
struct SockAddrIn {
    uint16 family;
    uint16 port;
    uint32 address;
    byte zero[8];
}
```

This type occupies 16 bytes, with fields at offsets 0, 2, 4 and 8. Fixed field
counts must be positive constant expressions. Their elements have no dynamic
array header. Whole-field assignment and `var copy = value.zero` copy all bytes
into independent storage. `.Length` is the declared count and still evaluates
its receiver once. Fixed arrays cannot implicitly become dynamic arrays, and
`delete` cannot release their inline storage.

Layout discovery uses an explicit dependency stack. A field stored by value
creates a dependency; a pointer or reference breaks it. Cyclic inline layouts,
invalid counts and excessive layouts report `E_LAYOUT`. A single layout or
function inline arena is limited to 16 MiB; the dependency stack supports
16,384 entries. These are checked compiler limits.

## Value semantics and calls

Uninitialized locals and `default(T)` zero the complete value, including padding.
`new T(...)` runs field initializers and the selected constructor. A struct with
only argument-taking constructors retains its implicit parameterless constructor;
`default(T)` always bypasses constructors.

Assignment, value parameters, array elements, globals and returns copy values.
Copies preserve overlapping source and destination ranges. Arguments are
evaluated once in source order, with each value captured before evaluating the
next argument, including named arguments. A return copies into caller-owned
storage before `finally` executes.

Struct methods receive the address of their storage. Calling a mutating method
on a readonly field or temporary uses a defensive copy. Readonly methods cannot
write through `this`. Temporaries and readonly values cannot be assigned or
passed as writable `ref` arguments.

Struct values and fixed arrays do not define a conversion to `bool`. Using a
complete inline value as an `if`, loop or ternary condition, or as an operand of
`&&` or `||`, is invalid. Select a boolean or scalar field, compare a field, or
call a method that returns a boolean instead. The address used to store the value
does not provide an implicit truth value. This rule applies after generic
inference as well as to directly declared structs and fixed arrays. Re.KrtC's
permissive treatment of some struct operands does not establish a conversion.

Struct return functions receive a hidden first result pointer. The source limit
remains 128 parameters including an instance receiver, so the physical call can
contain 129 arguments. Native frame arenas and VM v3 arenas are aligned to
16 bytes. SSA operations 70–72 represent frame addresses, copying and clearing;
the machine backend consumes them directly. Optimizations that would invalidate
aggregate frame lifetimes do not inline or eliminate these frames.

The `struct` constraint accepts struct values; `unmanaged` also examines nested
fields and rejects managed references, strings and dynamic arrays. Pointers,
function pointers and inline arrays of unmanaged elements remain eligible.

## Fixed arrays through generics

An inferred generic `T` can be a complete fixed array value. Element type,
element count, storage size and alignment participate in type identity, overload
matching and specialization names. A `byte[2]` field and a `byte[3]` field produce
different specializations of the same generic function; they cannot satisfy two
parameters that require one identical `T`. Fixed and dynamic arrays also remain
distinct.

```kairote
struct Buffers { byte two[2]; byte three[3]; }
T Copy<T>(T value) { return value; }
int32 Count<T>(T value) { return (int32)value.Length; }
```

Value parameters and returns copy the complete fixed array, including when a
generic owner stores `T` in a field or returns it through a function pointer.
`ref T` instead modifies the caller's array. Named arguments capture each array
before evaluating the next source argument; returning inside `try` captures the
result before `finally` can change the local source.

For a fixed array `T`, `T*` points to one complete array value. Dereference or
`pointer[0]` yields that array, and a subsequent index selects its scalar element.
`T[]`, array literals and `new T[count]` preserve the fixed shape of each element;
copying one row and changing another does not alias their inline bytes. Nested
fixed fields such as `Box<T>.rows[2]` retain both the outer count and the concrete
shape of `T`. The recursive element descriptor `native_element_type` carries
this information through binding, generic identity, copies and indexing.

Function pointers may be elements of fixed or dynamic arrays and fields of a
reference class. Each indexed element is a scalar callback; copying a fixed
callback array through generic `T` copies the callback values. The container
itself cannot be called or converted to one callback. A pointer to callback
storage must be dereferenced or indexed before calling it.

## Source libraries

Struct declarations and generic operations can reside in separate source files
of one compilation. Namespace imports and relative `import "file.krt"` paths
preserve their layouts and value semantics. The multi-file packet probes create
and return a nested packet, copy its fixed buffer through generic `T`, mutate the
original through `ref`, and use `System.Console` to print the saved values. Native
O0/O2 and VM O2 produce the same output and independent snapshots.

This source-library workflow merges the sources into one compilation. For
independently compiled native objects, ordinary public functions can consume and
return public concrete structs, including closed generic values. Import the same
layout declaration source in both modules and use consumer `extern` prototypes.
The ABI2 manifest records complete reachable layouts and the value/ref/callback
contract; the native driver compares exact bytes before linking. See
[struct object contracts](Projects.md#struct-objects-and-abi2) for diagnostics,
strong-definition rules and the distinction from bare ArkLink invocation.

## Verification and remaining boundaries

[test_struct_values.py](../../Test/SelfHost/test_struct_values.py) exercises native
O0–O3 and VM execution, physical layout, wide fields, overlapping copies, hidden
result arguments, generic layouts, construction, readonly receivers and invalid
operations. The [HTTP server](../../examples/http-server/README.md) uses the real
socket structure and Linux syscalls; its test sends actual loopback requests.

[test_fixed_generic_values.py](../../Test/SelfHost/test_fixed_generic_values.py)
checks fixed `T` identity, parameter and return copies, `ref`, pointers, nested
fixed fields, dynamic rows and generic-owner indirect calls in native O0–O3 and
VM execution. [test_function_containers.py](../../Test/SelfHost/test_function_containers.py)
checks indexed callback calls and rejects treating arrays or pointers as scalar
callbacks. [test_value_programs.py](../../Test/SelfHost/test_value_programs.py)
adds packet snapshots, argument ordering, mismatched array shapes and deep
inheritance overload selection.
[test_aggregate_conditions.py](../../Test/SelfHost/test_aggregate_conditions.py)
checks the rejection of complete inline conditions and logical operands, and
preserves side effects in valid scalar, boolean, reference and dynamic-array
conditions across native and VM O0–O3. The read-only source-library audit is recorded
in `build/selfhost-next-struct/isolated-report.json`, including compiler/source
hashes, exact commands and the independent expected results.

Struct inheritance/interfaces and boxing are not implemented. External native
signatures support concrete struct graphs; named classes, interfaces and enums
remain unsupported. Generic function templates need source declarations and are
not imported from binary objects. Shared method bodies do not receive weak/ODR
merging, and extern constructors remain unsupported. Explicit unsafe pointers
allow system interfaces such as `sockaddr_in`; they do not establish a general
foreign-function ABI.

[test_struct_linkage.py](../../Test/SelfHost/test_struct_linkage.py) exercises
independent objects, value/ref/hidden-result calls, typed callbacks, concrete
generic layouts, exact mismatch rejection, malformed manifests and preserved
outputs. These gates are separate from the source-library and VM tests.
