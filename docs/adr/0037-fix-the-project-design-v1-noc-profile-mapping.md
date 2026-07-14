# Fix the ProjectDesign V1 mapping for the NoC Profile

The unified `ProjectDesign V1` is the only project IR. Its V1 NoC Profile contains exactly one top-level NoC Component, NoC boundary Interfaces, no system-level Connections, and exactly one owned TopologyDocument containing Routers, structural Links, Access Slots, Attachments, Domains/memberships, and schema-declared Package Entities/Relations. Router-scale objects never become top-level Components or a parallel NoC document.
