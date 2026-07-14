# Use a NoC Profile on the generic ProjectDesign foundation

The V1 product exposes and permits exactly one top-level NoC, but this is an Application Profile and cardinality rule over an evolved generic `ProjectDesign` foundation. `DesignSession` owns that single aggregate; the implementation must not introduce a second NoC-specific project IR, schema authority, command path, or persistence model alongside the existing project/package/tool foundation.
