# Select exactly one Structure Authority per Package

Every V1 NoC Package declares `structureAuthority: default-engine | extension-provider`. The selected Authority alone owns Router, structural Link, Access Slot, and ownership=`engine` Package object lifecycle; the other path cannot emit competing structural mutations. Even in Provider mode, the host exclusively allocates opaque IDs and validates schemas, references, ownership, Domains, Attachments, causality, and atomicity. This preserves common platform behavior without forcing complex IP topology logic into the Default Engine.
