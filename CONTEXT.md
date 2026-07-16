# NoC Design Context

This context describes a system-level design whose single generated root is one network-on-chip. It distinguishes the NoC, its endpoints, and the domains that partition it.

## Language

**System Design**:
A user-owned design containing exactly one top-level NoC together with its boundary Interfaces, configuration, topology, and Domain assignments.
_Avoid_: Multi-NoC project, IP composition project

**NoC**:
The single interconnect instance owned by a System Design.
_Avoid_: Root IP list, NoC collection

**Domain**:
A typed, user-owned grouping of Routers within the NoC; in V1 every Router belongs to exactly one Domain of every Package-declared Domain type and may simultaneously belong to Domains of different types. The Package defines available types and their hardware meaning while the core manages identity, membership, and presentation.
_Avoid_: Separate NoC, untyped canvas group, core-defined hardware domain

**Connected Domain**:
A V1 Domain whose member Routers form one connected subgraph through structural Links, with no disconnected islands; empty Default Domain is the only empty-Domain exception.
_Avoid_: Visual proximity group, disconnected Router set

**NoC Interface**:
A logical boundary interface exposed by the NoC, declaring its external protocol contract and Router attachment without prescribing how Network Interface logic is grouped or generated.
_Avoid_: Endpoint, external IP instance, Network Interface implementation unit

**Interface Contract**:
A reusable declaration of protocol identity, role, and connection-relevant capabilities used to evaluate whether two interfaces can interoperate.
_Avoid_: IP configuration, arbitrary capability bag

**Access Slot**:
A stable, user-selected logical position through which a NoC Interface attaches at a Router; its identity and meaning remain unchanged when other interfaces are added, removed, or reordered.
_Avoid_: Array index, generated Network Interface number, canvas position

**Unresolved Attachment**:
A preserved Interface attachment intent whose referenced Router or Access Slot is not currently available; it remains editable and recoverable but blocks formal saving and generation until explicitly resolved by the user.
_Avoid_: Automatically migrated attachment, deleted interface

**Diagnostic Report**:
A versioned, structured result produced by an IP Core tool, containing stable rule identities, severity, messages, and references to affected design subjects; it describes DRC outcomes rather than executable rules.
_Avoid_: DRC rule language, unstructured tool log

**Pipeline Run**:
One immutable-Snapshot Validate or Generate operation that owns an ordered set of host and external-tool steps and aggregates their normalized outcomes.
_Avoid_: One external process, shared tool log, promoted output

**Tool Invocation**:
One concrete external process execution inside a Pipeline Run, with its own identity, input, optional tool-authored result, normalized host step result, and logs.
_Avoid_: Pipeline Run, reusable background daemon, implicit step

**Runtime Closure**:
The locked runtime distribution, loader/library/module dependencies, platform ABI, invocation profile, environment profile, and network policy required to replay a Provider or Tool invocation.
_Avoid_: Executable path, PATH lookup, interpreter version string

**IP Core Analyzer**:
An IP Core-owned analysis capability that evaluates private NoC microarchitecture semantics and returns a Diagnostic Report without exposing those semantics to the core product or third-party analyzers.
_Avoid_: Universal deadlock analyzer, core DRC rule engine

**Internal Transport**:
The NoC Package-owned protocol used between internal NoC components; it is not part of the user-visible design language or public interface contracts.
_Avoid_: NoC Interface protocol, configurable system interface

**Network Interface Implementation**:
Package-derived logic that translates between one or more NoC Interfaces and the Internal Transport; it may be dedicated per interface or shared by multiple interfaces and is not a user-owned design object.
_Avoid_: NoC Interface, fixed one-to-one adapter

**Engine-Managed Structure**:
Persisted Router, Access Slot, and structural Link entities whose stable identities belong to the System Design while their lifecycle and structural changes are controlled through validated Patches from the single selected Structure Authority.
_Avoid_: Ephemeral preview object, user-editable drawing primitive

**Derived State**:
The complete materialized output owned by the selected Structure Authority: Routers, structural Links, Access Slots, ownership=`engine` Package Entities/Relations, and Authority-owned properties used by generation or later reconciliation.
_Avoid_: Entire ProjectDesign, user intent, visual cache

**Structure Authority**:
The single Package-selected lifecycle authority for Derived State, either the Default Engine or one Extension Provider; the host still allocates IDs and enforces all contracts and invariants.
_Avoid_: Two-stage competing topology generators, unrestricted Provider ownership

