# frozen_string_literal: true

require_relative '../domain_rtl_context'

module FinepaperNoc
  module PowerIntent
    class Error < StandardError
      attr_reader :code, :path

      def initialize(code, path, message)
        @code = code
        @path = path
        super("#{message} at #{path}")
      end
    end

    # Validates Package-owned logical power intent against a typed Domain plan
    # and emits a deterministic, renderer-facing power-intent plan.
    class Compiler
      DOCUMENT_FORMAT = 'finepaper.noc-power-intent'
      DOCUMENT_VERSION = 1
      PLAN_FORMAT = 'finepaper.noc-power-intent-plan'
      PLAN_VERSION = 1
      SUPPLY_ROLE = 'supply-domain'
      ISOLATION_RECIPE = 'power-isolation'
      LEVEL_SHIFTER_RECIPE = 'power-level-shifter'
      HDL_IDENTIFIER = /\A[A-Za-z_][A-Za-z0-9_$]*\z/
      CONTROL_CHARACTERS = /\p{Cc}/

      def self.compile(context:, document:)
        new(context: context, document: document).compile
      end

      def initialize(context:, document:)
        @context = context
        @document = document
      end

      def compile
        expect!(@context.is_a?(DomainRtlContext), 'power_intent.invalid_context',
                '/context', 'context must be a DomainRtlContext')
        intent = parse_document(@document)
        index_document!(intent)
        index_plan_domains!
        validate_domain_coverage!
        validate_control_owners!
        index_crossing_requirements!
        compiled_domains = intent.fetch('domains').map do |domain|
          compile_domain(domain)
        end
        validate_supply_topology!(compiled_domains)
        validate_system_states!(intent.fetch('systemStates'),
                                intent.fetch('defaultSystemState'))
        validate_technology!(intent['technology'])

        plan = {
          'format' => PLAN_FORMAT,
          'formatVersion' => PLAN_VERSION,
          'design' => @context.plan.fetch('design'),
          'source' => {
            'format' => DOCUMENT_FORMAT,
            'formatVersion' => DOCUMENT_VERSION
          },
          'implementationPlan' => {
            'format' => @context.plan.fetch('format'),
            'formatVersion' => @context.plan.fetch('formatVersion')
          },
          'supplies' => sort_by_id(intent.fetch('supplies')).map do |supply|
            supply.merge('states' => sort_by_id(supply.fetch('states')))
          end,
          'controls' => sort_by_id(intent.fetch('controls')),
          'domains' => compiled_domains.sort_by { |domain| domain.fetch('domain') },
          'defaultSystemState' => intent.fetch('defaultSystemState'),
          'systemStates' => sort_by_id(intent.fetch('systemStates')).map do |state|
            state.merge(
              'domainStates' => state.fetch('domainStates').sort_by do |entry|
                entry.fetch('domain')
              end
            )
          end
        }
        plan['technology'] = canonical_technology(intent.fetch('technology')) if intent.key?('technology')
        canonical = canonicalize(plan)
        deep_freeze!(canonical)
      end

      private

      def parse_document(value)
        document = object!(value, '/')
        exact_keys!(document,
                    %w[
                      format formatVersion supplies controls domains
                      defaultSystemState systemStates
                    ],
                    %w[technology], '/')
        expect!(document['format'] == DOCUMENT_FORMAT,
                'power_intent.invalid_format', '/format',
                "expected #{DOCUMENT_FORMAT}")
        expect!(document['formatVersion'] == DOCUMENT_VERSION,
                'power_intent.invalid_version', '/formatVersion',
                "expected format version #{DOCUMENT_VERSION}")
        parsed = {
          'format' => document.fetch('format'),
          'formatVersion' => document.fetch('formatVersion'),
          'supplies' => parse_array(document.fetch('supplies'), '/supplies') do |entry, path|
            parse_supply(entry, path)
          end,
          'controls' => parse_array(document.fetch('controls'), '/controls') do |entry, path|
            parse_control(entry, path)
          end,
          'domains' => parse_array(document.fetch('domains'), '/domains') do |entry, path|
            parse_domain(entry, path)
          end,
          'defaultSystemState' => string!(
            document.fetch('defaultSystemState'), '/defaultSystemState'
          ),
          'systemStates' => parse_array(
            document.fetch('systemStates'), '/systemStates'
          ) { |entry, path| parse_system_state(entry, path) }
        }
        parsed['technology'] = parse_technology(document['technology'], '/technology') if document.key?('technology')
        parsed
      end

      def parse_supply(value, path)
        supply = object!(value, path)
        exact_keys!(supply, %w[id kind exposure net states], %w[port], path)
        result = {
          'id' => string!(supply['id'], "#{path}/id"),
          'kind' => enum!(supply['kind'], %w[power ground], "#{path}/kind"),
          'exposure' => enum!(supply['exposure'],
                              %w[external-port internal-switched],
                              "#{path}/exposure"),
          'net' => hdl_identifier!(supply['net'], "#{path}/net"),
          'states' => parse_array(supply['states'], "#{path}/states", nonempty: true) do |entry, state_path|
            parse_supply_state(entry, state_path)
          end
        }
        if result.fetch('exposure') == 'external-port'
          expect!(supply.key?('port'), 'power_intent.missing_supply_port',
                  "#{path}/port", 'an external supply requires port')
          result['port'] = hdl_identifier!(supply['port'], "#{path}/port")
        else
          expect!(!supply.key?('port'), 'power_intent.unexpected_supply_port',
                  "#{path}/port", 'an internal switched supply forbids port')
          expect!(result.fetch('kind') == 'power',
                  'power_intent.invalid_internal_supply_kind',
                  "#{path}/kind",
                  'only a power supply may be internally switched')
        end
        unique_ids!(result.fetch('states'), "#{path}/states", 'power_intent.duplicate_supply_state')
        result.fetch('states').each_with_index do |state, index|
          next unless state.fetch('condition') == 'full-on'

          voltage_path = "#{path}/states/#{index}/voltageMv"
          if result.fetch('kind') == 'ground'
            expect!(state.fetch('voltageMv').zero?,
                    'power_intent.invalid_ground_voltage', voltage_path,
                    'a full-on ground state must be 0 mV')
          else
            expect!(state.fetch('voltageMv').positive?,
                    'power_intent.invalid_power_voltage', voltage_path,
                    'a full-on power state must be greater than 0 mV')
          end
        end
        result
      end

      def parse_supply_state(value, path)
        state = object!(value, path)
        exact_keys!(state, %w[id condition], %w[voltageMv], path)
        condition = enum!(state['condition'], %w[full-on off], "#{path}/condition")
        if condition == 'full-on'
          expect!(state.key?('voltageMv'), 'power_intent.missing_voltage',
                  "#{path}/voltageMv", 'a full-on state requires voltageMv')
          voltage = json_integer!(state['voltageMv'], "#{path}/voltageMv")
          expect!(voltage >= 0, 'power_intent.invalid_voltage',
                  "#{path}/voltageMv", 'voltageMv must not be negative')
        else
          expect!(!state.key?('voltageMv'), 'power_intent.unexpected_voltage',
                  "#{path}/voltageMv", 'an off state must not declare voltageMv')
        end
        {'id' => string!(state['id'], "#{path}/id"), 'condition' => condition}.tap do |result|
          result['voltageMv'] = voltage if condition == 'full-on'
        end
      end

      def parse_control(value, path)
        control = object!(value, path)
        exact_keys!(control, %w[id signal source activeSense], %w[ownerDomain], path)
        {
          'id' => string!(control['id'], "#{path}/id"),
          'signal' => hdl_identifier!(control['signal'], "#{path}/signal"),
          'source' => enum!(control['source'], %w[top-port upf-port], "#{path}/source"),
          'activeSense' => enum!(control['activeSense'], %w[high low], "#{path}/activeSense")
        }.tap do |result|
          result['ownerDomain'] = string!(control['ownerDomain'], "#{path}/ownerDomain") if control.key?('ownerDomain')
        end
      end

      def parse_domain(value, path)
        domain = object!(value, path)
        exact_keys!(domain,
                    %w[domain primaryPower primaryGround mode defaultState states],
                    %w[powerSwitch retention isolation levelShifter], path)
        result = {
          'domain' => string!(domain['domain'], "#{path}/domain"),
          'primaryPower' => string!(domain['primaryPower'], "#{path}/primaryPower"),
          'primaryGround' => string!(domain['primaryGround'], "#{path}/primaryGround"),
          'mode' => enum!(domain['mode'], %w[always-on switchable], "#{path}/mode"),
          'defaultState' => string!(domain['defaultState'], "#{path}/defaultState"),
          'states' => parse_array(domain['states'], "#{path}/states", nonempty: true) do |entry, state_path|
            parse_domain_state(entry, state_path)
          end
        }
        unique_ids!(result.fetch('states'), "#{path}/states", 'power_intent.duplicate_domain_state')
        if domain.key?('powerSwitch')
          result['powerSwitch'] = parse_power_switch(
            domain['powerSwitch'], "#{path}/powerSwitch"
          )
        end
        result['retention'] = parse_retention(domain['retention'], "#{path}/retention") if domain.key?('retention')
        result['isolation'] = parse_isolation(domain['isolation'], "#{path}/isolation") if domain.key?('isolation')
        if domain.key?('levelShifter')
          result['levelShifter'] = parse_level_shifter(
            domain['levelShifter'], "#{path}/levelShifter"
          )
        end
        result
      end

      def parse_domain_state(value, path)
        state = object!(value, path)
        exact_keys!(state, %w[id powerState groundState behavior], [], path)
        {
          'id' => string!(state['id'], "#{path}/id"),
          'powerState' => string!(state['powerState'], "#{path}/powerState"),
          'groundState' => string!(state['groundState'], "#{path}/groundState"),
          'behavior' => enum!(state['behavior'], %w[operational retained corrupt],
                              "#{path}/behavior")
        }
      end

      def parse_power_switch(value, path)
        power_switch = object!(value, path)
        exact_keys!(power_switch, %w[inputSupply outputSupply control onSense], [], path)
        {
          'inputSupply' => string!(power_switch['inputSupply'], "#{path}/inputSupply"),
          'outputSupply' => string!(power_switch['outputSupply'], "#{path}/outputSupply"),
          'control' => string!(power_switch['control'], "#{path}/control"),
          'onSense' => enum!(power_switch['onSense'], %w[high low], "#{path}/onSense")
        }
      end

      def parse_retention(value, path)
        retention = object!(value, path)
        exact_keys!(retention,
                    %w[
                      supply saveControl restoreControl saveEdge restoreEdge
                      location
                    ], [], path)
        {
          'supply' => string!(retention['supply'], "#{path}/supply"),
          'saveControl' => string!(retention['saveControl'], "#{path}/saveControl"),
          'restoreControl' => string!(retention['restoreControl'], "#{path}/restoreControl"),
          'saveEdge' => enum!(retention['saveEdge'], %w[posedge negedge],
                              "#{path}/saveEdge"),
          'restoreEdge' => enum!(retention['restoreEdge'],
                                 %w[posedge negedge],
                                 "#{path}/restoreEdge"),
          'location' => enum!(retention['location'], %w[self parent], "#{path}/location")
        }
      end

      def parse_isolation(value, path)
        isolation = object!(value, path)
        exact_keys!(isolation,
                    %w[control supply clampValue location], [], path)
        clamp_value = json_integer!(isolation['clampValue'],
                                    "#{path}/clampValue")
        {
          'control' => string!(isolation['control'], "#{path}/control"),
          'supply' => string!(isolation['supply'], "#{path}/supply"),
          'clampValue' => enum!(clamp_value, [0, 1],
                                "#{path}/clampValue"),
          'location' => enum!(isolation['location'], %w[self parent],
                              "#{path}/location")
        }
      end

      def parse_level_shifter(value, path)
        level_shifter = object!(value, path)
        exact_keys!(level_shifter, %w[location], [], path)
        {
          'location' => enum!(level_shifter['location'],
                              %w[self parent automatic],
                              "#{path}/location")
        }
      end

      def parse_system_state(value, path)
        state = object!(value, path)
        exact_keys!(state, %w[id domainStates], [], path)
        domain_states = parse_array(state['domainStates'], "#{path}/domainStates") do |entry, state_path|
          entry = object!(entry, state_path)
          exact_keys!(entry, %w[domain state], [], state_path)
          {
            'domain' => string!(entry['domain'], "#{state_path}/domain"),
            'state' => string!(entry['state'], "#{state_path}/state")
          }
        end
        unique_key!(domain_states, 'domain', "#{path}/domainStates",
                    'power_intent.duplicate_system_domain')
        {'id' => string!(state['id'], "#{path}/id"), 'domainStates' => domain_states}
      end

      def parse_technology(value, path)
        technology = object!(value, path)
        exact_keys!(technology, %w[profile interfaceCells], [], path)
        cells = parse_array(technology['interfaceCells'], "#{path}/interfaceCells") do |entry, cell_path|
          parse_interface_cell(entry, cell_path)
        end
        unique_ids!(cells, "#{path}/interfaceCells", 'power_intent.duplicate_interface_cell')
        {
          'profile' => enum!(technology['profile'],
                             %w[abstract upf-interface-cells], "#{path}/profile"),
          'interfaceCells' => cells
        }
      end

      def parse_interface_cell(value, path)
        cell = object!(value, path)
        exact_keys!(cell, %w[id kind cells], %w[direction], path)
        kind = enum!(cell['kind'],
                     %w[isolation level-shifter retention power-switch],
                     "#{path}/kind")
        cells = parse_array(cell['cells'], "#{path}/cells", nonempty: true) do |entry, cell_path|
          library_cell_name!(entry, cell_path)
        end
        expect!(cells.uniq.size == cells.size, 'power_intent.duplicate_cell',
                "#{path}/cells", 'cell names must be unique')
        if kind == 'level-shifter'
          expect!(cell.key?('direction'),
                  'power_intent.missing_cell_direction',
                  "#{path}/direction",
                  'a level-shifter mapping requires direction')
        else
          expect!(!cell.key?('direction'),
                  'power_intent.invalid_cell_direction',
                  "#{path}/direction",
                  'direction applies only to level-shifter mappings')
        end
        result = {
          'id' => string!(cell['id'], "#{path}/id"),
          'kind' => kind,
          'cells' => cells.sort
        }
        result['direction'] = enum!(cell['direction'], %w[up down], "#{path}/direction") if cell.key?('direction')
        result
      end

      def index_document!(intent)
        @supplies, @supply_paths = indexed(intent.fetch('supplies'), 'id', '/supplies',
                                           'power_intent.duplicate_supply')
        unique_values!(intent.fetch('supplies'), 'port', '/supplies',
                       'power_intent.duplicate_supply_port', optional: true)
        unique_values!(intent.fetch('supplies'), 'net', '/supplies',
                       'power_intent.duplicate_supply_net')
        @controls, @control_paths = indexed(intent.fetch('controls'), 'id', '/controls',
                                            'power_intent.duplicate_control')
        unique_values!(intent.fetch('controls'), 'signal', '/controls',
                       'power_intent.duplicate_control_signal')
        @domain_configs, @domain_paths = indexed(intent.fetch('domains'), 'domain', '/domains',
                                                 'power_intent.duplicate_domain')
        @system_states, @system_state_paths = indexed(
          intent.fetch('systemStates'), 'id', '/systemStates',
          'power_intent.duplicate_system_state'
        )
      end

      def index_plan_domains!
        @plan_domains = @context.domains_for_role(SUPPLY_ROLE).to_h do |domain|
          [domain.fetch('domain'), domain]
        end
        @plan_domain_paths = @context.plan.fetch('domainBindings').each_with_index.to_h do |domain, index|
          [domain.fetch('domain'), "/context/domainBindings/#{index}"]
        end
      end

      def index_crossing_requirements!
        @isolation_domains = {}
        @level_shifter_domains = {}
        @context.plan.fetch('edgeBindings').each do |edge|
          edge.fetch('stages').each do |stage|
            domains = [stage.fetch('fromDomain'), stage.fetch('toDomain')]
            register_crossing_domains!(
              stage, domains, @isolation_domains, @level_shifter_domains
            )
            stage.fetch('directions', []).each do |direction|
              register_crossing_domains!(
                direction, domains, @isolation_domains,
                @level_shifter_domains
              )
            end
          end
        end
      end

      def register_crossing_domains!(entry, domains, isolation, level_shifter)
        case entry['recipe']
        when ISOLATION_RECIPE
          domains.each { |domain| isolation[domain] = true }
        when LEVEL_SHIFTER_RECIPE
          domains.each { |domain| level_shifter[domain] = true }
        end
      end

      def validate_domain_coverage!
        extra = @domain_configs.keys - @plan_domains.keys
        unless extra.empty?
          id = extra.sort.first
          fail!('power_intent.unknown_domain', "#{@domain_paths.fetch(id)}/domain",
                "#{id} is not a supply-domain in the implementation plan")
        end
        missing = @plan_domains.keys - @domain_configs.keys
        expect!(missing.empty?, 'power_intent.missing_domain', '/domains',
                "missing supply-domain configuration #{missing.sort.first}")
      end

      def validate_control_owners!
        @controls.each do |id, control|
          next unless control.key?('ownerDomain')

          path = "#{@control_paths.fetch(id)}/ownerDomain"
          owner = @domain_configs[control.fetch('ownerDomain')]
          expect!(owner, 'power_intent.unknown_owner_domain', path,
                  'control ownerDomain is not a configured supply-domain')
          expect!(owner.fetch('mode') == 'always-on',
                  'power_intent.invalid_control_owner', path,
                  'control ownerDomain must be always-on')
        end
      end

      def compile_domain(domain)
        id = domain.fetch('domain')
        path = @domain_paths.fetch(id)
        plan_domain = @plan_domains.fetch(id)
        power = supply_ref!(domain.fetch('primaryPower'), 'power',
                            "#{path}/primaryPower")
        ground = supply_ref!(domain.fetch('primaryGround'), 'ground',
                             "#{path}/primaryGround")
        if domain.fetch('mode') == 'always-on'
          expect!(power.fetch('exposure') == 'external-port',
                  'power_intent.invalid_always_on_primary_supply',
                  "#{path}/primaryPower",
                  'an always-on Domain cannot use an internal switched supply')
        end
        nominal_voltage = nominal_voltage!(plan_domain)
        power_states = power.fetch('states').to_h do |state|
          [state.fetch('id'), state]
        end
        ground_states = ground.fetch('states').to_h do |state|
          [state.fetch('id'), state]
        end
        state_index = domain.fetch('states').to_h { |state| [state.fetch('id'), state] }
        resolved_states = []
        domain.fetch('states').each_with_index do |state, index|
          power_state = power_states[state.fetch('powerState')]
          ground_state = ground_states[state.fetch('groundState')]
          expect!(power_state,
                  'power_intent.unknown_power_state',
                  "#{path}/states/#{index}/powerState",
                  'powerState is absent from the primary power supply')
          expect!(ground_state,
                  'power_intent.unknown_ground_state',
                  "#{path}/states/#{index}/groundState",
                  'groundState is absent from the primary ground supply')
          validate_state_behavior!(domain, state, power_state, ground_state,
                                   nominal_voltage, "#{path}/states/#{index}")
          resolved_states << [state, power_state]
        end
        default_state = state_index[domain.fetch('defaultState')]
        expect!(default_state, 'power_intent.unknown_default_state',
                "#{path}/defaultState", 'defaultState is not declared by the Domain')
        expect!(default_state.fetch('behavior') == 'operational',
                'power_intent.invalid_default_state', "#{path}/defaultState",
                'defaultState must be operational')
        if domain.fetch('mode') == 'switchable'
          has_off_state = resolved_states.any? do |_state, supply_state|
            supply_state.fetch('condition') == 'off'
          end
          expect!(has_off_state, 'power_intent.missing_off_state',
                  "#{path}/states",
                  'a switchable Domain requires at least one powered-off state')
        end
        validate_switch!(domain, path)
        validate_retention!(domain, plan_domain, path)
        validate_isolation!(domain, path)
        validate_level_shifter!(domain, path)

        domain.merge(
          'name' => plan_domain.fetch('name'),
          'token' => plan_domain.fetch('token'),
          'active' => !plan_domain.fetch('members').empty?,
          'members' => plan_domain.fetch('members').sort_by do |member|
            [member.fetch('kind'), member.fetch('id')]
          end,
          'parameters' => plan_domain.fetch('parameters'),
          'states' => sort_by_id(domain.fetch('states'))
        )
      end

      def nominal_voltage!(plan_domain)
        path = "#{@plan_domain_paths.fetch(plan_domain.fetch('domain'))}/parameters/nominal-voltage-mv"
        parameter = plan_domain.fetch('parameters')['nominal-voltage-mv']
        expect!(parameter, 'power_intent.missing_nominal_voltage_binding', path,
                'supply-domain requires typed nominal-voltage-mv binding')
        expect!(parameter.fetch('type') == 'integer' &&
                  parameter.fetch('value').is_a?(Integer) &&
                  parameter.fetch('value').positive?,
                'power_intent.invalid_nominal_voltage_binding', path,
                'nominal-voltage-mv must be a positive typed integer')
        parameter.fetch('value')
      end

      def validate_state_behavior!(domain, state, power_state, ground_state,
                                   nominal_voltage, path)
        behavior = state.fetch('behavior')
        if domain.fetch('mode') == 'always-on'
          expect!(behavior == 'operational',
                  'power_intent.invalid_always_on_state', "#{path}/behavior",
                  'an always-on Domain permits only operational states')
        end
        expected_power = behavior == 'operational' ? 'full-on' : 'off'
        expect!(power_state.fetch('condition') == expected_power,
                'power_intent.invalid_state_power_condition',
                "#{path}/powerState",
                "#{behavior} behavior requires a #{expected_power} power state")
        expect!(ground_state.fetch('condition') == 'full-on',
                'power_intent.invalid_state_ground_condition',
                "#{path}/groundState",
                "#{behavior} behavior requires a full-on ground state")
        return unless behavior == 'operational'

        expect!(power_state.fetch('voltageMv') == nominal_voltage,
                'power_intent.nominal_voltage_mismatch',
                "#{path}/powerState",
                'operational power state voltage differs from nominal-voltage-mv')
      end

      def validate_switch!(domain, path)
        power_switch = domain['powerSwitch']
        if domain.fetch('mode') == 'switchable'
          expect!(power_switch, 'power_intent.missing_power_switch',
                  "#{path}/powerSwitch", 'a switchable Domain requires powerSwitch')
        else
          expect!(!power_switch, 'power_intent.unexpected_power_switch',
                  "#{path}/powerSwitch", 'an always-on Domain forbids powerSwitch')
          return
        end
        input_supply = supply_ref!(power_switch.fetch('inputSupply'), 'power',
                                   "#{path}/powerSwitch/inputSupply")
        supply_ref!(power_switch.fetch('outputSupply'), 'power',
                    "#{path}/powerSwitch/outputSupply")
        expect!(power_switch.fetch('inputSupply') != power_switch.fetch('outputSupply'),
                'power_intent.identical_switch_supplies',
                "#{path}/powerSwitch/inputSupply",
                'powerSwitch inputSupply and outputSupply must differ')
        input_can_turn_off = input_supply.fetch('states').any? do |state|
          state.fetch('condition') == 'off'
        end
        expect!(!input_can_turn_off,
                'power_intent.switch_input_can_turn_off',
                "#{path}/powerSwitch/inputSupply",
                'powerSwitch inputSupply must not declare an off state')
        expect!(power_switch.fetch('outputSupply') == domain.fetch('primaryPower'),
                'power_intent.invalid_switch_output',
                "#{path}/powerSwitch/outputSupply",
                'powerSwitch outputSupply must equal primaryPower')
        control = control_ref!(power_switch.fetch('control'),
                               "#{path}/powerSwitch/control")
        expect!(power_switch.fetch('onSense') == control.fetch('activeSense'),
                'power_intent.switch_sense_mismatch',
                "#{path}/powerSwitch/onSense",
                'powerSwitch onSense must match the referenced control activeSense')
      end

      def validate_retention!(domain, plan_domain, path)
        parameter_path = "#{@plan_domain_paths.fetch(domain.fetch('domain'))}/parameters/retains-state"
        parameter = plan_domain.fetch('parameters')['retains-state']
        expect!(parameter, 'power_intent.missing_retention_binding', parameter_path,
                'supply-domain requires typed retains-state binding')
        expect!(parameter.fetch('type') == 'boolean',
                'power_intent.invalid_retention_binding', parameter_path,
                'retains-state binding must be boolean')
        retention = domain['retention']
        retained_state = domain.fetch('states').any? do |state|
          state.fetch('behavior') == 'retained'
        end
        if parameter.fetch('value')
          expect!(retained_state, 'power_intent.missing_retained_state',
                  "#{path}/states",
                  'retains-state true requires at least one retained state')
          expect!(retention, 'power_intent.missing_retention', "#{path}/retention",
                  'retains-state true requires retention configuration')
        else
          expect!(!retained_state, 'power_intent.unexpected_retained_state',
                  "#{path}/states",
                  'retains-state false forbids retained states')
          expect!(!retention, 'power_intent.unexpected_retention', "#{path}/retention",
                  'retains-state false forbids retention configuration')
          return
        end
        retention_supply = supply_ref!(retention.fetch('supply'), 'power',
                                       "#{path}/retention/supply")
        retention_can_turn_off = retention_supply.fetch('states').any? do |state|
          state.fetch('condition') == 'off'
        end
        expect!(!retention_can_turn_off,
                'power_intent.retention_supply_can_turn_off',
                "#{path}/retention/supply",
                'retention supply must not declare an off state')
        control_ref!(retention.fetch('saveControl'), "#{path}/retention/saveControl")
        control_ref!(retention.fetch('restoreControl'), "#{path}/retention/restoreControl")
      end

      def validate_isolation!(domain, path)
        isolation = domain['isolation']
        required = @isolation_domains.key?(domain.fetch('domain')) &&
                   domain.fetch('mode') == 'switchable'
        if required
          expect!(isolation, 'power_intent.missing_isolation_configuration',
                  "#{path}/isolation",
                  'a switchable Domain on an isolation boundary requires isolation configuration')
        end
        return unless isolation

        expect!(domain.fetch('mode') == 'switchable',
                'power_intent.unexpected_isolation_configuration',
                "#{path}/isolation",
                'an always-on Domain does not accept isolation configuration')
        supply = supply_ref!(isolation.fetch('supply'), 'power',
                             "#{path}/isolation/supply")
        can_turn_off = supply.fetch('states').any? do |state|
          state.fetch('condition') == 'off'
        end
        expect!(!can_turn_off,
                'power_intent.isolation_supply_can_turn_off',
                "#{path}/isolation/supply",
                'isolation supply must not declare an off state')
        control_ref!(isolation.fetch('control'),
                     "#{path}/isolation/control")
      end

      def validate_level_shifter!(domain, path)
        return unless @level_shifter_domains.key?(domain.fetch('domain'))

        expect!(domain['levelShifter'],
                'power_intent.missing_level_shifter_configuration',
                "#{path}/levelShifter",
                'a Domain on a level-shifter boundary requires placement configuration')
      end

      def validate_supply_topology!(domains)
        switch_outputs = Hash.new { |result, supply| result[supply] = [] }
        domains.each do |domain|
          power_switch = domain['powerSwitch']
          next unless power_switch

          switch_outputs[power_switch.fetch('outputSupply')] <<
            domain.fetch('domain')
        end
        @supplies.each do |id, supply|
          owners = switch_outputs.fetch(id, [])
          path = @supply_paths.fetch(id)
          if supply.fetch('exposure') == 'internal-switched'
            expect!(owners.size == 1,
                    'power_intent.invalid_internal_supply_driver',
                    "#{path}/exposure",
                    'an internal switched supply requires exactly one powerSwitch output')
          else
            expect!(owners.empty?,
                    'power_intent.externally_driven_switch_output',
                    "#{path}/exposure",
                    'a powerSwitch output must be an internal switched supply')
          end
        end
      end

      def validate_system_states!(states, default_system_state)
        active_ids = @plan_domains.values.select do |domain|
          !domain.fetch('members').empty?
        end.map { |domain| domain.fetch('domain') }.sort
        expect!(!states.empty?,
                'power_intent.missing_system_states', '/systemStates',
                'defaultSystemState requires at least one system state')
        used_states = active_ids.to_h { |id| [id, {}] }
        states.each_with_index do |system_state, index|
          path = "/systemStates/#{index}/domainStates"
          entries = system_state.fetch('domainStates')
          actual = entries.map { |entry| entry.fetch('domain') }.sort
          expect!(actual == active_ids, 'power_intent.incomplete_system_state', path,
                  'system state must cover every active supply-domain exactly once')
          entries.each_with_index do |entry, entry_index|
            domain = @domain_configs.fetch(entry.fetch('domain'))
            known = domain.fetch('states').any? do |state|
              state.fetch('id') == entry.fetch('state')
            end
            expect!(known, 'power_intent.unknown_domain_state',
                    "#{path}/#{entry_index}/state",
                    'system state references an unknown Domain state')
            used_states.fetch(entry.fetch('domain'))[entry.fetch('state')] = true
          end
        end
        default = @system_states[default_system_state]
        expect!(default, 'power_intent.unknown_default_system_state',
                '/defaultSystemState',
                'defaultSystemState does not name a declared system state')
        expected_vector = active_ids.to_h do |id|
          [id, @domain_configs.fetch(id).fetch('defaultState')]
        end
        actual_vector = default.fetch('domainStates').to_h do |entry|
          [entry.fetch('domain'), entry.fetch('state')]
        end
        expect!(actual_vector == expected_vector,
                'power_intent.default_system_state_mismatch',
                '/defaultSystemState',
                'default system state must select every active Domain defaultState')
        active_ids.each do |id|
          @domain_configs.fetch(id).fetch('states').each_with_index do |state, index|
            expect!(used_states.fetch(id).key?(state.fetch('id')),
                    'power_intent.unreachable_domain_state',
                    "#{@domain_paths.fetch(id)}/states/#{index}",
                    'active Domain state is not referenced by any system state')
          end
        end
      end

      def validate_technology!(technology)
        return unless technology

        cells = technology.fetch('interfaceCells')
        if technology.fetch('profile') == 'abstract'
          expect!(cells.empty?, 'power_intent.abstract_profile_has_cells',
                  '/technology/interfaceCells',
                  'abstract technology profile must not bind interface cells')
          return
        else
          expect!(!cells.empty?, 'power_intent.missing_interface_cells',
                  '/technology/interfaceCells',
                  'upf-interface-cells profile requires interface cells')
        end
        cells.each_with_index do |cell, index|
          next unless cell.key?('direction')

          expect!(cell.fetch('kind') == 'level-shifter',
                  'power_intent.invalid_cell_direction',
                  "/technology/interfaceCells/#{index}/direction",
                  'direction applies only to level-shifter cells')
        end
        unique_technology_mappings!(cells)
        requirements = technology_requirements
        require_cell_kind!(cells, 'isolation') if requirements.fetch('isolation')
        require_cell_kind!(cells, 'retention') if requirements.fetch('retention')
        require_cell_kind!(cells, 'power-switch') if requirements.fetch('powerSwitch')
        requirements.fetch('levelShifterDirections').each do |direction|
          covered = cells.one? do |cell|
            cell.fetch('kind') == 'level-shifter' &&
              cell['direction'] == direction
          end
          expect!(covered, 'power_intent.missing_technology_mapping',
                  '/technology/interfaceCells',
                  "missing level-shifter #{direction} technology mapping")
        end
      end

      def technology_requirements
        requirements = {
          'isolation' => false,
          'retention' => @plan_domains.values.any? do |domain|
            !domain.fetch('members').empty? &&
              domain.dig('parameters', 'retains-state', 'value') == true
          end,
          'powerSwitch' => @domain_configs.any? do |id, domain|
            !@plan_domains.fetch(id).fetch('members').empty? &&
              domain.fetch('mode') == 'switchable'
          end,
          'levelShifterDirections' => []
        }
        @context.plan.fetch('edgeBindings').each_with_index do |edge, edge_index|
          edge.fetch('stages').each_with_index do |stage, stage_index|
            stage_path = "/context/edgeBindings/#{edge_index}/stages/#{stage_index}"
            register_technology_recipe!(stage, stage_path, requirements)
            stage.fetch('directions', []).each_with_index do |direction, index|
              register_technology_recipe!(
                direction, "#{stage_path}/directions/#{index}", requirements
              )
            end
          end
        end
        requirements['levelShifterDirections'] =
          requirements.fetch('levelShifterDirections').uniq.sort
        requirements
      end

      def register_technology_recipe!(entry, path, requirements)
        case entry['recipe']
        when ISOLATION_RECIPE
          requirements['isolation'] = true
        when LEVEL_SHIFTER_RECIPE
          parameter = entry.fetch('parameters', {})['translation-direction']
          valid = parameter && parameter['type'] == 'enum' &&
                  %w[up down].include?(parameter['value'])
          expect!(valid, 'power_intent.invalid_level_shifter_recipe', path,
                  'power-level-shifter requires typed up/down translation-direction')
          requirements.fetch('levelShifterDirections') << parameter.fetch('value')
        end
      end

      def require_cell_kind!(cells, kind)
        expect!(cells.one? { |cell| cell.fetch('kind') == kind },
                'power_intent.missing_technology_mapping',
                '/technology/interfaceCells',
                "expected exactly one #{kind} technology mapping")
      end

      def unique_technology_mappings!(cells)
        seen = {}
        cells.each_with_index do |cell, index|
          key = [cell.fetch('kind'), cell['direction']]
          expect!(!seen.key?(key),
                  'power_intent.duplicate_technology_mapping',
                  "/technology/interfaceCells/#{index}",
                  "duplicate technology mapping for #{key.compact.join(' ')}")
          seen[key] = true
        end
      end

      def supply_ref!(id, expected_kind, path)
        supply = @supplies[id]
        expect!(supply, 'power_intent.unknown_supply', path,
                "unknown supply #{id}")
        expect!(supply.fetch('kind') == expected_kind,
                'power_intent.invalid_supply_kind', path,
                "supply #{id} must be #{expected_kind}")
        supply
      end

      def control_ref!(id, path)
        control = @controls[id]
        expect!(control, 'power_intent.unknown_control', path,
                "unknown control #{id}")
        control
      end

      def canonical_technology(technology)
        technology.merge(
          'interfaceCells' => technology.fetch('interfaceCells').sort_by do |cell|
            cell.fetch('id')
          end
        )
      end

      def indexed(values, key, path, duplicate_code)
        result = {}
        paths = {}
        values.each_with_index do |value, index|
          id = value.fetch(key)
          entry_path = "#{path}/#{index}"
          expect!(!result.key?(id), duplicate_code, "#{entry_path}/#{key}",
                  "duplicate #{key} #{id}")
          result[id] = value
          paths[id] = entry_path
        end
        [result, paths]
      end

      def unique_ids!(values, path, code)
        unique_key!(values, 'id', path, code)
      end

      def unique_key!(values, key, path, code)
        seen = {}
        values.each_with_index do |value, index|
          id = value.fetch(key)
          expect!(!seen.key?(id), code, "#{path}/#{index}/#{key}",
                  "duplicate #{key} #{id}")
          seen[id] = true
        end
      end

      def unique_values!(values, key, path, code, optional: false)
        seen = {}
        values.each_with_index do |value, index|
          next if optional && !value.key?(key)

          entry = value.fetch(key)
          expect!(!seen.key?(entry), code, "#{path}/#{index}/#{key}",
                  "duplicate #{key} #{entry}")
          seen[entry] = true
        end
      end

      def parse_array(value, path, nonempty: false)
        values = array!(value, path)
        expect!(!nonempty || !values.empty?, 'power_intent.empty_array', path,
                'array must not be empty')
        values.each_with_index.map do |entry, index|
          yield(entry, "#{path}/#{index}")
        end
      end

      def sort_by_id(values)
        values.sort_by { |value| value.fetch('id') }
      end

      def exact_keys!(object, required, optional, path)
        allowed = required + optional
        unknown = object.keys - allowed
        missing = required - object.keys
        fail!('power_intent.unknown_field', pointer(path, unknown.first),
              "unknown field #{unknown.first}") unless unknown.empty?
        fail!('power_intent.missing_field', pointer(path, missing.first),
              "missing field #{missing.first}") unless missing.empty?
      end

      def pointer(path, token)
        escaped = token.to_s.gsub('~', '~0').gsub('/', '~1')
        "#{path == '/' ? '' : path}/#{escaped}"
      end

      def object!(value, path)
        expect!(value.is_a?(Hash), 'power_intent.expected_object', path,
                'expected an object')
        value
      end

      def array!(value, path)
        expect!(value.is_a?(Array), 'power_intent.expected_array', path,
                'expected an array')
        value
      end

      def string!(value, path)
        expect!(value.is_a?(String) && value.match?(/\S/) &&
                  !value.match?(CONTROL_CHARACTERS),
                'power_intent.expected_string', path,
                'expected a non-empty string without control characters')
        value
      end

      def hdl_identifier!(value, path)
        value = string!(value, path)
        expect!(value.match?(HDL_IDENTIFIER),
                'power_intent.invalid_hdl_identifier', path,
                'expected a simple HDL identifier')
        value
      end

      def library_cell_name!(value, path)
        value = string!(value, path)
        expect!(value.match?(/\A[A-Za-z_][A-Za-z0-9_$.:\/-]*\z/),
                'power_intent.invalid_library_cell', path,
                'expected a Tcl-safe library cell name')
        value
      end

      def integer!(value, path)
        expect!(value.is_a?(Integer), 'power_intent.expected_integer', path,
                'expected an integer')
        value
      end

      def json_integer!(value, path)
        valid_float = value.is_a?(Float) && value.finite? &&
                      value == value.to_i
        expect!(value.is_a?(Integer) || valid_float,
                'power_intent.expected_integer', path,
                'expected an integral JSON number')
        value.to_i
      end

      def enum!(value, values, path)
        expect!(values.include?(value), 'power_intent.invalid_enum', path,
                "expected one of #{values.join(', ')}")
        value
      end

      def canonicalize(value)
        case value
        when Hash
          value.keys.sort.to_h do |key|
            [canonicalize(key), canonicalize(value.fetch(key))]
          end
        when Array then value.map { |entry| canonicalize(entry) }
        when String then value.dup
        else value
        end
      end

      def deep_freeze!(value)
        case value
        when Hash
          value.each { |key, child| deep_freeze!(key); deep_freeze!(child) }
        when Array
          value.each { |child| deep_freeze!(child) }
        end
        value.freeze
      end

      def expect!(condition, code, path, message)
        fail!(code, path, message) unless condition
        condition
      end

      def fail!(code, path, message)
        raise Error.new(code, path, message)
      end
    end
  end
end
