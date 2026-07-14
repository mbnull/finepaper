# Use typed Application commands over an internal Patch IR

Workbench UI invokes typed NoC commands rather than constructing generic Patches. Commands validate product semantics and compile to an internal atomic Patch representation; Default Engine and Extension Provider reconciliation may also produce Patches, but the Patch algebra is not the public business-command API and cannot be used to bypass command, ownership, schema, or undo rules.
