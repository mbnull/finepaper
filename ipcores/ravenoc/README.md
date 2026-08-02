# RaveNoC Finepaper Package

This Package adapts the vendored RaveNoC release as a rectangular 2D Mesh.
It exposes the vendor's width, virtual-channel, buffer, routing, packet-size,
and AXI CDC settings through Finepaper. Each Router may host one AXI4 network
interface, matching RaveNoC's top-level interface array.

Generation validates the Mesh and emits an ordered simulator filelist plus a
resolved configuration record. The filelist points at the vendored source and
sets the vendor macros; compile it with the top module `ravenoc`.

The adapter intentionally does not claim to generate endpoint-side AXI
masters/slaves. Finepaper Endpoints describe which of RaveNoC's existing AXI
network interfaces are consumed by the surrounding design.

Vendor source: `vendor/ravenoc` at revision `ffc4683` (MIT).
