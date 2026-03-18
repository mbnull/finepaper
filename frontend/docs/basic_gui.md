# NoC IP Configuration GUI Frontend

## Goal Description
Build a modern Qt6-based graphical user interface for visualizing and configuring Network-on-Chip (NoC) IP cores. The primary focus is topology visualization with a collapsible configuration panel. The GUI enables users to visually design NoC topologies (mesh and explicit modes), configure crosspoint routers and endpoints, and export valid JSON configurations for the finepaper framework's code generation pipeline.

## Acceptance Criteria

Following TDD philosophy, each criterion includes positive and negative tests for deterministic verification.

- AC-1: Topology visualization displays NoC structure correctly
  - Positive Tests (expected to PASS):
    - Load a mesh topology JSON (3x3 grid) and verify all 9 crosspoint routers are displayed at correct grid positions
    - Load an explicit topology JSON with custom XP placements and verify XPs appear at specified coordinates
    - Verify connections between XPs are rendered as lines/arrows showing data flow direction
  - Negative Tests (expected to FAIL):
    - Load a JSON with invalid XP coordinates (negative values) and verify error handling
    - Load a JSON with connections referencing non-existent XP IDs and verify rejection

- AC-2: Configuration wizard supports both mesh and explicit topology modes
  - Positive Tests (expected to PASS):
    - Create new NoC using mesh mode wizard, specify 4x4 dimensions, verify JSON contains mesh parameters
    - Create new NoC using explicit mode wizard, manually place 5 XPs, verify JSON contains XPs array
    - Switch between mesh and explicit modes during configuration without data loss
  - Negative Tests (expected to FAIL):
    - Attempt to create mesh with zero or negative dimensions and verify validation error
    - Attempt to create explicit topology with duplicate XP IDs and verify rejection

- AC-3: Endpoint configuration interface exposes all required parameters
  - Positive Tests (expected to PASS):
    - Add endpoint to XP, configure as master with AXI4 protocol, set buffer_depth=32, verify JSON output
    - Add endpoint with QoS enabled, verify qos_enabled=true in JSON
    - Configure endpoint with default values, verify framework defaults are applied
  - Negative Tests (expected to FAIL):
    - Set endpoint buffer_depth to zero or negative value and verify validation error
    - Select invalid protocol type and verify rejection

- AC-4: Crosspoint router configuration interface exposes routing and buffering parameters
  - Positive Tests (expected to PASS):
    - Configure XP with xy routing algorithm, vc_count=4, buffer_depth=16, verify JSON output
    - Configure XP with yx routing, verify routing_algorithm field in JSON
    - Use default XP configuration, verify framework defaults (xy routing, 2 VCs, depth 8)
  - Negative Tests (expected to FAIL):
    - Set vc_count to 0 or >8 and verify validation error
    - Set buffer_depth to negative value and verify rejection
    - Select invalid routing algorithm (not xy or yx) and verify rejection

- AC-5: Global topology parameters are configurable
  - Positive Tests (expected to PASS):
    - Set data_width=64, flit_width=128, addr_width=32, verify parameters section in JSON
    - Use default topology parameters, verify framework defaults are applied
    - Modify parameters after initial creation, verify JSON updates correctly
  - Negative Tests (expected to FAIL):
    - Set data_width to non-power-of-2 value and verify validation warning
    - Set flit_width smaller than data_width and verify logical error detection

- AC-6: JSON export generates valid framework-compatible configuration files
  - Positive Tests (expected to PASS):
    - Export mesh topology configuration, verify JSON contains name, version, parameters.mesh, endpoints
    - Export explicit topology configuration, verify JSON contains xps array with all configured routers
    - Load exported JSON back into GUI, verify all settings are preserved
  - Negative Tests (expected to FAIL):
    - Export configuration with missing mandatory fields (name, version) and verify error
    - Export configuration with orphaned endpoints (not mapped to any XP) and verify warning

