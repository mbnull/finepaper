# Finepaper Mesh NoC Package

This runtime-loaded Package handles ordinary N×M mesh NoCs. It provides the
parameter and endpoint declarations plus a process-based generator. The
adapter owns conversion to the pre-existing Ruby generator format, keeping
that legacy format outside Finepaper Core.

This directory remains the V1 compatibility Package. The sibling
`finepaper-noc-v3` Package exposes Domain configuration and sparse per-Router
microarchitecture overrides while sharing the same maintained generator. V1
does not declare `runtimeCapabilities.domainConfiguration` because its Design
format has no Domain data planes. For V2+ Design input, the shared runtime
strictly compiles Domain intent into a deterministic Mesh crossing constraints
artifact; the V1 path remains free of Domain assumptions.
