# NoC IP Configuration Frontend

## Goal Description
Build a React-based frontend for configuring Network-on-Chip (NoC) Intellectual Property cores with visual mesh topology display, wizard-driven configuration, and JSON export capability that integrates with the existing framework generator.

## Acceptance Criteria

Following TDD philosophy, each criterion includes positive and negative tests for deterministic verification.

- AC-1: Visual mesh topology display
  - Positive Tests (expected to PASS):
    - Display a 2×2 mesh with 4 crosspoints arranged in a grid layout
    - Display a 3×3 mesh with 9 crosspoints with correct spatial relationships
    - Render mesh topology that visually matches the reference workflow image (1.png)
  - Negative Tests (expected to FAIL):
    - Attempt to display non-mesh topology (star, ring) should be rejected or not supported
    - Display mesh with invalid dimensions (0×0, negative values) should fail validation
    - Render topology without proper grid alignment should be visually incorrect

- AC-2: Endpoint configuration during mesh generation
  - Positive Tests (expected to PASS):
    - Configure endpoint type (master/slave) and attach to a crosspoint
    - Configure endpoint protocol (AXI4) and data width
    - Reference framework schema for endpoint configuration options
    - Generate mesh where each crosspoint can have zero or more endpoints attached
  - Negative Tests (expected to FAIL):
    - Attempt to configure endpoint with unsupported protocol should be rejected
    - Attach endpoint without specifying required type field should fail validation
    - Configure endpoint with invalid data width should be rejected

- AC-3: Wizard-based creation and configuration workflow
  - Positive Tests (expected to PASS):
    - Launch wizard to create new NoC configuration
    - Step through wizard to configure mesh dimensions (width × height)
    - Configure crosspoint routing algorithm through wizard interface
    - Complete wizard and generate initial topology
  - Negative Tests (expected to FAIL):
    - Attempt to skip required wizard steps should be prevented
    - Submit wizard with incomplete required fields should fail validation
    - Cancel wizard should not create partial configuration

- AC-4: JSON export compatible with framework schema
  - Positive Tests (expected to PASS):
    - Export JSON with required fields: name, version, xps, connections, endpoints
    - Generate JSON that can be consumed by framework's bin/generate script
    - Export includes crosspoint positions (x, y) and IDs
    - Export includes connections with from/to/dir fields
    - Export includes endpoints with id, type, protocol, data_width
  - Negative Tests (expected to FAIL):
    - Export empty configuration should fail or produce minimal valid JSON
    - Export with missing required framework fields should be invalid
    - Export with incorrect field types should fail framework parsing

- AC-5: NoC-only component support
  - Positive Tests (expected to PASS):
    - Create and configure NoC mesh topology
    - Configure NoC-specific parameters (routing algorithm, virtual channels)
    - Wizard explicitly supports NoC component type
  - Negative Tests (expected to FAIL):
    - Attempt to create non-NoC components should not be available in UI
    - Select component types other than NoC should not be offered in wizard

- AC-6: Port visualization for components (Second Step)
  - Positive Tests (expected to PASS):
    - Display ports on crosspoint components matching reference image (2.png)
    - Display ports on endpoint components
    - Visual representation matches example image (socreates.png / 3.png)
  - Negative Tests (expected to FAIL):
    - Display components without port information should show incomplete visualization
    - Render ports with incorrect directionality should be visually wrong

## Path Boundaries

Path boundaries define the acceptable range of implementation quality and choices.

### Upper Bound (Maximum Acceptable Scope)
The implementation includes a complete React application with visual mesh topology rendering, full wizard workflow for NoC configuration, comprehensive endpoint and crosspoint configuration UI, port visualization on all components, JSON export with validation, and integration testing with the framework's generate script. The UI provides intuitive drag-and-drop or form-based configuration, real-time visual feedback, and error handling for invalid configurations.

### Lower Bound (Minimum Acceptable Scope)
The implementation includes a basic React application that displays mesh topology in a grid layout, provides a simple wizard for creating NoC configurations with mesh dimensions and routing algorithm selection, allows basic endpoint attachment to crosspoints, and exports valid JSON matching the framework schema. The visual representation is functional but minimal, and port visualization may be simplified or deferred to the second milestone.

### Allowed Choices
- Can use: React (required), standard React patterns (hooks, components), CSS/styled-components for styling, JSON serialization libraries, canvas or SVG for topology visualization, form libraries for wizard implementation
- Cannot use: Non-React frameworks (Vue, Angular), technologies that conflict with future Qt migration, complex state management libraries unless necessary (Redux, MobX - prefer React Context or simple state)
- Technology stack: React is specified for this prototype, with awareness that the project will migrate to Qt-based implementation in the future
- JSON format: Must use the existing framework schema (NocConfig format with xps, connections, endpoints fields) - this is fixed per framework requirements
- Topology support: Only mesh topology is supported in this version - other topologies (star, ring, tree) are explicitly out of scope

## Feasibility Hints and Suggestions

> **Note**: This section is for reference and understanding only. These are conceptual suggestions, not prescriptive requirements.

### Conceptual Approach

