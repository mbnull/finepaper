# Use a new NoC Package V1 envelope

The new path loads `ipcraft.noc-package.v1` as its unambiguous NoC Package envelope. The legacy broad `ipcraft.package.v1` remains confined to the frozen old path before cutover. Unknown business capabilities remain loadable through supported generic schemas or namespaced opaque extensions, while malformed core fields and invalid declarations of known capabilities are rejected.
