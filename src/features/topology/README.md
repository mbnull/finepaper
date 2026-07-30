# Topology feature

This directory owns the desktop interaction and presentation of the NoC
topology: the canvas, Router/Endpoint attachment gestures, layout state, and
Mesh resize UI.

It does not own design mutation rules, Package schemas, Domain realization, or
RTL generation. Those remain in `application/`, `package/`, and the selected
Package runtime. The feature may be composed by `gui/`, but must not include
headers from `gui/`; shared widgets and workbench contracts belong under
`ui/`.

Large classes in this directory are migration points, not a permanent
architecture. Subsequent behavior changes should extract scene projection,
attachment interaction, and layout persistence behind focused interfaces.
