# Truffle Shader Package 1.0 Binary Format

Truffle Shader Package (TSP, `.tsp`; C++ `truffle::rhi::ShaderPackage`) uses one
deterministic little-endian container. Integers have
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
provided SHA-256 values (or Shader-compiler-computed dependency hashes) and is
not a package-signing mechanism. The product name/extension does not change
the magic, schema, enum values, or C++ type.

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

Reflection vectors encode bindings, push ranges, scalar specializations, then
**all** located stage IO, including internal varyings. Each IO record encodes
`u8 stage`, `u8 direction` (`0=input`, `1=output`), `u32 location`, then name,
type and interpolation strings. The unique logical key is
`(stage,direction,location)`, not the diagnostic name. Opposite directions at
the same stage/location are distinct records. Aggregates expand to one record
per occupied 32-bit scalar/vector location; built-ins are excluded.

After IO come six `u32` dimensions: required/default X/Y/Z, then preferred X/Y/Z.
They are followed by three optional specialization references in X/Y/Z order.
Each reference is a `u8` presence flag (`0` or `1`); only a present reference
has a following `u32` scalar specialization ID. ID zero is valid, not a sentinel.
Absent references mean fixed dimensions. Referenced scalar records must exist,
be positive signed/unsigned 32-bit integers, and have defaults equal to the
corresponding required/default dimension. Shared IDs are legal with consistent
types/defaults. Graphics-stage references, missing IDs and inconsistent defaults
are rejected. This represents direct scalar references, **not expressions**.

Selected `ShaderDesc` retains full directional IO, dimensions and axis IDs.
RHI resolves typed axis overrides before explicit pipeline-size comparisons,
axis/total device limits, native pipeline creation and retained dispatch
metadata. Total-invocation validation is overflow-safe. An unspecified pipeline
preferred size follows the resolved required size when the shader hint equals
its default required size; a distinct shader hint or explicit pipeline hint
remains an independent hint. Effective axis defaults are passed as typed native
specialization values too. See the [reflection policy and native limitations](../../src/shader/README.md#reflection-and-limits).

## Canonicalization And Compatibility

Creation sorts every unordered collection and rejects duplicate logical keys.
Loading revalidates the manifest, rebuilds the canonical container, and requires
byte-for-byte equality with the input. Equivalent descriptors therefore produce
identical bytes, and ambiguous encodings are rejected.

**This is an unreleased development format, not a released compatibility
contract.** The current container/schema identifiers remain `1` / `1.0` while
the draft evolves; source contracts, codec, fixtures and consumers change
together. The directional-IO/workgroup extension was approved in
[#186 comment 6527097946](https://github.com/truffle-graphics/truffle/issues/186#issuecomment-6527097946)
without a format or product-version bump. There are no legacy readers,
migrations, aliases or compatibility shims. Previously generated development
artifacts must be rebuilt. Stable format/versioning guarantees begin with a
release, not today's draft.

The current reader rejects a mismatched schema major or newer minor, invalid
enum/optional tags and noncanonical bytes. These checks do not promise
compatibility with earlier development snapshots bearing the same identifiers.
