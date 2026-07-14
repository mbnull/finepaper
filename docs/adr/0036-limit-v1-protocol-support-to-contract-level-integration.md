# Limit V1 AXI5, ACE, and CHI support to Contract-level integration

V1 loads exact AXI5, ACE, and CHI Contract Packages, renders and validates their declared fields and roles, matches them generically to Access Slot declarations, persists Interface configuration, locates field diagnostics, and projects it to generation. Default Engine code does not branch on protocol identity; endpoint composition, address allocation, transaction correctness, and cache-coherency correctness remain outside V1 and protocol-specific semantics stay in Contracts, NoC Packages, IP Core DRC, and generators.