- AC-7: GUI integrates with xmake build system
  - Positive Tests (expected to PASS):
    - Run xmake build from frontend/qt directory, verify executable is generated
    - Run xmake run, verify GUI application launches successfully
    - Modify source files, run xmake rebuild, verify changes are reflected
  - Negative Tests (expected to FAIL):
    - Run xmake with missing Qt dependencies and verify clear error message
    - Run xmake with corrupted xmake.lua and verify build failure is reported

- AC-8: UI follows modern design principles
  - Positive Tests (expected to PASS):
    - Verify configuration panel can be collapsed/expanded via UI control
    - Verify modern styling (flat design, consistent spacing, readable fonts)
    - Verify topology visualization occupies primary screen space when config panel is collapsed
  - Negative Tests (expected to FAIL):
    - Attempt to use application with config panel permanently blocking topology view

## Path Boundaries

Path boundaries define the acceptable range of implementation quality and choices.

### Upper Bound (Maximum Acceptable Scope)
The implementation includes a modern Qt GUI with both mesh and explicit topology modes, high-quality topology visualization with drag-and-drop XP placement, collapsible configuration panel with smooth animations, comprehensive configuration forms for all XP/endpoint/topology parameters, real-time visual feedback, JSON import/export with validation, integration with framework's run_drc.rb for validation, modern flat UI design with consistent styling, and complete xmake build configuration with Qt dependency management.

### Lower Bound (Minimum Acceptable Scope)
The implementation includes a basic Qt GUI with modern styling, topology visualization as the primary focus (occupying most screen space), collapsible configuration panel (can be hidden to maximize topology view), support for mesh topology creation via parameter input, simple form-based configuration for essential XP and endpoint parameters, JSON export functionality that generates framework-compatible files using a reference example scheme, and functional xmake build system that compiles and runs the application.

