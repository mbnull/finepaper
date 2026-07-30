# Finepaper Mesh NoC Package

This runtime-loaded Package handles ordinary N×M mesh NoCs. It provides the
parameter and endpoint declarations plus a process-based generator. The
adapter owns conversion to the pre-existing Ruby generator format, keeping
that legacy format outside Finepaper Core.

This directory remains the V1 compatibility Package. The sibling
`finepaper-noc-v3` Package exposes Domain configuration and sparse per-Router
microarchitecture overrides while sharing the same maintained generator.