**Default Engine Bundle**:
The exact immutable, installable implementation of common NoC derivation behavior pinned by bundle digest and connected to the Host through a versioned Engine Host Contract; its name/version and compatibility class do not substitute for exact identity.
_Avoid_: Whatever Engine ships with the current application, Package Provider, silently upgraded built-in code

**Host Side-effect Contract**:
The versioned Host-owned semantics that transform an Authority Patch into Domain, Attachment, Package-Relation, impact, and canonical local-reference side effects within a Topology Candidate.
_Avoid_: Package-private topology logic, unversioned application behavior, UI preview rules

**Degraded Inspect Mode**:
A restricted project-open state used when an exact required dependency, Engine Host Contract, Host Side-effect Contract, Runtime Closure, or platform compatibility is unavailable; it preserves/displays data and permits only an explicitly supported, digest-confirmed Engine migration recovery action, not normal editing, saving, validation, generation, or silent substitution.
_Avoid_: Best-effort fallback, automatic migration, editable degraded project

**ProjectDesignWellFormed**:
A readable authoritative working or recovery design that passes strict JSON,
schema, identity, and reference checks; it may contain a disconnected Domain
with a matching blocking diagnostic.
_Avoid_: Save-eligible design, invalid unreadable document

**ProjectDesignCommitValid**:
A working design that may be installed by one atomic command or topology
materialization transaction; a disconnected Domain is allowed only when the
same transaction produces the stable blocking diagnostic.
_Avoid_: Formal save checkpoint, unrestricted invalid state

**ProjectDesignSaveEligible**:
A current design with connected Domains, resolved Attachments, current locked
dependencies/Derived State, and no blocking diagnostics; only this predicate
permits formal save, Validate, or Generate.
_Avoid_: Any parseable working state

**Pending Topology Group**:
The sole open, uncommitted V1 topology transaction proposal containing the latest topology intent and reconciliation generation; it enters authoritative design and formal history only when intent, Derived Patch, invariant side effects, tombstones, and Host IDs materialize atomically.
_Avoid_: Accepted stale command, queue of topology transactions, saved intermediate revision

**Topology Candidate**:
An immutable, digest-identified proposed materialization for a Pending Topology Group, containing the Authority Patch, Application side effects, tombstones, and impact report; it may auto-commit, require explicit confirmation, or be blocked, but is never trusted through recovery.
_Avoid_: Accepted design, Provider-owned transaction, persisted checkpoint

**Draft Overlay**:
An independent, locally undoable collection of ordinary edit proposals made while a Pending Topology Group is open; it is not ProjectDesign, formal history, reconciliation input, or formally saveable state and is revalidated for separate submission after materialization.
_Avoid_: Accepted command, hidden project mutation, topology input

**Formal Authoritative Command History**:
The single undo/redo history of transactions that changed authoritative ProjectDesign; Draft Overlay and Pending-Group local stacks are interaction state and are not additional authoritative histories.
_Avoid_: Every temporary editor undo stack, process-wide history, recovery journal

**Reconciliation**:
The asynchronous process of deriving one candidate atomic transaction for the single Pending Topology Group from normalized topology input and current Derived State; failure leaves authoritative design unchanged and preserves the Group for retry or discard.
_Avoid_: Blocking configuration transaction, destructive provider rollback

**Extension Provider**:
An optional Package-supplied domain extension used only when the Default NoC Design Engine and declarations cannot express an IP Core's design behavior; it returns validated Patches or preview data and does not control GUI widgets.
_Avoid_: Qt UI plugin, IP Core generator, mandatory adapter, default design engine

**Design Patch**:
A structured change from a user command, the Default Engine, or an Extension Provider, bound to a specific design revision and validated before the Application commits it.
_Avoid_: Direct project mutation, file rewrite, unversioned callback

**Package Entity**:
A Package-defined, schema-governed design object whose identity and lifecycle are managed through generic Design Patch operations without adding Package-specific execution code to the core.
_Avoid_: Opaque unvalidated JSON, core hardcoded IP feature

**Package Relation**:
A Package-defined, schema-governed relationship between stable design subjects, managed through the same generic Design Patch mechanism as Package Entities.
_Avoid_: Canvas-only edge, implicit array ordering