### Allowed Choices
- Can use: Qt Widgets or Qt Quick/QML for UI framework, JSON libraries (Qt's QJsonDocument or third-party), modern Qt styling (QSS stylesheets, Material Design principles), standard Qt layouts and controls, collapsible/dockable widgets for configuration panel, framework's example JSON schemas as reference (select one static example as the initial target format)
- Cannot use: Qt5 or earlier versions (project is Qt-based), non-Qt GUI frameworks, build systems other than xmake (explicitly required), JSON formats incompatible with the finepaper framework structure, outdated UI patterns (avoid skeuomorphic or cluttered designs)
- Validation approach: Must support future integration with framework's run_drc.rb script (as clarified by user), but initial implementation can use basic client-side validation for immediate feedback

## Feasibility Hints and Suggestions

> **Note**: This section is for reference and understanding only. These are conceptual suggestions, not prescriptive requirements.

### Conceptual Approach

**Architecture Overview:**
```
┌─────────────────────────────────────────────────────────────────────┐
│                     Main Window (Qt6)                               │
├─────────────────────────────────────────────────────────────────────┤
│  Menu Bar: File (New/Open/Save/Export) | Edit | View               │
├─────────────────────────────────────────────┬───────────────────────┤
│                                             │  Config Panel [◀]     │
│   Topology Visualization Canvas             │  (Collapsible)        │
│   (Primary Focus - Maximum Space)           │ ┌─────────────────┐   │
│                                             │ │ Mode: [Mesh ▼]  │   │
│  ┌─────┐         ┌─────┐                   │ ├─────────────────┤   │
│  │ XP  │────────▶│ XP  │                   │ │ Mesh Params:    │   │
│  └─────┘         └─────┘                   │ │  Width:  [4]    │   │
│     │               │                       │ │  Height: [4]    │   │
│     ▼               ▼                       │ ├─────────────────┤   │
│  ┌─────┐         ┌─────┐                   │ │ Topology:       │   │
│  │ XP  │────────▶│ XP  │                   │ │  Data W: [32]   │   │
│  └─────┘         └─────┘                   │ │  Flit W: [64]   │   │
│                                             │ ├─────────────────┤   │
│  Modern flat design with clean lines       │ │ Selected XP:    │   │
│  High-DPI support, smooth rendering         │ │  Route: [xy▼]   │   │
│                                             │ │  VC: [2]        │   │
│                                             │ │  Buf: [8]       │   │
│                                             │ ├─────────────────┤   │
│                                             │ │ Endpoint:       │   │
│                                             │ │  Type: [M/S]    │   │
│                                             │ │  Proto: [AXI4]  │   │
│                                             │ │  QoS: [✓]       │   │
│                                             │ └─────────────────┘   │
└─────────────────────────────────────────────┴───────────────────────┘

When collapsed: [▶] button expands panel
Topology canvas expands to full width for maximum visualization space
```

**Data Flow:**
1. User creates new NoC → Wizard collects mode (mesh/explicit) and basic parameters
2. GUI initializes internal data model (NoC object with XPs, endpoints, connections)
3. Visualization canvas renders topology based on data model
4. User selects XP/endpoint → Configuration panel populates with current values
5. User modifies config → Data model updates → Canvas refreshes
6. User exports → Data model serializes to JSON → Framework validation (future: run_drc.rb)

**Key Components:**
- `NocDataModel`: Holds topology structure (XPs, endpoints, connections, parameters)
- `TopologyCanvas`: QGraphicsView-based widget for rendering and interaction
- `ConfigPanel`: QWidget with forms for parameter editing
- `JsonExporter`: Serializes NocDataModel to framework JSON format
- `JsonImporter`: Parses framework JSON and populates NocDataModel

### Relevant References
- `frontend/qt/xmake.lua` - Build configuration template, defines Qt dependencies and build rules
- `frontend/docs/xmake.txt` - xmake documentation for Qt project setup
- `framework/lib/topology_expander.rb` - Shows how mesh parameters expand to XPs/connections
- `framework/lib/noc.rb` - Defines NoC data structure (xps, endpoints, connections, parameters)
- `framework/lib/xp.rb` - XP class with config_schema showing valid routing algorithms and parameters
- `framework/lib/endpoint.rb` - Endpoint class with config_schema for buffer_depth and qos_enabled
- `framework/examples/*.json` - Reference JSON files showing expected structure

## Dependencies and Sequence

### Milestones

1. **Project Setup and Build Infrastructure**
   - Phase A: Configure xmake.lua with Qt6 dependencies (qtcore, qtwidgets, qtgui)
   - Phase B: Create basic Qt6 application skeleton with main window and modern styling
   - Phase C: Verify build and run workflow with xmake

2. **Data Model Implementation**
   - Phase A: Define NocDataModel class to hold topology structure
   - Phase B: Implement JSON serialization (export to framework format)
   - Phase C: Implement JSON deserialization (import from framework format)
   - Dependencies: Requires Milestone 1 completion

3. **Topology Visualization**
   - Phase A: Create TopologyCanvas widget using QGraphicsView
   - Phase B: Implement mesh topology rendering (grid layout of XPs)
   - Phase C: Implement explicit topology rendering (custom XP positions)
   - Phase D: Add connection visualization (lines between XPs)
   - Dependencies: Requires Milestone 2 (data model must exist to visualize)

4. **Configuration Interface**
   - Phase A: Build topology mode selector (mesh vs explicit)
   - Phase B: Create mesh parameter input form (width, height)
   - Phase C: Create topology parameter form (data_width, flit_width, addr_width)
   - Phase D: Create XP configuration panel (routing algorithm, VC count, buffer depth)
   - Phase E: Create endpoint configuration panel (type, protocol, buffer depth, QoS)
   - Dependencies: Requires Milestone 2 (data model) and Milestone 3 (visualization for selection feedback)

5. **Wizard and Workflow Integration**
   - Phase A: Implement new NoC creation wizard
   - Phase B: Connect configuration panels to data model (bidirectional binding)
   - Phase C: Add file operations (New, Open, Save, Export JSON)
   - Dependencies: Requires Milestones 2, 3, and 4

6. **Validation and Polish**
   - Phase A: Add client-side validation for parameter ranges
   - Phase B: Prepare integration point for framework's run_drc.rb (future)
   - Phase C: Add user feedback (error messages, status indicators)
   - Dependencies: Requires all previous milestones

## Implementation Notes

### Code Style Requirements
- Implementation code and comments must NOT contain plan-specific terminology such as "AC-", "Milestone", "Step", "Phase", or similar workflow markers
- These terms are for plan documentation only, not for the resulting codebase
- Use descriptive, domain-appropriate naming in code instead

### Framework Integration Points
- JSON output must match the framework's expected structure: top-level fields (name, version, parameters), xps array or parameters.mesh, endpoints array, connections array
- Use a static reference example from framework/examples/ directory as the initial target format (select one example that demonstrates the core structure, avoiding overly complex test schemes)
- The framework will provide run_drc.rb for validation in the future - design the export workflow to accommodate external validation script integration
- Reference the selected example JSON for concrete structure validation

### Qt6 and xmake Specifics
- Use the existing template at frontend/qt as the starting point
- xmake.lua must declare Qt6 dependencies: add_requires("qtwidgets", "qtgui", "qtcore")
- Follow xmake documentation at frontend/docs/xmake.txt for Qt6-specific build rules
- Ensure cross-platform compatibility (Linux, macOS, Windows) through Qt6 abstractions
- Leverage Qt6 modern features: improved QML engine, better high-DPI support, enhanced styling capabilities

### Configuration Schema Alignment
- XP routing algorithms: Must be one of ['xy', 'yx'] (from framework's XP.config_schema)
- XP VC count: Integer range 1-8 (from framework validation)
- Buffer depths: Positive integers (XP default: 8, Endpoint default: 16)
- Endpoint types: 'master' or 'slave'
- Endpoint protocols: Must match framework's supported protocols (check framework/lib/endpoint.rb)

### User Experience Considerations
- Topology visualization is the primary focus - it should occupy the majority of screen space
- Configuration panel must be collapsible to maximize topology viewing area
- Apply modern UI design principles: flat design, consistent spacing, readable typography, subtle shadows/borders
- Provide visual feedback when users select XPs or endpoints in the canvas
- Use sensible defaults from the framework to minimize required user input
- Display clear error messages for validation failures
- Consider using a side panel or dockable widget for configuration that can slide in/out
- Support undo/redo for configuration changes (optional for upper bound)

### Port Configuration (Second Step - Future Work)
- The draft mentions adding ports for all components as a second step (currently TBD)
- This refers to integrating with the framework's NiPortPlugin which parses Verilog/SystemVerilog IP cores
- Initial implementation should focus on topology and basic configuration; port integration can be added in a future iteration

--- Original Design Draft Start ---

# Main goals
Write a IP configuration frontend under frontend.
The total workflow is the first image.
The configured IR current is a json file ,but format is not defined yet.Use the framework/example scheme to as first stage target.

# First step
Should  can display topology correctly, and assume only mesh topology is supported. Every endpoint can be config while generate.The endpoint current is not have many config, you can reference framework scheme.

Each create, config will use a create wizzard, and current assume current version only support noc.

It ***MUST*** can generate json for framework t use.The framework is not complete yet, so how to call lib current is just use generate script.

# Second Step
add ports for all componenets. The target is second image(example image).
TBD.

# Notes
we will use xmake as build system. you can change all files under frontend. and it provide a simple template under frontend/qt. The xmake doc is under frontend/docs/xmake.txt

All reference image is below.
![alt text](image-1.png) after upload ,is 1.jpeg or 2.jpeg

![example image](image.png) after upload is 3.jpeg

![example 2](socreates.png) after upload is 4.jpeg
--- Original Design Draft End ---
