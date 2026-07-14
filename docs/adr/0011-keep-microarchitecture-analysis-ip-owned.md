# Keep microarchitecture analysis owned by the IP Core

Deadlock and similar analyses depend on private routing, channel, and transport semantics, so they are supplied by the NoC IP Core through an Analyzer capability and return standardized Diagnostic Reports. The product does not promise a universal third-party analysis model or expose private microarchitecture semantics merely to enable generic analyzers.
