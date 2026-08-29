# ShaderPackage 1.0 Binary Format

ShaderPackage uses one deterministic little-endian container. Integers have
fixed widths; strings are a 32-bit byte length followed by UTF-8 bytes; vectors
are a 32-bit element count followed by elements. Runtime limits are 256 MiB per
package, 1 MiB per string, and 65,535 records per vector.

## Container Header

| Field | Encoding | Value |
|---|---:|---|
| magic | 8 bytes | `TRFSHPKG` |
| container version | `u16` | `1` |
| schema major | `u16` | `1` |
| schema minor | `u16` | `0` |
| reserved | `u16` | zero |
| manifest size | `u64` | encoded manifest byte count |
| manifest hash | `u64` | FNV-1a 64 over manifest bytes |
| blob count | `u32` | must equal variant count |
| reserved | `u32` | zero |

The canonical manifest follows the header. Each variant then contributes one
blob record in canonical variant order: `u64 size`, `u64 FNV-1a hash`, and the
blob bytes. No padding or trailing data is permitted. The integrity hashes
detect accidental corruption; source provenance separately records caller-
provided SHA-256 values and is not a package-signing mechanism.

## Manifest Order

The manifest encodes these fields in order:

1. package name;
2. sorted unique required features;
3. named permutations, defines, and specialization constants;
4. logical-to-native binding remaps;
5. source paths, language identities, and lowercase SHA-256 values;
6. compiler names, versions, and revisions;
7. bounded diagnostics;
8. target variant metadata and normalized reflection.

Variant metadata records target, byte format, provenance kind, stage, entry
point, permutation, logical bindings, push constants, specialization constants,
interfaces, and required and preferred workgroup sizes. Blob bytes are outside
the manifest and correspond by canonical variant index.

## Canonicalization And Compatibility

Creation sorts every unordered collection and rejects duplicate logical keys.
Loading revalidates the manifest, rebuilds the canonical container, and requires
byte-for-byte equality with the input. Equivalent descriptors therefore produce
identical bytes, and ambiguous encodings are rejected.

A schema major mismatch or a newer schema minor is rejected. A future compatible
reader may raise its supported minor; incompatible layout or semantic changes
require a new major. Enum ordinals are part of schema 1.0 and may not be
reassigned within that schema.
