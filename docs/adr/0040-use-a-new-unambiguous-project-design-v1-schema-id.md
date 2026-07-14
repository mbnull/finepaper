# Use a new unambiguous ProjectDesign V1 schema ID

The unified `.nocproj` root uses `ipcraft.project-design.v1` with an explicit `ipcraft.profile.noc` profile marker. The two incompatible pre-release roots that currently claim `ipcraft.project.v1` remain confined to the frozen legacy `.fpproj` path; the new reader does not guess by shape or import them and returns `project.legacy_format_unsupported` for legacy schema IDs.
