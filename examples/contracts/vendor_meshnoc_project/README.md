# Vendor MeshNoC Contract Fixture

This fixture documents the zero-C++ onboarding path for the `vendor.meshnoc`
package. The package lives under `ipcores/vendor-meshnoc` and is loaded through
the normal `PackageService`, `ModuleRegistry`, and package flow runners.

Useful commands:

```bash
xmake run -P qt vendor_meshnoc_onboarding_test
ruby spec_generator/bin/spec-gen --check
```
