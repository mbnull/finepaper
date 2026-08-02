# OpenNoC CHI Mesh Finepaper Package

This Package adapts the vendored OpenNoC Mesh wrapper generator. It supports
only rectangular 2×2 through 8×8 Meshes because the upstream Mesh generator
uses 3-bit X/Y identifiers. The separate upstream Ring generator is deliberately
out of scope until Finepaper has a non-Mesh topology model.

RN-F, RN-I, HN-F, HN-I, and SN-F Endpoint types map to the two local ports of
each OpenNoC crosspoint. Generation writes the upstream Mesh configuration,
creates the wrapper RTL in Finepaper's artifact directory, and copies its two
required support sources into that directory. The generated filelist is the
integration entry point; endpoint component RTL remains the responsibility of
the surrounding CHI subsystem.

Vendor source: `vendor/OpenNoC` at revision `4f57dda` (Mulan PSL v2).