**React Component Architecture:**
```
App
├── WizardFlow (manages multi-step configuration)
│   ├── MeshDimensionsStep (width × height input)
│   ├── RoutingAlgorithmStep (select xy, yx, west_first, etc.)
│   └── EndpointConfigStep (add/configure endpoints)
├── TopologyCanvas (visual mesh display)
│   ├── CrosspointNode (renders XP at grid position)
│   ├── ConnectionLine (renders links between XPs)
│   └── EndpointNode (renders endpoint attached to XP)
└── ExportButton (generates JSON output)
```

**Data Model (in-memory state):**
```javascript
{
  name: "my_noc",
  version: "1.0",
  meshDimensions: { width: 3, height: 3 },
  xps: [
    { id: "xp_0_0", x: 0, y: 0, routing_algorithm: "xy" }
  ],
  connections: [
    { from: "xp_0_0", to: "xp_1_0", dir: "east" }
  ],
  endpoints: [
    { id: "ep_cpu0", type: "master", protocol: "axi4", data_width: 64 }
  ]
}
```

**JSON Export Logic:**
1. Validate configuration completeness
2. Transform internal state to framework schema format
3. Generate connections automatically based on mesh topology (east/south links)
4. Serialize to JSON string
5. Provide download or copy-to-clipboard functionality

**Mesh Topology Generation:**
- For width=W, height=H: create W×H crosspoints at positions (x,y)
- Generate east connections: (x,y) → (x+1,y) for x < W-1
- Generate south connections: (x,y) → (x,y+1) for y < H-1
- Each XP gets unique ID: `xp_{x}_{y}`

**Port Visualization (Second Step):**
- Render directional ports on each crosspoint (north, south, east, west, local)
- Show endpoint connection to local port
- Visual style matches reference images (2.png, 3.png)

### Relevant References
- `/framework/examples/simple_mesh.json` - Example NoC configuration showing required JSON structure
- `/framework/src/ruby/model/noc_config.rb` - Framework's data model defining schema
- `/framework/src/ruby/parser/json_parser.rb` - JSON parsing logic that validates structure
- `/framework/src/ruby/topology/mesh_expander.rb` - Mesh topology generation algorithm
- `/framework/bin/generate` - CLI script that consumes JSON and generates RTL

## Dependencies and Sequence

### Milestones
1. **Milestone 1: Core Visual Topology and Configuration**
   - Phase A: React project setup and basic component structure
   - Phase B: Wizard implementation for mesh configuration (dimensions, routing algorithm)
   - Phase C: Visual mesh topology rendering with crosspoints in grid layout
   - Phase D: Endpoint configuration and attachment to crosspoints
   - Phase E: JSON export matching framework schema
   - Phase F: Integration testing with framework's bin/generate script

2. **Milestone 2: Port Visualization Enhancement**
   - Phase A: Add port rendering to crosspoint components
   - Phase B: Add port rendering to endpoint components
   - Phase C: Visual refinement to match reference images (2.png, 3.png)

**Dependency Notes:**
- Milestone 1 Phase B (wizard) must complete before Phase C (visual rendering) to provide configuration data
- Milestone 1 Phase C (topology rendering) must complete before Phase D (endpoint attachment) to have visual targets
- Milestone 1 Phase E (JSON export) depends on all prior phases to have complete data model
- Milestone 2 depends on Milestone 1 completion for base topology visualization
- Priority: Visual components (Phases C, D) take precedence over data model perfection per draft requirements

## Implementation Notes

### Code Style Requirements
- Implementation code and comments must NOT contain plan-specific terminology such as "AC-", "Milestone", "Step", "Phase", or similar workflow markers
- These terms are for plan documentation only, not for the resulting codebase
- Use descriptive, domain-appropriate naming in code instead

### Clarifications from Draft Analysis

**Terminology:**
- "IP" refers to Intellectual Property (NoC IP cores), not Internet Protocol
- The framework uses "NoC" (Network-on-Chip) terminology consistently
- "IR" in the draft appears to be a typo and should be interpreted as "NoC configuration"

**JSON Format:**
- Despite draft stating "format is not defined yet", the framework already has a well-defined schema
- Frontend must generate JSON matching the existing framework's NocConfig format
- Required top-level fields: name, version, xps, connections, endpoints
- Reference: `/framework/examples/simple_mesh.json` for exact structure

**Technology Stack:**
- Frontend technology: React (confirmed)
- This is a prototype project that will migrate to Qt-based implementation in the future
- Keep implementation simple and focused on visual functionality over complex data models

**Configuration Parameters:**
- Mesh dimensions: width × height (fundamental to mesh topology)
- XP routing algorithm: xy, yx, west_first, etc. (per framework support)
- Endpoint types and protocols: master/slave with AXI4 protocol
- XP buffer configuration (vc_count, buffer_depth) can be added as needed

**Priority Guidance:**
- Highest priority: Complete the visual part (topology display, wizard UI)
- Secondary priority: Data models and JSON export
- This aligns with rapid prototyping approach before Qt migration

**Reference Images:**
- 1.png (image-1.png): Overall workflow diagram
- 2.png (image.png): Example with port visualization
- 3.png (socreates.png): Additional example of component visualization

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
The highest prioir is complete the visual part instead of data models
this is proto project. we will imgrate to qt-base project in the future.

All reference image is below.
![alt text](image-1.png) after upload ,is 1.png

![example image](image.png) after upload is 2.png

![example 2](socreates.png) after upload is 3.png
--- Original Design Draft End ---
