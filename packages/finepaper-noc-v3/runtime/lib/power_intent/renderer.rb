# frozen_string_literal: true

require 'digest'

module FinepaperNoc
  module PowerIntent
    # Renders a validated implementation plan as deterministic IEEE 1801-2013
    # (UPF 2.1) Tcl. The renderer deliberately does not infer missing policy:
    # deferred crossings remain deferred and produce no strategy commands.
    class Renderer
      PLAN_FORMAT = 'finepaper.noc-power-implementation-plan'
      PLAN_VERSION = 1
      RESULT_FORMAT = 'finepaper.noc-power-intent-render-result'
      RECEIPT_FORMAT = 'finepaper.noc-power-intent-render-receipt'
      UPF_VERSION = '2.1'
      SOURCE_CONTRACTS = {
        'domainImplementationPlan' =>
          ['finepaper.noc-domain-implementation-plan', 1],
        'powerIntentPlan' => ['finepaper.noc-power-intent-plan', 1],
        'rtlHierarchy' => ['finepaper.noc-rtl-hierarchy', 1]
      }.freeze
      TECHNOLOGY_BINDING_COMMANDS = %w[
        map_power_switch_cell
        map_retention_cell
        use_interface_cell
      ].freeze

      IDENTIFIER = /\A[A-Za-z_][A-Za-z0-9_$]*\z/
      HIERARCHICAL_NAME = /\A[A-Za-z_][A-Za-z0-9_$]*(?:\/[A-Za-z_][A-Za-z0-9_$]*(?:\[[0-9]+\])?)*(?:\[[0-9]+\])?\z/
      ENUMS = {
        'supply kind' => %w[power ground],
        'supply exposure' => %w[external-port internal-switched],
        'control source' => %w[top-port upf-port],
        'active sense' => %w[high low],
        'behavior' => %w[operational retained corrupt],
        'location' => %w[self parent automatic],
        'appliesTo' => %w[inputs outputs],
        'isolation status' => %w[expected not-required deferred],
        'level-shifter status' => %w[expected not-required deferred],
        'power boundary status' => %w[none resolvable deferred],
        'level-shifter rule' => %w[low_to_high high_to_low],
        'technology profile' => %w[abstract upf-interface-cells]
      }.freeze

      class Error < StandardError
        attr_reader :code, :path

        def initialize(code, path, message)
          @code = code
          @path = path
          super("#{message} at #{path}")
        end
      end

      def self.render(plan:)
        new(plan).render
      end

      def initialize(plan)
        @plan = plan
        @lines = []
        @commands = []
        @strategy_coverage = []
        @token_paths = {}
        @control_nets = {}
      end

      def render
        index_plan!
        emit_header
        render_supplies
        render_controls
        render_domains
        render_power_switches
        render_retentions
        render_crossing_strategies

        upf = (@lines + ['']).join("\n").freeze
        receipt = build_receipt
        deep_freeze!(receipt)
        result = {
          'format' => RESULT_FORMAT,
          'formatVersion' => 1,
          'upf' => upf,
          'receipt' => receipt
        }
        deep_freeze!(result)
      end

      private

      def index_plan!
        object!(@plan, '/')
        exact_keys!(
          @plan,
          %w[
            format formatVersion design topModule sourceContracts hierarchyFacts
            technology supplies controls domains powerSwitches retentions
            defaultSystemState systemStates edgeOrientations inactiveIntent
            coverage
          ],
          [], '/'
        )
        expect!(@plan['format'] == PLAN_FORMAT, 'power_renderer.invalid_format',
                '/format', "expected #{PLAN_FORMAT}")
        expect!(@plan['formatVersion'] == PLAN_VERSION,
                'power_renderer.invalid_version', '/formatVersion',
                "expected format version #{PLAN_VERSION}")
        @design = opaque_id!(@plan['design'], '/design')
        @top_module = identifier!(@plan['topModule'], '/topModule')
        validate_source_contracts!
        validate_hierarchy_facts!
        validate_inactive_intent!
        validate_coverage!

        @supplies = index_entries!('supplies', 'id') do |entry, path|
          validate_supply!(entry, path)
        end
        @controls = index_entries!('controls', 'id') do |entry, path|
          validate_control!(entry, path)
        end
        @domains = index_entries!('domains', 'domain') do |entry, path|
          validate_domain_shape!(entry, path)
        end
        @power_switches = index_entries!('powerSwitches', 'id') do |entry, path|
          validate_power_switch_shape!(entry, path)
        end
        @retentions = index_entries!('retentions', 'id') do |entry, path|
          validate_retention_shape!(entry, path)
        end
        @edge_orientations = array!(@plan['edgeOrientations'], '/edgeOrientations')
        @system_states = array!(@plan['systemStates'], '/systemStates')
        @default_system_state = opaque_id!(
          @plan['defaultSystemState'], '/defaultSystemState'
        )
        validate_system_states!
        index_technology!
        validate_references!
      end

      def index_entries!(key, id_key)
        values = array!(@plan[key], "/#{key}")
        indexed = {}
        values.each_with_index do |entry, index|
          path = "/#{key}/#{index}"
          object!(entry, path)
          id = opaque_id!(entry[id_key], "#{path}/#{id_key}")
          expect!(!indexed.key?(id), 'power_renderer.duplicate_id',
                  "#{path}/#{id_key}", "duplicate #{id_key} #{id}")
          yield(entry, path)
          indexed[id] = entry
        end
        indexed
      end

      def validate_source_contracts!
        contracts = object!(@plan['sourceContracts'], '/sourceContracts')
        exact_keys!(
          contracts,
          %w[domainImplementationPlan powerIntentPlan rtlHierarchy], [],
          '/sourceContracts'
        )
        contracts.each do |key, contract|
          path = "/sourceContracts/#{key}"
          object!(contract, path)
          exact_keys!(contract, %w[format formatVersion], [], path)
          expected_format, expected_version = SOURCE_CONTRACTS.fetch(key)
          expect!(contract['format'] == expected_format,
                  'power_renderer.invalid_source_contract', "#{path}/format",
                  "expected source contract #{expected_format}")
          expect!(contract['formatVersion'] == expected_version,
                  'power_renderer.invalid_source_contract_version',
                  "#{path}/formatVersion",
                  "expected source contract version #{expected_version}")
        end
      end

      def validate_hierarchy_facts!
        facts = object!(@plan['hierarchyFacts'], '/hierarchyFacts')
        exact_keys!(
          facts,
          %w[
            format formatVersion topArtifact resetSynchronizers logicControlPorts
          ],
          [], '/hierarchyFacts'
        )
        expect!(facts['format'] == 'finepaper.noc-rtl-hierarchy',
                'power_renderer.invalid_hierarchy_contract',
                '/hierarchyFacts/format',
                'hierarchyFacts must use finepaper.noc-rtl-hierarchy')
        expect!(facts['formatVersion'] == 1,
                'power_renderer.invalid_hierarchy_contract_version',
                '/hierarchyFacts/formatVersion',
                'hierarchyFacts must use format version 1')
        opaque_id!(facts['topArtifact'], '/hierarchyFacts/topArtifact')
        resets = array!(facts['resetSynchronizers'],
                        '/hierarchyFacts/resetSynchronizers')
        resets.each_with_index do |reset, index|
          path = "/hierarchyFacts/resetSynchronizers/#{index}"
          object!(reset, path)
          exact_keys!(
            reset,
            %w[
              timingDomain module instance placement clockSignal
              asyncResetSignal localResetSignal
            ],
            [], path
          )
          opaque_id!(reset['timingDomain'], "#{path}/timingDomain")
          identifier!(reset['module'], "#{path}/module")
          %w[instance clockSignal asyncResetSignal localResetSignal].each do |key|
            hierarchical_name!(reset[key], "#{path}/#{key}")
          end
          expect!(reset['placement'] == 'infrastructure',
                  'power_renderer.invalid_reset_placement',
                  "#{path}/placement",
                  'reset synchronizer placement must be infrastructure')
        end
        @logic_control_ports = {}
        logic_signals = {}
        ports = array!(facts['logicControlPorts'],
                       '/hierarchyFacts/logicControlPorts')
        ports.each_with_index do |port, index|
          path = "/hierarchyFacts/logicControlPorts/#{index}"
          object!(port, path)
          exact_keys!(port, %w[id signal source direction], [], path)
          id = opaque_id!(port['id'], "#{path}/id")
          signal = identifier!(port['signal'], "#{path}/signal")
          expect!(port['source'] == 'top-port',
                  'power_renderer.invalid_logic_control_source',
                  "#{path}/source",
                  'hierarchy logic control source must be top-port')
          expect!(port['direction'] == 'input',
                  'power_renderer.invalid_logic_control_direction',
                  "#{path}/direction",
                  'hierarchy logic control direction must be input')
          expect!(!@logic_control_ports.key?(id),
                  'power_renderer.duplicate_logic_control_id', "#{path}/id",
                  "duplicate hierarchy logic control #{id}")
          expect!(!logic_signals.key?(signal),
                  'power_renderer.duplicate_logic_control_signal',
                  "#{path}/signal",
                  "duplicate hierarchy logic control signal #{signal}")
          @logic_control_ports[id] = port
          logic_signals[signal] = true
        end
      end

      def validate_inactive_intent!
        inactive = object!(@plan['inactiveIntent'], '/inactiveIntent')
        exact_keys!(inactive, %w[domains supplies controls], [], '/inactiveIntent')
        inactive.each do |key, values|
          path = "/inactiveIntent/#{key}"
          ids = array!(values, path)
          ids.each_with_index do |id, index|
            opaque_id!(id, "#{path}/#{index}")
          end
          expect!(ids.uniq.size == ids.size,
                  'power_renderer.duplicate_inactive_id', path,
                  "inactive #{key} IDs must be unique")
        end
      end

      def validate_coverage!
        coverage = object!(@plan['coverage'], '/coverage')
        exact_keys!(coverage, %w[complete summary items], [], '/coverage')
        expect!([true, false].include?(coverage['complete']),
                'power_renderer.invalid_coverage_complete', '/coverage/complete',
                'coverage complete must be boolean')
        summary = object!(coverage['summary'], '/coverage/summary')
        exact_keys!(summary, %w[expected notRequired deferred total], [],
                    '/coverage/summary')
        summary.each do |key, value|
          expect!(value.is_a?(Integer) && value >= 0,
                  'power_renderer.invalid_coverage_count',
                  "/coverage/summary/#{key}",
                  'coverage counts must be non-negative integers')
        end
        expect!(summary['total'] == summary['expected'] +
                  summary['notRequired'] + summary['deferred'],
                'power_renderer.coverage_summary_mismatch', '/coverage/summary',
                'coverage total must equal its status counts')
        items = array!(coverage['items'], '/coverage/items')
        counts = {'expected' => 0, 'not-required' => 0, 'deferred' => 0}
        seen = {}
        items.each_with_index do |item, index|
          path = "/coverage/items/#{index}"
          object!(item, path)
          exact_keys!(item, %w[id kind subject status reason recipes], [], path)
          opaque_id!(item['id'], "#{path}/id")
          opaque_id!(item['kind'], "#{path}/kind")
          subject = opaque_id!(item['subject'], "#{path}/subject")
          expect!(subject.start_with?('/'), 'power_renderer.invalid_subject_pointer',
                  "#{path}/subject", 'coverage subject must be a JSON Pointer')
          validate_ledger_metadata!(item, path)
          key = [item['kind'], item['id']]
          expect!(!seen.key?(key), 'power_renderer.duplicate_coverage_item',
                  "#{path}/id", 'coverage kind/id must be unique')
          seen[key] = true
          counts[item['status']] += 1
        end
        expect!(items.size == summary['total'] &&
                  counts['expected'] == summary['expected'] &&
                  counts['not-required'] == summary['notRequired'] &&
                  counts['deferred'] == summary['deferred'],
                'power_renderer.coverage_summary_mismatch', '/coverage/summary',
                'coverage summary must equal coverage item statuses')
        expect!(coverage['complete'] == summary['deferred'].zero?,
                'power_renderer.coverage_complete_mismatch', '/coverage/complete',
                'coverage complete must be false exactly when items are deferred')
      end

      def validate_supply!(supply, path)
        exact_keys!(
          supply,
          %w[id token kind exposure net states status reason recipes],
          %w[port], path
        )
        validate_ledger_metadata!(supply, path)
        require_expected_ledger!(supply, path, 'active supply')
        token!(supply['token'], "#{path}/token")
        enum!(supply['kind'], 'supply kind', "#{path}/kind")
        exposure = enum!(supply['exposure'], 'supply exposure',
                         "#{path}/exposure")
        identifier!(supply['net'], "#{path}/net")
        if exposure == 'external-port'
          identifier!(supply['port'], "#{path}/port")
        else
          expect!(!supply.key?('port'), 'power_renderer.internal_supply_port',
                  "#{path}/port", 'an internal switched supply must not have a port')
          expect!(supply['kind'] == 'power',
                  'power_renderer.invalid_internal_supply_kind', "#{path}/kind",
                  'only a power supply may be internally switched')
        end
        states = array!(supply['states'], "#{path}/states", nonempty: true)
        seen = {}
        states.each_with_index do |state, index|
          state_path = "#{path}/states/#{index}"
          object!(state, state_path)
          exact_keys!(state, %w[id token condition], %w[voltageMv], state_path)
          id = opaque_id!(state['id'], "#{state_path}/id")
          expect!(!seen.key?(id), 'power_renderer.duplicate_supply_state',
                  "#{state_path}/id", "duplicate supply state #{id}")
          seen[id] = true
          token!(state['token'], "#{state_path}/token")
          condition = enum_value!(state['condition'], %w[full-on off],
                                  "#{state_path}/condition")
          if condition == 'full-on'
            voltage = state['voltageMv']
            expect!(voltage.is_a?(Integer) && voltage >= 0,
                    'power_renderer.invalid_voltage', "#{state_path}/voltageMv",
                    'full-on voltageMv must be a non-negative integer')
            if supply['kind'] == 'ground'
              expect!(voltage.zero?, 'power_renderer.invalid_ground_voltage',
                      "#{state_path}/voltageMv",
                      'a full-on ground state must be 0 mV')
            else
              expect!(voltage.positive?, 'power_renderer.invalid_power_voltage',
                      "#{state_path}/voltageMv",
                      'a full-on power state must be greater than 0 mV')
            end
          else
            expect!(!state.key?('voltageMv'), 'power_renderer.off_state_voltage',
                    "#{state_path}/voltageMv",
                    'an off state must not declare voltageMv')
          end
        end
      end

      def validate_control!(control, path)
        exact_keys!(
          control,
          %w[id token signal source activeSense status reason recipes],
          %w[ownerDomain], path
        )
        validate_ledger_metadata!(control, path)
        require_expected_ledger!(control, path, 'active control')
        token!(control['token'], "#{path}/token")
        identifier!(control['signal'], "#{path}/signal")
        enum!(control['source'], 'control source', "#{path}/source")
        enum!(control['activeSense'], 'active sense', "#{path}/activeSense")
      end

      def validate_domain_shape!(domain, path)
        exact_keys!(
          domain,
          %w[
            domain token name mode primaryPower primaryGround defaultState
            states parameters elements elementBindings status reason recipes
          ],
          [], path
        )
        validate_ledger_metadata!(domain, path)
        require_expected_ledger!(domain, path, 'active Domain')
        token!(domain['token'], "#{path}/token")
        opaque_id!(domain['name'], "#{path}/name")
        enum_value!(domain['mode'], %w[always-on switchable], "#{path}/mode")
        opaque_id!(domain['primaryPower'], "#{path}/primaryPower")
        opaque_id!(domain['primaryGround'], "#{path}/primaryGround")
        default_state = opaque_id!(domain['defaultState'],
                                   "#{path}/defaultState")
        elements = array!(domain['elements'], "#{path}/elements", nonempty: true)
        elements.each_with_index do |element, index|
          hierarchical_name!(element, "#{path}/elements/#{index}")
        end
        expect!(elements.uniq.size == elements.size,
                'power_renderer.duplicate_domain_element', "#{path}/elements",
                'Domain elements must be unique')
        validate_element_bindings!(domain['elementBindings'],
                                   "#{path}/elementBindings", elements)
        object!(domain['parameters'], "#{path}/parameters")
        states = array!(domain['states'], "#{path}/states", nonempty: true)
        seen = {}
        states.each_with_index do |state, index|
          state_path = "#{path}/states/#{index}"
          object!(state, state_path)
          exact_keys!(state, %w[id token powerState groundState behavior], [],
                      state_path)
          id = opaque_id!(state['id'], "#{state_path}/id")
          expect!(!seen.key?(id), 'power_renderer.duplicate_domain_state',
                  "#{state_path}/id", "duplicate Domain state #{id}")
          seen[id] = true
          token!(state['token'], "#{state_path}/token")
          opaque_id!(state['powerState'], "#{state_path}/powerState")
          opaque_id!(state['groundState'], "#{state_path}/groundState")
          enum!(state['behavior'], 'behavior', "#{state_path}/behavior")
        end
        expect!(seen.key?(default_state), 'power_renderer.unknown_default_state',
                "#{path}/defaultState",
                'defaultState must name a declared Domain state')
      end

      def validate_power_switch_shape!(power_switch, path)
        exact_keys!(
          power_switch,
          %w[
            id token domain inputSupply outputSupply control onSense
            technologyCellMappingId status reason recipes
          ],
          [], path
        )
        validate_ledger_metadata!(power_switch, path)
        require_expected_ledger!(power_switch, path, 'power switch')
        token!(power_switch['token'], "#{path}/token")
        %w[domain inputSupply outputSupply control].each do |key|
          opaque_id!(power_switch[key], "#{path}/#{key}")
        end
        if power_switch['technologyCellMappingId']
          opaque_id!(power_switch['technologyCellMappingId'],
                     "#{path}/technologyCellMappingId")
        end
        enum!(power_switch['onSense'], 'active sense', "#{path}/onSense")
      end

      def validate_retention_shape!(retention, path)
        exact_keys!(
          retention,
          %w[
            id token domain supply saveControl restoreControl saveEdge
            restoreEdge location technologyCellMappingId status reason recipes
          ],
          [], path
        )
        validate_ledger_metadata!(retention, path)
        require_expected_ledger!(retention, path, 'retention strategy')
        token!(retention['token'], "#{path}/token")
        %w[domain supply saveControl restoreControl].each do |key|
          opaque_id!(retention[key], "#{path}/#{key}")
        end
        if retention['technologyCellMappingId']
          opaque_id!(retention['technologyCellMappingId'],
                     "#{path}/technologyCellMappingId")
        end
        enum!(retention['location'], 'location', "#{path}/location")
        enum_value!(retention['saveEdge'], %w[posedge negedge],
                    "#{path}/saveEdge")
        enum_value!(retention['restoreEdge'], %w[posedge negedge],
                    "#{path}/restoreEdge")
      end

      def index_technology!
        technology = object!(@plan['technology'], '/technology')
        exact_keys!(technology, %w[profile interfaceCells], [], '/technology')
        @technology_profile = enum!(technology['profile'], 'technology profile',
                                    '/technology/profile')
        cells = array!(technology['interfaceCells'], '/technology/interfaceCells')
        @technology_cells = {}
        cells.each_with_index do |cell, index|
          path = "/technology/interfaceCells/#{index}"
          object!(cell, path)
          exact_keys!(cell, %w[id kind cells], %w[direction], path)
          id = opaque_id!(cell['id'], "#{path}/id")
          expect!(!@technology_cells.key?(id),
                  'power_renderer.duplicate_technology_mapping', "#{path}/id",
                  "duplicate technology mapping #{id}")
          kind = enum_value!(cell['kind'],
                             %w[isolation level-shifter retention power-switch],
                             "#{path}/kind")
          if kind == 'level-shifter'
            enum_value!(cell['direction'], %w[up down], "#{path}/direction")
          else
            expect!(!cell.key?('direction'),
                    'power_renderer.unexpected_mapping_direction',
                    "#{path}/direction",
                    'direction is valid only for level-shifter mappings')
          end
          lib_cells = array!(cell['cells'], "#{path}/cells", nonempty: true)
          lib_cells.each_with_index do |name, cell_index|
            library_cell_name!(name, "#{path}/cells/#{cell_index}")
          end
          expect!(lib_cells.uniq.size == lib_cells.size,
                  'power_renderer.duplicate_library_cell', "#{path}/cells",
                  'library cell names must be unique')
          @technology_cells[id] = cell
        end
        if @technology_profile == 'abstract'
          expect!(@technology_cells.empty?,
                  'power_renderer.abstract_technology_has_cells',
                  '/technology/interfaceCells',
                  'abstract technology must not bind library cells')
        end
      end

      def validate_references!
        validate_control_port_bijection!
        internal_outputs = Hash.new { |hash, key| hash[key] = [] }
        @power_switches.each do |id, power_switch|
          path = entry_path('powerSwitches', @power_switches, id)
          domain_ref!(power_switch['domain'], "#{path}/domain")
          supply_ref!(power_switch['inputSupply'], 'power',
                      "#{path}/inputSupply")
          output = supply_ref!(power_switch['outputSupply'], 'power',
                               "#{path}/outputSupply")
          expect!(output['exposure'] == 'internal-switched',
                  'power_renderer.switch_output_not_internal',
                  "#{path}/outputSupply",
                  'power switch output must be an internal-switched supply')
          control_ref!(power_switch['control'], "#{path}/control")
          control = @controls.fetch(power_switch['control'])
          expect!(power_switch['onSense'] == control['activeSense'],
                  'power_renderer.switch_sense_mismatch', "#{path}/onSense",
                  'power switch onSense must match its control activeSense')
          mapping_ref!(power_switch['technologyCellMappingId'], 'power-switch',
                       nil, "#{path}/technologyCellMappingId")
          internal_outputs[power_switch['outputSupply']] << id
        end
        @supplies.each do |id, supply|
          next unless supply['exposure'] == 'internal-switched'

          drivers = internal_outputs[id]
          expect!(drivers.size == 1, 'power_renderer.invalid_internal_driver',
                  supply_path(id, 'exposure'),
                  'an internal-switched supply requires exactly one power switch')
        end
        @domains.each do |id, domain|
          power = supply_ref!(domain['primaryPower'], 'power',
                              domain_path(id, 'primaryPower'))
          ground = supply_ref!(domain['primaryGround'], 'ground',
                               domain_path(id, 'primaryGround'))
          validate_domain_states!(domain, power, ground, id)
        end
        @retentions.each do |id, retention|
          path = entry_path('retentions', @retentions, id)
          domain_ref!(retention['domain'], "#{path}/domain")
          supply_ref!(retention['supply'], 'power', "#{path}/supply")
          control_ref!(retention['saveControl'], "#{path}/saveControl")
          control_ref!(retention['restoreControl'], "#{path}/restoreControl")
          mapping_ref!(retention['technologyCellMappingId'], 'retention', nil,
                       "#{path}/technologyCellMappingId")
        end
      end

      def validate_control_port_bijection!
        expected = @controls.select do |_id, control|
          control['source'] == 'top-port'
        end
        expect!(@logic_control_ports.keys.sort == expected.keys.sort,
                'power_renderer.logic_control_bijection_mismatch',
                '/hierarchyFacts/logicControlPorts',
                'hierarchy logic controls must match top-port controls exactly')
        @logic_control_ports.each do |id, port|
          control = expected.fetch(id)
          expect!(port['signal'] == control['signal'],
                  'power_renderer.logic_control_mismatch',
                  "#{pointer('/hierarchyFacts/logicControlPorts', id)}/signal",
                  'hierarchy logic control signal differs from power intent')
        end
      end

      def validate_domain_states!(domain, power, ground, domain_id)
        power_states = power['states'].to_h { |state| [state['id'], state] }
        ground_states = ground['states'].to_h { |state| [state['id'], state] }
        domain['states'].each_with_index do |state, index|
          path = "#{domain_path(domain_id, 'states')}/#{index}"
          expect!(power_states.key?(state['powerState']),
                  'power_renderer.unknown_power_state', "#{path}/powerState",
                  'Domain state references an unknown power state')
          expect!(ground_states.key?(state['groundState']),
                  'power_renderer.unknown_ground_state', "#{path}/groundState",
                  'Domain state references an unknown ground state')
        end
      end

      def validate_system_states!
        expect!(!@system_states.empty?, 'power_renderer.empty_system_states',
                '/systemStates', 'at least one system state is required')
        seen = {}
        reachable = @domains.keys.to_h { |id| [id, {}] }
        @system_states.each_with_index do |state, index|
          path = "/systemStates/#{index}"
          object!(state, path)
          exact_keys!(state, %w[id domainStates], [], path)
          id = opaque_id!(state['id'], "#{path}/id")
          expect!(!seen.key?(id), 'power_renderer.duplicate_system_state',
                  "#{path}/id", "duplicate system state #{id}")
          seen[id] = true
          domain_states = array!(state['domainStates'], "#{path}/domainStates")
          vector_domains = {}
          domain_states.each_with_index do |entry, entry_index|
            entry_path = "#{path}/domainStates/#{entry_index}"
            object!(entry, entry_path)
            exact_keys!(entry, %w[domain state], [], entry_path)
            domain_id = opaque_id!(entry['domain'], "#{entry_path}/domain")
            state_id = opaque_id!(entry['state'], "#{entry_path}/state")
            expect!(!vector_domains.key?(domain_id),
                    'power_renderer.duplicate_system_domain',
                    "#{entry_path}/domain",
                    "duplicate Domain #{domain_id} in system state")
            vector_domains[domain_id] = true
            domain = domain_ref!(domain_id, "#{entry_path}/domain")
            known = domain['states'].any? { |candidate| candidate['id'] == state_id }
            expect!(known, 'power_renderer.unknown_domain_state',
                    "#{entry_path}/state",
                    "unknown state #{state_id} for Domain #{domain_id}")
            reachable.fetch(domain_id)[state_id] = true
          end
          expected_domains = @domains.keys.sort
          actual_domains = vector_domains.keys.sort
          expect!(actual_domains == expected_domains,
                  'power_renderer.incomplete_system_state',
                  "#{path}/domainStates",
                  'system state must cover every active Domain exactly once')
        end
        expect!(seen.key?(@default_system_state),
                'power_renderer.unknown_default_system_state',
                '/defaultSystemState',
                'defaultSystemState must name a declared system state')
        default_vector = @system_states.find do |state|
          state['id'] == @default_system_state
        end['domainStates'].to_h { |entry| [entry['domain'], entry['state']] }
        expected_default = @domains.to_h do |id, domain|
          [id, domain['defaultState']]
        end
        expect!(default_vector == expected_default,
                'power_renderer.default_system_state_mismatch',
                '/defaultSystemState',
                'default system state must select every Domain defaultState')
        @domains.each do |domain_id, domain|
          domain['states'].each_with_index do |state, state_index|
            expect!(reachable.fetch(domain_id).key?(state['id']),
                    'power_renderer.unreachable_domain_state',
                    "#{domain_path(domain_id, 'states')}/#{state_index}",
                    'active Domain state is absent from all system states')
          end
        end
      end

      def emit_header
        @lines << '# Generated power intent: IEEE 1801-2013 (UPF 2.1)'
        @lines << "# design=#{safe_comment(@design)}"
        @lines << '# System-state vectors are recorded in the render receipt; no system-state command is inferred.'
        emit_command('upf_version', ['2.1'], item_type: 'document',
                     item_id: @design)
        emit_command('set_design_top', [scalar(@top_module)],
                     item_type: 'document', item_id: @design)
      end

      def render_supplies
        sorted_entries(@supplies).each do |id, supply|
          if supply['exposure'] == 'external-port'
            trace('supply', id)
            emit_command('create_supply_port', [scalar(supply['port'])],
                         item_type: 'supply', item_id: id)
          end
          trace('supply', id) if supply['exposure'] == 'internal-switched'
          emit_command('create_supply_net', [scalar(supply['net'])],
                       item_type: 'supply', item_id: id)
          next unless supply['exposure'] == 'external-port'

          emit_command(
            'connect_supply_net',
            [scalar(supply['net']), '-ports', list_word([supply['port']])],
            item_type: 'supply', item_id: id
          )
        end
      end

      def render_controls
        sorted_entries(@controls).each do |id, control|
          if control['source'] == 'top-port'
            @control_nets[id] = control['signal']
            next
          end

          trace('control', id)
          logic_net = generated_token('logic_net', id)
          @control_nets[id] = logic_net
          emit_command(
            'create_logic_port', [scalar(control['signal']), '-direction', 'in'],
            item_type: 'control', item_id: id
          )
          emit_command('create_logic_net', [scalar(logic_net)],
                       item_type: 'control', item_id: id)
          emit_command(
            'connect_logic_net',
            [scalar(logic_net), '-ports', list_word([control['signal']])],
            item_type: 'control', item_id: id
          )
        end
      end

      def render_domains
        sorted_entries(@domains).each do |id, domain|
          trace('domain', id)
          emit_command(
            'create_power_domain',
            [scalar(domain['token']), '-elements', list_word(domain['elements'].sort)],
            item_type: 'domain', item_id: id
          )
          supply_set = primary_supply_set_token(id)
          power = @supplies.fetch(domain['primaryPower'])
          ground = @supplies.fetch(domain['primaryGround'])
          emit_command(
            'create_supply_set',
            [scalar(supply_set), '-function', list_word(['power', power['net']]),
             '-function', list_word(['ground', ground['net']])],
            item_type: 'domain', item_id: id
          )
          emit_command(
            'associate_supply_set',
            [scalar(supply_set), '-handle', scalar("#{domain['token']}.primary")],
            item_type: 'domain', item_id: id
          )
          render_domain_states(id, domain, power, ground, supply_set)
        end
      end

      def render_domain_states(domain_id, domain, power, ground, supply_set)
        power_states = power['states'].to_h { |state| [state['id'], state] }
        ground_states = ground['states'].to_h { |state| [state['id'], state] }
        domain['states'].sort_by { |state| state['id'] }.each do |state|
          power_state = power_states.fetch(state['powerState'])
          ground_state = ground_states.fetch(state['groundState'])
          expression = supply_expression(power_state, ground_state)
          simstate = state['behavior'] == 'operational' ? 'NORMAL' : 'CORRUPT'
          emit_command(
            'add_power_state',
            [scalar(supply_set), '-state',
             list_word([state['token'], '-supply_expr', expression,
                        '-simstate', simstate])],
            item_type: 'domain-state',
            item_id: "#{domain_id}/#{state['id']}"
          )
        end
      end

      def render_power_switches
        sorted_entries(@power_switches).each do |id, power_switch|
          trace('power-switch', id)
          domain = @domains.fetch(power_switch['domain'])
          input = @supplies.fetch(power_switch['inputSupply'])
          output = @supplies.fetch(power_switch['outputSupply'])
          control = @controls.fetch(power_switch['control'])
          input_port = generated_token('switch_input', id)
          output_port = generated_token('switch_output', id)
          control_port = generated_token('switch_control', id)
          on_state = generated_token('switch_on', id)
          off_state = generated_token('switch_off', id)
          on_condition = power_switch['onSense'] == 'high' ? control_port : "!#{control_port}"
          off_condition = power_switch['onSense'] == 'high' ? "!#{control_port}" : control_port
          emit_command(
            'create_power_switch',
            [scalar(power_switch['token']), '-domain', scalar(domain['token']),
             '-input_supply_port', list_word([input_port, input['net']]),
             '-output_supply_port', list_word([output_port, output['net']]),
             '-control_port', list_word([control_port, control_net(control)]),
             '-on_state', list_word([on_state, input_port, on_condition]),
             '-off_state', list_word([off_state, off_condition])],
            item_type: 'power-switch', item_id: id
          )
          render_power_switch_mapping(id, power_switch, domain)
        end
      end

      def render_power_switch_mapping(id, power_switch, domain)
        mapping = mapping_ref!(power_switch['technologyCellMappingId'],
                               'power-switch', nil,
                               power_switch_path(id, 'technologyCellMappingId'))
        return record_abstract_binding('power-switch', id) if abstract_technology?

        emit_command(
          'map_power_switch_cell',
          [scalar(power_switch['token']), '-domain', scalar(domain['token']),
           '-lib_cells', list_word(mapping['cells'].sort)],
          item_type: 'power-switch-binding', item_id: id
        )
      end

      def render_retentions
        sorted_entries(@retentions).each do |id, retention|
          trace('retention', id)
          domain = @domains.fetch(retention['domain'])
          supply = @supplies.fetch(retention['supply'])
          ground = @supplies.fetch(domain['primaryGround'])
          retention_set = generated_token('retention_supply_set', id)
          emit_command(
            'create_supply_set',
            [scalar(retention_set), '-function', list_word(['power', supply['net']]),
             '-function', list_word(['ground', ground['net']])],
            item_type: 'retention', item_id: id
          )
          emit_command(
            'set_retention',
            [scalar(retention['token']), '-domain', scalar(domain['token']),
             '-retention_supply_set', scalar(retention_set), '-location',
             retention['location']],
            item_type: 'retention', item_id: id
          )
          save = @controls.fetch(retention['saveControl'])
          restore = @controls.fetch(retention['restoreControl'])
          emit_command(
            'set_retention_control',
             [scalar(retention['token']), '-domain', scalar(domain['token']),
             '-save_signal', list_word([control_net(save), retention['saveEdge']]),
             '-restore_signal', list_word([control_net(restore), retention['restoreEdge']])],
            item_type: 'retention', item_id: id
          )
          render_retention_mapping(id, retention, domain)
          record_strategy(
            id, 'retention', retention['status'], 'emitted',
            reason_code: retention['reason'], recipes: retention['recipes']
          )
        end
      end

      def render_retention_mapping(id, retention, domain)
        mapping = mapping_ref!(retention['technologyCellMappingId'], 'retention',
                               nil, retention_path(id, 'technologyCellMappingId'))
        return record_abstract_binding('retention', id) if abstract_technology?

        emit_command(
          'map_retention_cell',
          [scalar(retention['token']), '-domain', scalar(domain['token']),
           '-lib_cells', list_word(mapping['cells'].sort)],
          item_type: 'retention-binding', item_id: id
        )
      end

      def render_crossing_strategies
        strategies = collect_crossing_strategies
        strategies.sort_by { |entry| [entry.fetch('kind'), entry.fetch('id')] }.each do |entry|
          strategy = entry.fetch('strategy')
          status = strategy.fetch('status')
          if status == 'expected'
            entry.fetch('kind') == 'isolation' ? render_isolation(entry) : render_level_shifter(entry)
          else
            record_strategy(entry.fetch('id'), entry.fetch('kind'), status,
                            status == 'deferred' ? 'deferred' : 'not-required',
                            reason_code: strategy['reason'],
                            recipes: strategy['recipes'])
          end
        end
      end

      def collect_crossing_strategies
        result = []
        strategy_ids = {}
        @edge_orientations.each_with_index do |orientation, orientation_index|
          orientation_path = "/edgeOrientations/#{orientation_index}"
          object!(orientation, orientation_path)
          exact_keys!(
            orientation,
            %w[
              id token edge orientation producer consumer sourceSupplyDomain
              destinationSupplyDomain powerBoundary status reason recipes
              signalFlows
            ],
            [], orientation_path
          )
          validate_ledger_metadata!(orientation, orientation_path)
          validate_reference!(orientation['edge'],
                              "#{orientation_path}/edge")
          validate_reference!(orientation['producer'],
                              "#{orientation_path}/producer")
          validate_reference!(orientation['consumer'],
                              "#{orientation_path}/consumer")
          %w[sourceSupplyDomain destinationSupplyDomain].each do |key|
            next if orientation[key].nil?

            domain_ref!(orientation[key], "#{orientation_path}/#{key}")
          end
          orientation_boundary = validate_power_boundary!(
            orientation['powerBoundary'], "#{orientation_path}/powerBoundary"
          )
          flows = array!(orientation['signalFlows'], "#{orientation_path}/signalFlows")
          flows.each_with_index do |flow, flow_index|
            flow_path = "#{orientation_path}/signalFlows/#{flow_index}"
            object!(flow, flow_path)
            exact_keys!(
              flow,
              %w[
                id token net type side direction driver receiver powerBoundary
                status reason recipes isolation levelShifter
              ],
              [], flow_path
            )
            validate_ledger_metadata!(flow, flow_path)
            opaque_id!(flow['id'], "#{flow_path}/id")
            token!(flow['token'], "#{flow_path}/token")
            identifier!(flow['net'], "#{flow_path}/net")
            validate_endpoint!(flow['driver'], "#{flow_path}/driver")
            validate_endpoint!(flow['receiver'], "#{flow_path}/receiver")
            boundary_status = validate_power_boundary!(
              flow['powerBoundary'], "#{flow_path}/powerBoundary"
            )
            expect!(boundary_status == orientation_boundary,
                    'power_renderer.power_boundary_mismatch',
                    "#{flow_path}/powerBoundary/status",
                    'flow powerBoundary must match its edge orientation')
            %w[isolation levelShifter].each do |key|
              next unless flow.key?(key)

              kind = key == 'isolation' ? 'isolation' : 'level-shifter'
              entry = validate_crossing_strategy!(
                flow[key], kind, "#{flow_path}/#{key}", boundary_status
              )
              if entry['status'] == 'expected'
                expect!(orientation['status'] == 'expected' &&
                          flow['status'] == 'expected',
                        'power_renderer.strategy_on_non_emittable_parent',
                        "#{flow_path}/#{key}/status",
                        'an expected strategy requires expected edge and flow status')
              end
              composite_id = "#{kind}:#{entry.fetch('id')}"
              expect!(!strategy_ids.key?(composite_id),
                      'power_renderer.duplicate_strategy',
                      "#{flow_path}/#{key}/id",
                      "duplicate #{kind} strategy #{entry.fetch('id')}")
              strategy_ids[composite_id] = true
              result << {
                'id' => entry.fetch('id'),
                'kind' => kind,
                'strategy' => entry,
                'flowId' => flow.fetch('id')
              }
            end
            if boundary_status == 'none'
              unexpected = %w[isolation levelShifter].any? do |key|
                flow.dig(key, 'status') == 'expected'
              end
              expect!(!unexpected,
                      'power_renderer.strategy_without_power_boundary',
                      "#{flow_path}/powerBoundary/status",
                      'a same-power flow must not emit a power strategy')
            elsif boundary_status == 'deferred'
              expected = %w[isolation levelShifter].any? do |key|
                flow.dig(key, 'status') == 'expected'
              end
              expect!(!expected,
                      'power_renderer.strategy_on_deferred_boundary',
                      "#{flow_path}/powerBoundary/status",
                      'a deferred power boundary must not emit a strategy')
            end
          end
        end
        result
      end

      def validate_endpoint!(endpoint, path)
        object!(endpoint, path)
        exact_keys!(endpoint, %w[instance pin domain], [], path)
        hierarchical_name!(endpoint['instance'], "#{path}/instance")
        identifier!(endpoint['pin'], "#{path}/pin")
        return if endpoint['domain'].nil?

        opaque_id!(endpoint['domain'], "#{path}/domain")
        domain_ref!(endpoint['domain'], "#{path}/domain")
      end

      def validate_crossing_strategy!(strategy, kind, path, boundary_status)
        object!(strategy, path)
        status_key = kind == 'isolation' ? 'isolation status' : 'level-shifter status'
        status = enum!(strategy['status'], status_key, "#{path}/status")
        common = %w[
          id token domain elements appliesTo technologyCellMappingId status
          reason recipes
        ]
        expected = kind == 'isolation' ?
          %w[supply clampValue isolationControl isolationSense location] :
          %w[rule location]
        exact_keys!(strategy, common + (status == 'expected' ? expected : []),
                    [], path)
        validate_ledger_metadata!(strategy, path)
        opaque_id!(strategy['id'], "#{path}/id")
        token!(strategy['token'], "#{path}/token")
        if status != 'expected'
          opaque_id!(strategy['domain'], "#{path}/domain") if strategy['domain']
          array!(strategy['elements'], "#{path}/elements")
          if strategy['technologyCellMappingId']
            opaque_id!(strategy['technologyCellMappingId'],
                       "#{path}/technologyCellMappingId")
          end
          return strategy
        end
        expect!(boundary_status != 'deferred',
                'power_renderer.strategy_on_deferred_boundary', "#{path}/status",
                'a deferred power boundary must not emit a strategy')
        domain_ref!(strategy['domain'], "#{path}/domain")
        elements = array!(strategy['elements'], "#{path}/elements", nonempty: true)
        elements.each_with_index do |element, index|
          hierarchical_name!(element, "#{path}/elements/#{index}")
        end
        enum!(strategy['appliesTo'], 'appliesTo', "#{path}/appliesTo")
        enum!(strategy['location'], 'location', "#{path}/location")
        mapping_kind = kind
        direction = nil
        if kind == 'isolation'
          expect!([0, 1].include?(strategy['clampValue']),
                  'power_renderer.invalid_clamp_value', "#{path}/clampValue",
                  'isolation clampValue must be 0 or 1')
          control_ref!(strategy['isolationControl'],
                       "#{path}/isolationControl")
          enum!(strategy['isolationSense'], 'active sense',
                "#{path}/isolationSense")
          supply_ref!(strategy['supply'], 'power', "#{path}/supply")
          control = @controls.fetch(strategy['isolationControl'])
          expect!(strategy['isolationSense'] == control['activeSense'],
                  'power_renderer.isolation_sense_mismatch',
                  "#{path}/isolationSense",
                  'isolationSense must match its control activeSense')
        else
          rule = enum!(strategy['rule'], 'level-shifter rule', "#{path}/rule")
          direction = rule == 'low_to_high' ? 'up' : 'down'
        end
        mapping_ref!(strategy['technologyCellMappingId'], mapping_kind,
                     direction, "#{path}/technologyCellMappingId")
        strategy
      end

      def validate_reference!(reference, path)
        object!(reference, path)
        exact_keys!(reference, %w[kind id], [], path)
        opaque_id!(reference['kind'], "#{path}/kind")
        opaque_id!(reference['id'], "#{path}/id")
      end

      def validate_element_bindings!(value, path, elements)
        bindings = array!(value, path, nonempty: true)
        instances = []
        bindings.each_with_index do |binding, index|
          binding_path = "#{path}/#{index}"
          object!(binding, binding_path)
          exact_keys!(binding, %w[element module instance], [], binding_path)
          validate_reference!(binding['element'], "#{binding_path}/element")
          identifier!(binding['module'], "#{binding_path}/module")
          instance = hierarchical_name!(binding['instance'],
                                        "#{binding_path}/instance")
          instances << instance
        end
        expect!(instances.sort == elements.sort,
                'power_renderer.element_binding_mismatch', path,
                'elementBindings instances must equal Domain elements')
      end

      def validate_power_boundary!(boundary, path)
        object!(boundary, path)
        status = enum!(boundary['status'], 'power boundary status',
                       "#{path}/status")
        required = status == 'deferred' ? %w[status reasonCode] : %w[status]
        exact_keys!(boundary, required, [], path)
        opaque_id!(boundary['reasonCode'], "#{path}/reasonCode") if status == 'deferred'
        status
      end

      def validate_ledger_metadata!(entry, path)
        enum_value!(entry['status'], %w[expected not-required deferred],
                    "#{path}/status")
        opaque_id!(entry['reason'], "#{path}/reason")
        recipes = array!(entry['recipes'], "#{path}/recipes")
        recipes.each_with_index do |recipe, index|
          validate_recipe_descriptor!(recipe, "#{path}/recipes/#{index}")
        end
      end

      def require_expected_ledger!(entry, path, label)
        expect!(entry['status'] == 'expected',
                'power_renderer.non_emittable_item', "#{path}/status",
                "#{label} must have expected status before it can emit UPF")
      end

      def validate_recipe_descriptor!(recipe, path)
        object!(recipe, path)
        exact_keys!(
          recipe,
          %w[recipe],
          %w[
            order recipeKind role domainType fromDomain toDomain policy parameters
            orientation directionParameters
          ],
          path
        )
        opaque_id!(recipe['recipe'], "#{path}/recipe")
        if recipe.key?('order')
          expect!(recipe['order'].is_a?(Integer) && recipe['order'] >= 0,
                  'power_renderer.invalid_recipe_order', "#{path}/order",
                  'recipe order must be a non-negative integer')
        end
        %w[
          recipeKind role domainType fromDomain toDomain orientation
        ].each do |key|
          opaque_id!(recipe[key], "#{path}/#{key}") if recipe.key?(key)
        end
        if recipe.key?('policy')
          policy = object!(recipe['policy'], "#{path}/policy")
          exact_keys!(policy, %w[source id], [], "#{path}/policy")
          opaque_id!(policy['source'], "#{path}/policy/source")
          opaque_id!(policy['id'], "#{path}/policy/id")
        end
        object!(recipe['parameters'], "#{path}/parameters") if recipe.key?('parameters')
        if recipe.key?('directionParameters')
          object!(recipe['directionParameters'], "#{path}/directionParameters")
        end
      end

      def render_isolation(entry)
        id = entry.fetch('id')
        strategy = entry.fetch('strategy')
        trace('isolation', id)
        domain = @domains.fetch(strategy['domain'])
        control = @controls.fetch(strategy['isolationControl'])
        isolation_supply = @supplies.fetch(strategy['supply'])
        ground = @supplies.fetch(domain['primaryGround'])
        supply_set = generated_token('isolation_supply_set', id)
        emit_command(
          'create_supply_set',
          [scalar(supply_set), '-function', list_word(['power', isolation_supply['net']]),
           '-function', list_word(['ground', ground['net']])],
          item_type: 'isolation', item_id: id
        )
        emit_command(
          'set_isolation',
          [scalar(strategy['token']), '-domain', scalar(domain['token']),
           '-elements', list_word(strategy['elements'].sort), '-applies_to',
           strategy['appliesTo'], '-clamp_value', strategy['clampValue'].to_s,
           '-isolation_signal', scalar(control_net(control)), '-isolation_sense',
           strategy['isolationSense'], '-isolation_supply_set', scalar(supply_set),
           '-location', strategy['location']],
          item_type: 'isolation', item_id: id
        )
        render_interface_cell_binding(entry, domain, 'isolation', nil)
        record_strategy(
          id, 'isolation', strategy['status'], 'emitted',
          reason_code: strategy['reason'], recipes: strategy['recipes']
        )
      end

      def render_level_shifter(entry)
        id = entry.fetch('id')
        strategy = entry.fetch('strategy')
        trace('level-shifter', id)
        domain = @domains.fetch(strategy['domain'])
        emit_command(
          'set_level_shifter',
          [scalar(strategy['token']), '-domain', scalar(domain['token']),
           '-elements', list_word(strategy['elements'].sort), '-applies_to',
           strategy['appliesTo'], '-rule', strategy['rule'], '-location',
           strategy['location']],
          item_type: 'level-shifter', item_id: id
        )
        direction = strategy['rule'] == 'low_to_high' ? 'up' : 'down'
        render_interface_cell_binding(entry, domain, 'level-shifter', direction)
        record_strategy(
          id, 'level-shifter', strategy['status'], 'emitted',
          reason_code: strategy['reason'], recipes: strategy['recipes']
        )
      end

      def render_interface_cell_binding(entry, domain, kind, direction)
        return record_abstract_binding(kind, entry.fetch('id')) if abstract_technology?

        strategy = entry.fetch('strategy')
        mapping = mapping_ref!(strategy['technologyCellMappingId'], kind,
                               direction, '/technology/interfaceCells')
        binding_token = generated_token(
          'interface_cell', "#{kind}\0#{entry.fetch('id')}"
        )
        emit_command(
          'use_interface_cell',
          [scalar(binding_token), '-domain', scalar(domain['token']),
           '-strategy', scalar(strategy['token']), '-lib_cells',
           list_word(mapping['cells'].sort)],
          item_type: "#{kind}-binding", item_id: entry.fetch('id')
        )
      end

      def build_receipt
        binding_command_count = @commands.count do |command|
          TECHNOLOGY_BINDING_COMMANDS.include?(command.fetch('kind'))
        end
        binding_status = if abstract_technology?
                           'abstract-unbound'
                         elsif binding_command_count.positive?
                           'emitted'
                         else
                           'not-required'
                         end
        {
          'format' => RECEIPT_FORMAT,
          'formatVersion' => 1,
          'source' => {'format' => PLAN_FORMAT, 'formatVersion' => PLAN_VERSION},
          'design' => @design.dup,
          'upfVersion' => UPF_VERSION,
          'technology' => {
            'profile' => @technology_profile.dup,
            'bindingStatus' => binding_status,
            'bindingCommandCount' => binding_command_count
          },
          'validation' => {
            'rendererValidation' => 'complete',
            'tclSyntaxValidation' => 'not-performed-by-renderer',
            'commercialSemanticValidation' => 'not-performed'
          },
          'commandCount' => @commands.size,
          'commands' => canonical_copy(@commands),
          'strategyCoverage' => canonical_copy(
            @strategy_coverage.sort_by do |entry|
              [entry.fetch('kind'), entry.fetch('id')]
            end
          ),
          'implementationCoverage' => canonical_copy(@plan.fetch('coverage')),
          'inactiveIntent' => canonical_copy(@plan.fetch('inactiveIntent')),
          'systemStates' => {
            'status' => 'receipt-only',
            'reasonCode' => 'power_renderer.system_state_command_not_inferred',
            'defaultSystemState' => @default_system_state.dup,
            'vectors' => canonical_system_vectors
          }
        }
      end

      def canonical_system_vectors
        @system_states.sort_by { |state| state['id'] }.map do |state|
          {
            'id' => state['id'].dup,
            'domainStates' => state['domainStates'].sort_by do |entry|
              entry['domain']
            end.map do |entry|
              {'domain' => entry['domain'].dup, 'state' => entry['state'].dup}
            end
          }
        end
      end

      def record_strategy(id, kind, source_status, render_status,
                          reason_code: nil, recipes: [])
        command_kinds = @commands.select do |command|
          command['itemId'] == id &&
            (command['itemType'] == kind || command['itemType'] == "#{kind}-binding")
        end.map { |command| command['kind'].dup }
        entry = {
          'id' => id.dup,
          'kind' => kind.dup,
          'sourceStatus' => source_status.dup,
          'renderStatus' => render_status.dup,
          'recipes' => canonical_copy(recipes),
          'commandKinds' => command_kinds,
          'commandCount' => command_kinds.size
        }
        entry['reasonCode'] = reason_code.dup if reason_code
        @strategy_coverage << entry
      end

      def record_abstract_binding(_kind, _id)
        nil
      end

      def emit_command(kind, arguments, item_type:, item_id:)
        @lines << ([kind] + arguments).join(' ')
        @commands << {
          'sequence' => @commands.size,
          'kind' => kind,
          'itemType' => item_type,
          'itemId' => item_id.dup,
          'arguments' => arguments.map(&:dup),
          'emitted' => true
        }
      end

      def control_net(control)
        @control_nets.fetch(control.fetch('id'))
      end

      def trace(kind, id)
        digest = Digest::SHA256.hexdigest(id)
        @lines << "# item kind=#{kind} id=#{safe_comment(id)} sha256=#{digest}"
      end

      def safe_comment(value)
        value.gsub(/[^A-Za-z0-9_.:\/ -]/, '?').slice(0, 96)
      end

      def scalar(value)
        replacements = {
          '\\' => '\\\\', '"' => '\\"', '$' => '\\$', '[' => '\\[',
          ']' => '\\]', "\n" => '\\n', "\r" => '\\r', "\t" => '\\t'
        }
        escaped = value.to_s.gsub(/[\\"$\[\]\n\r\t]/) do |character|
          replacements.fetch(character)
        end
        "\"#{escaped}\""
      end

      def list_word(values)
        expect!(!values.empty?, 'power_renderer.empty_tcl_list', '/',
                'a rendered Tcl list must not be empty')
        "[list #{values.map { |value| scalar(value) }.join(' ')}]"
      end

      def supply_expression(power_state, ground_state)
        power = if power_state['condition'] == 'off'
                  'OFF'
                else
                  "{FULL_ON #{millivolts_to_volts(power_state['voltageMv'])}}"
                end
        ground = if ground_state['condition'] == 'off'
                   'OFF'
                 else
                   "{FULL_ON #{millivolts_to_volts(ground_state['voltageMv'])}}"
                 end
        "power == #{power} && ground == #{ground}"
      end

      def millivolts_to_volts(value)
        whole = value / 1000
        fraction = (value % 1000).to_s.rjust(3, '0').sub(/0+\z/, '')
        fraction = '0' if fraction.empty?
        "#{whole}.#{fraction}"
      end

      def primary_supply_set_token(domain_id)
        generated_token('primary_supply_set', domain_id)
      end

      def generated_token(namespace, value)
        prefix = {
          'logic_net' => 'ln',
          'primary_supply_set' => 'ss',
          'switch_input' => 'swi',
          'switch_output' => 'swo',
          'switch_control' => 'swc',
          'switch_on' => 'son',
          'switch_off' => 'soff',
          'retention_supply_set' => 'rss',
          'isolation_supply_set' => 'iss',
          'interface_cell' => 'uic'
        }.fetch(namespace)
        token = "#{prefix}_#{Digest::SHA256.hexdigest("#{namespace}\0#{value}")}"
        existing = @token_paths[token]
        expect!(!existing, 'power_renderer.generated_token_collision',
                "/generated/#{namespace}",
                "generated token #{token} collides with #{existing}")
        @token_paths[token] = "/generated/#{namespace}"
        token
      end

      def mapping_ref!(id, expected_kind, expected_direction, path)
        return nil if abstract_technology?

        mapping = @technology_cells[id]
        expect!(mapping, 'power_renderer.unknown_technology_mapping', path,
                "unknown technology mapping #{id}")
        expect!(mapping['kind'] == expected_kind,
                'power_renderer.invalid_technology_mapping_kind', path,
                "technology mapping must be #{expected_kind}")
        if expected_direction
          expect!(mapping['direction'] == expected_direction,
                  'power_renderer.invalid_technology_mapping_direction', path,
                  "technology mapping must have direction #{expected_direction}")
        end
        mapping
      end

      def abstract_technology?
        @technology_profile == 'abstract'
      end

      def domain_ref!(id, path)
        expect!(@domains.key?(id), 'power_renderer.unknown_domain', path,
                "unknown Domain #{id}")
        @domains.fetch(id)
      end

      def supply_ref!(id, kind, path)
        supply = @supplies[id]
        expect!(supply, 'power_renderer.unknown_supply', path,
                "unknown supply #{id}")
        expect!(supply['kind'] == kind, 'power_renderer.invalid_supply_kind', path,
                "supply #{id} must be #{kind}")
        supply
      end

      def control_ref!(id, path)
        expect!(@controls.key?(id), 'power_renderer.unknown_control', path,
                "unknown control #{id}")
        @controls.fetch(id)
      end

      def sorted_entries(index)
        index.sort_by { |id, entry| [entry['token'].to_s, id] }
      end

      def supply_path(id, field)
        entry_path('supplies', @supplies, id, field)
      end

      def domain_path(id, field)
        entry_path('domains', @domains, id, field)
      end

      def power_switch_path(id, field)
        entry_path('powerSwitches', @power_switches, id, field)
      end

      def retention_path(id, field)
        entry_path('retentions', @retentions, id, field)
      end

      def entry_path(collection, index, id, field = nil)
        position = index.keys.index(id)
        base = "/#{collection}/#{position}"
        field ? "#{base}/#{field}" : base
      end

      def token!(value, path)
        token = identifier!(value, path)
        existing = @token_paths[token]
        expect!(!existing, 'power_renderer.duplicate_token', path,
                "token #{token} already used at #{existing}")
        @token_paths[token] = path
        token
      end

      def identifier!(value, path)
        expect!(value.is_a?(String) && value.match?(IDENTIFIER),
                'power_renderer.invalid_identifier', path,
                'expected a Tcl-safe HDL identifier')
        value
      end

      def hierarchical_name!(value, path)
        expect!(value.is_a?(String) && value.match?(HIERARCHICAL_NAME),
                'power_renderer.invalid_hierarchical_name', path,
                'expected a relative hierarchical HDL name')
        value
      end

      def library_cell_name!(value, path)
        expect!(value.is_a?(String) &&
                  value.match?(/\A[A-Za-z_][A-Za-z0-9_$.:\/-]*\z/),
                'power_renderer.invalid_library_cell', path,
                'expected a Tcl-safe library cell name')
        value
      end

      def opaque_id!(value, path)
        expect!(value.is_a?(String) && value.match?(/\S/) &&
                  !value.match?(/\p{Cc}/),
                'power_renderer.invalid_opaque_id', path,
                'expected a non-empty string without control characters')
        value
      end

      def enum!(value, enum_name, path)
        enum_value!(value, ENUMS.fetch(enum_name), path)
      end

      def enum_value!(value, allowed, path)
        expect!(allowed.include?(value), 'power_renderer.invalid_enum', path,
                "expected one of #{allowed.join(', ')}")
        value
      end

      def object!(value, path)
        expect!(value.is_a?(Hash), 'power_renderer.expected_object', path,
                'expected an object')
        value
      end

      def exact_keys!(object, required, optional, path)
        allowed = required + optional
        unknown = object.keys - allowed
        missing = required - object.keys
        unless unknown.empty?
          key = unknown.sort.first
          raise Error.new('power_renderer.unknown_field', pointer(path, key),
                          "unknown field #{key}")
        end
        unless missing.empty?
          key = missing.sort.first
          raise Error.new('power_renderer.missing_field', pointer(path, key),
                          "missing field #{key}")
        end
      end

      def pointer(path, token)
        escaped = token.to_s.gsub('~', '~0').gsub('/', '~1')
        "#{path == '/' ? '' : path}/#{escaped}"
      end

      def array!(value, path, nonempty: false)
        expect!(value.is_a?(Array), 'power_renderer.expected_array', path,
                'expected an array')
        expect!(!nonempty || !value.empty?, 'power_renderer.empty_array', path,
                'array must not be empty')
        value
      end

      def expect!(condition, code, path, message)
        raise Error.new(code, path, message) unless condition
      end

      def deep_freeze!(value)
        case value
        when Hash
          value.each do |key, entry|
            key.freeze
            deep_freeze!(entry)
          end
        when Array
          value.each { |entry| deep_freeze!(entry) }
        end
        value.freeze
      end

      def canonical_copy(value)
        case value
        when Hash
          value.keys.sort.to_h do |key|
            [key.dup, canonical_copy(value.fetch(key))]
          end
        when Array
          value.map { |entry| canonical_copy(entry) }
        when String
          value.dup
        else
          value
        end
      end
    end
  end
end
