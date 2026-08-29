# Backend-Private Dependencies

These checked-out sources are optional backend groups. CMake never downloads
them. They are linked privately by the targets that select them and are not
exported through `Truffle::RHI`.

| Path | Revision | Upstream | License | Consumer |
|---|---|---|---|---|
| `vulkan-headers` | `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77` (`vulkan-sdk-1.4.350.1`) | `KhronosGroup/Vulkan-Headers` | Apache-2.0 | `truffle_backend_vulkan` |
| `volk` | `3ca312a4f38baa63d8006b6905abbeeb89c8087d` (`1.4.350`) | `zeux/volk` | MIT | `truffle_backend_vulkan` |

Initialize this group with:

```sh
git submodule update --init vendor/vulkan-headers vendor/volk
```
