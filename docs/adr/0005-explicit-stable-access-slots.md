# Attach NoC Interfaces through explicit stable Access Slots

Users explicitly select the Access Slot used by each NoC Interface. Slot identity is persisted as design intent and must remain stable across reopening, reordering, and unrelated interface changes because it may affect interleaving, address encoding, and generated RTL; Network Interface grouping remains a Package-derived implementation detail.
