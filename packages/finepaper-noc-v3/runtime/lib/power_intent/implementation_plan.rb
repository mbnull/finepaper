# frozen_string_literal: true

require 'digest'
require 'json'
require_relative '../domain_rtl_context'

module FinepaperNoc
  module PowerIntent
    class PowerImplementationPlanError < StandardError
      attr_reader :code, :path

      def initialize(code, path, message)
        @code = code
        @path = path
        super("#{message} at #{path}")
      end
    end

    # Joins the typed Domain plan, Package-owned power intent, and emitted RTL
    # hierarchy into a deterministic renderer contract. It records only
    # evidence present in those inputs; it never invents low-power controls,
    # placement, bridge ownership, shutdown sequencing, or routing proofs.
    class ImplementationPlanBuilder
      FORMAT = 'finepaper.noc-power-implementation-plan'
      VERSION = 1
      POWER_PLAN_FORMAT = 'finepaper.noc-power-intent-plan'
      POWER_PLAN_VERSION = 1
      POWER_SOURCE_FORMAT = 'finepaper.noc-power-intent'
      POWER_SOURCE_VERSION = 1
      HIERARCHY_FORMAT = 'finepaper.noc-rtl-hierarchy'
      HIERARCHY_VERSION = 1
      DOMAIN_PLAN_FORMAT = DomainRtlContext::PLAN_FORMAT
      DOMAIN_PLAN_VERSION = DomainRtlContext::PLAN_VERSION
      SUPPLY_ROLE = 'supply-domain'
      ORIENTATIONS = DomainRtlContext::ORIENTATIONS
      SIGNAL_TYPES = %w[payload valid ready].freeze
      STATUSES = %w[expected not-required deferred].freeze
      EDGE_ORDER = {'router-link' => 0, 'endpoint-attachment' => 1}.freeze

      def self.build(context:, power_plan:, hierarchy:)
        new(context: context, power_plan: power_plan, hierarchy: hierarchy).build
      end

      def initialize(context:, power_plan:, hierarchy:)
        @context = context
        @power_plan = power_plan
        @hierarchy = hierarchy
      end

      def build
        validate_context!
        index_power_plan!
        index_hierarchy!
        validate_cross_contracts!

        domains, inactive_domains = compile_domains
        active_supply_ids, active_control_ids = active_resource_ids(domains)
        supplies, inactive_supplies = compile_supplies(active_supply_ids)
        controls, inactive_controls = compile_controls(active_control_ids)
        switches = compile_power_switches(domains)
        retentions = compile_retentions(domains)
        edge_orientations = compile_edge_orientations
        system_states = compile_system_states
        technology = compile_technology
        coverage_items = compile_coverage(
          supplies, inactive_supplies, controls, inactive_controls, domains,
          inactive_domains, switches, retentions, edge_orientations
        )
        summary = STATUSES.to_h do |status|
          key = status == 'not-required' ? 'notRequired' : status
          [key, coverage_items.count { |item| item.fetch('status') == status }]
        end
        summary['total'] = coverage_items.size

        result = {
          'format' => FORMAT,
          'formatVersion' => VERSION,
          'design' => @context.plan.fetch('design'),
          'topModule' => @hierarchy.fetch('topModule'),
          'sourceContracts' => {
            'domainImplementationPlan' => header(@context.plan),
            'powerIntentPlan' => header(@power_plan),
            'rtlHierarchy' => header(@hierarchy)
          },
          'hierarchyFacts' => {
            'format' => HIERARCHY_FORMAT,
            'formatVersion' => HIERARCHY_VERSION,
            'topArtifact' => @hierarchy.fetch('topArtifact'),
            'resetSynchronizers' => @hierarchy.fetch('resetSynchronizers')
              .sort_by { |entry| [entry.fetch('timingDomain'), entry.fetch('instance')] },
            'logicControlPorts' => @hierarchy.fetch('logicControlPorts')
              .sort_by { |entry| [entry.fetch('id'), entry.fetch('signal')] }
          },
          'technology' => technology,
          'supplies' => supplies,
          'controls' => controls,
          'domains' => domains,
          'powerSwitches' => switches,
          'retentions' => retentions,
          'defaultSystemState' => @power_plan.fetch('defaultSystemState'),
          'systemStates' => system_states,
          'edgeOrientations' => edge_orientations,
          'inactiveIntent' => {
            'domains' => inactive_domains.map { |domain| domain.fetch('domain') },
            'supplies' => inactive_supplies,
            'controls' => inactive_controls
          },
          'coverage' => {
            'complete' => summary.fetch('deferred').zero?,
            'summary' => summary,
            'items' => coverage_items
          }
        }
        canonical = canonicalize(result)
        deep_freeze!(canonical)
      end

      private

      def validate_context!
        expect!(@context.is_a?(DomainRtlContext),
                'power_implementation.invalid_context', '/context',
                'context must be a DomainRtlContext')
        expect!(@context.plan.fetch('format') == DOMAIN_PLAN_FORMAT &&
                  @context.plan.fetch('formatVersion') == DOMAIN_PLAN_VERSION,
                'power_implementation.invalid_context', '/context',
                'unsupported Domain implementation plan')
      end

      def index_power_plan!
        plan = object!(@power_plan, '/powerPlan')
        exact_keys!(plan,
                    %w[
                      format formatVersion design source implementationPlan
                      supplies controls domains defaultSystemState systemStates
                    ], %w[technology], '/powerPlan')
        expect!(plan.fetch('format') == POWER_PLAN_FORMAT,
                'power_implementation.invalid_power_plan_format',
                '/powerPlan/format', "expected #{POWER_PLAN_FORMAT}")
        expect!(plan.fetch('formatVersion') == POWER_PLAN_VERSION,
                'power_implementation.invalid_power_plan_version',
                '/powerPlan/formatVersion', 'unsupported power plan version')
        validate_header!(plan.fetch('source'), POWER_SOURCE_FORMAT,
                         POWER_SOURCE_VERSION, '/powerPlan/source')
        validate_header!(plan.fetch('implementationPlan'), DOMAIN_PLAN_FORMAT,
                         DOMAIN_PLAN_VERSION, '/powerPlan/implementationPlan')
        string!(plan.fetch('design'), '/powerPlan/design')
        string!(plan.fetch('defaultSystemState'),
                '/powerPlan/defaultSystemState')

        @supplies = index_records!(
          array!(plan.fetch('supplies'), '/powerPlan/supplies'), 'id',
          '/powerPlan/supplies', 'power_implementation.duplicate_supply'
        )
        @controls = index_records!(
          array!(plan.fetch('controls'), '/powerPlan/controls'), 'id',
          '/powerPlan/controls', 'power_implementation.duplicate_control'
        )
        @power_domains = index_records!(
          array!(plan.fetch('domains'), '/powerPlan/domains'), 'domain',
          '/powerPlan/domains', 'power_implementation.duplicate_domain'
        )
        @system_states = index_records!(
          array!(plan.fetch('systemStates'), '/powerPlan/systemStates'), 'id',
          '/powerPlan/systemStates',
          'power_implementation.duplicate_system_state'
        )
        validate_power_records!
        index_technology!
      end

      def validate_power_records!
        @supplies.each_value.with_index do |supply, index|
          path = "/powerPlan/supplies/#{index}"
          exact_keys!(supply, %w[id kind exposure net states], %w[port], path)
          enum!(supply.fetch('kind'), %w[power ground], "#{path}/kind")
          exposure = enum!(supply.fetch('exposure'),
                           %w[external-port internal-switched],
                           "#{path}/exposure")
          if exposure == 'external-port'
            string!(supply['port'], "#{path}/port")
          else
            expect!(!supply.key?('port'),
                    'power_implementation.unexpected_supply_port',
                    "#{path}/port", 'internal switched supply cannot have a port')
          end
          string!(supply.fetch('net'), "#{path}/net")
          array!(supply.fetch('states'), "#{path}/states")
        end
        @controls.each_value.with_index do |control, index|
          path = "/powerPlan/controls/#{index}"
          exact_keys!(control, %w[id signal source activeSense],
                      %w[ownerDomain], path)
          string!(control.fetch('signal'), "#{path}/signal")
          enum!(control.fetch('source'), %w[top-port upf-port],
                "#{path}/source")
          enum!(control.fetch('activeSense'), %w[high low],
                "#{path}/activeSense")
        end
        @power_domains.each_value.with_index do |domain, index|
          path = "/powerPlan/domains/#{index}"
          exact_keys!(domain,
                      %w[
                        domain primaryPower primaryGround mode defaultState
                        states name token active members parameters
                      ], %w[powerSwitch retention isolation levelShifter], path)
          enum!(domain.fetch('mode'), %w[always-on switchable], "#{path}/mode")
          boolean!(domain.fetch('active'), "#{path}/active")
          array!(domain.fetch('members'), "#{path}/members")
          array!(domain.fetch('states'), "#{path}/states")
        end
      end

      def index_technology!
        @technology = @power_plan['technology'] || {
          'profile' => 'abstract', 'interfaceCells' => []
        }
        exact_keys!(@technology, %w[profile interfaceCells], [],
                    '/powerPlan/technology')
        enum!(@technology.fetch('profile'),
              %w[abstract upf-interface-cells],
              '/powerPlan/technology/profile')
        @technology_mappings = {}
        array!(@technology.fetch('interfaceCells'),
               '/powerPlan/technology/interfaceCells').each_with_index do |cell, index|
          path = "/powerPlan/technology/interfaceCells/#{index}"
          exact_keys!(cell, %w[id kind cells], %w[direction], path)
          key = [cell.fetch('kind'), cell['direction']]
          expect!(!@technology_mappings.key?(key),
                  'power_implementation.duplicate_technology_mapping', path,
                  'technology mapping is ambiguous')
          @technology_mappings[key] = cell
        end
      end

      def index_hierarchy!
        hierarchy = object!(@hierarchy, '/hierarchy')
        exact_keys!(hierarchy,
                    %w[
                      format formatVersion design topModule topArtifact
                      elements resetSynchronizers logicControlPorts edgeDirections
                    ], [], '/hierarchy')
        expect!(hierarchy.fetch('format') == HIERARCHY_FORMAT,
                'power_implementation.invalid_hierarchy_format',
                '/hierarchy/format', "expected #{HIERARCHY_FORMAT}")
        expect!(hierarchy.fetch('formatVersion') == HIERARCHY_VERSION,
                'power_implementation.invalid_hierarchy_version',
                '/hierarchy/formatVersion', 'unsupported hierarchy version')
        string!(hierarchy.fetch('design'), '/hierarchy/design')
        string!(hierarchy.fetch('topModule'), '/hierarchy/topModule')
        string!(hierarchy.fetch('topArtifact'), '/hierarchy/topArtifact')
        array!(hierarchy.fetch('resetSynchronizers'),
               '/hierarchy/resetSynchronizers')
        @logic_control_ports = index_records!(
          array!(hierarchy.fetch('logicControlPorts'),
                 '/hierarchy/logicControlPorts'), 'id',
          '/hierarchy/logicControlPorts',
          'power_implementation.duplicate_logic_control_port'
        )
        @hierarchy_elements = {}
        @instances = {}
        array!(hierarchy.fetch('elements'), '/hierarchy/elements')
          .each_with_index do |record, index|
          path = "/hierarchy/elements/#{index}"
          exact_keys!(record,
                      %w[element module instance clockDomain supplyDomain], [], path)
          reference = reference!(record.fetch('element'), "#{path}/element")
          key = reference_key(reference)
          expect!(!@hierarchy_elements.key?(key),
                  'power_implementation.duplicate_hierarchy_element', path,
                  'hierarchy element is duplicated')
          instance = string!(record.fetch('instance'), "#{path}/instance")
          expect!(!@instances.key?(instance),
                  'power_implementation.duplicate_instance', "#{path}/instance",
                  'hierarchy instance is duplicated')
          @hierarchy_elements[key] = record
          @instances[instance] = record
        end
        @hierarchy_directions = {}
        array!(hierarchy.fetch('edgeDirections'), '/hierarchy/edgeDirections')
          .each_with_index do |record, index|
          index_hierarchy_direction!(record, index)
        end
      end

      def index_hierarchy_direction!(record, index)
        path = "/hierarchy/edgeDirections/#{index}"
        exact_keys!(record,
                    %w[
                      edge orientation producer consumer producerInstance
                      consumerInstance producerPins consumerPins sourceBundle
                      destinationBundle sourceClockDomain destinationClockDomain
                      sourceSupplyDomain destinationSupplyDomain bridge
                      signalFlows powerBoundary
                    ], [], path)
        edge = reference!(record.fetch('edge'), "#{path}/edge")
        orientation = enum!(record.fetch('orientation'), ORIENTATIONS,
                            "#{path}/orientation")
        key = reference_key(edge) + [orientation]
        expect!(!@hierarchy_directions.key?(key),
                'power_implementation.duplicate_edge_orientation', path,
                'hierarchy edge orientation is duplicated')
        reference!(record.fetch('producer'), "#{path}/producer")
        reference!(record.fetch('consumer'), "#{path}/consumer")
        array!(record.fetch('signalFlows'), "#{path}/signalFlows")
          .each_with_index do |flow, flow_index|
          validate_signal_flow_shape!(flow, "#{path}/signalFlows/#{flow_index}")
        end
        @hierarchy_directions[key] = record
      end

      def validate_signal_flow_shape!(flow, path)
        exact_keys!(flow, %w[type direction side signal driver receiver], [], path)
        enum!(flow.fetch('type'), SIGNAL_TYPES, "#{path}/type")
        enum!(flow.fetch('direction'),
              %w[producer-to-consumer consumer-to-producer],
              "#{path}/direction")
        enum!(flow.fetch('side'), %w[direct source destination], "#{path}/side")
        string!(flow.fetch('signal'), "#{path}/signal")
        %w[driver receiver].each do |terminal|
          value = object!(flow.fetch(terminal), "#{path}/#{terminal}")
          exact_keys!(value, %w[instance pin], [], "#{path}/#{terminal}")
          string!(value.fetch('instance'), "#{path}/#{terminal}/instance")
          string!(value.fetch('pin'), "#{path}/#{terminal}/pin")
        end
      end

      def validate_cross_contracts!
        design = @context.plan.fetch('design')
        expect!(@power_plan.fetch('design') == design,
                'power_implementation.design_mismatch', '/powerPlan/design',
                'power plan design differs from Domain plan')
        expect!(@hierarchy.fetch('design') == design,
                'power_implementation.design_mismatch', '/hierarchy/design',
                'hierarchy design differs from Domain plan')
        validate_domain_bijection!
        validate_element_bijection!
        validate_control_port_bijection!
        validate_edge_bijection!
      end

      def validate_domain_bijection!
        planned = @context.domains_for_role(SUPPLY_ROLE).to_h do |domain|
          [domain.fetch('domain'), domain]
        end
        expect!(@power_domains.keys.sort == planned.keys.sort,
                'power_implementation.domain_bijection_mismatch',
                '/powerPlan/domains',
                'power plan Domains are not a bijection with supply Domains')
        @power_domains.each do |id, domain|
          source = planned.fetch(id)
          expected_members = source.fetch('members').sort_by { |m| reference_key(m) }
          actual_members = domain.fetch('members').sort_by { |m| reference_key(m) }
          expect!(actual_members == expected_members &&
                    domain.fetch('name') == source.fetch('name') &&
                    domain.fetch('token') == source.fetch('token') &&
                    domain.fetch('parameters') == source.fetch('parameters') &&
                    domain.fetch('active') == !expected_members.empty?,
                  'power_implementation.domain_fact_mismatch',
                  pointer('/powerPlan/domains', id),
                  'compiled Domain facts differ from Domain implementation plan')
        end
      end

      def validate_element_bijection!
        expect!(@hierarchy_elements.keys.sort == @context.entities.keys.sort,
                'power_implementation.element_bijection_mismatch',
                '/hierarchy/elements',
                'hierarchy elements are not a bijection with Domain entities')
        @hierarchy_elements.each do |key, record|
          supply = @context.entity_domain(*key, SUPPLY_ROLE).fetch('domain')
          expect!(record.fetch('supplyDomain') == supply,
                  'power_implementation.element_supply_mismatch',
                  '/hierarchy/elements',
                  'hierarchy element supply Domain differs from Domain plan')
          domain = @power_domains.fetch(supply)
          expect!(domain.fetch('members').any? do |member|
                    reference_key(member) == key
                  end,
                  'power_implementation.element_member_mismatch',
                  '/hierarchy/elements',
                  'hierarchy element is absent from compiled Domain members')
        end
      end

      def validate_control_port_bijection!
        expected = @controls.values.select do |control|
          control.fetch('source') == 'top-port'
        end.to_h { |control| [control.fetch('id'), control] }
        expect!(@logic_control_ports.keys.sort == expected.keys.sort,
                'power_implementation.logic_control_bijection_mismatch',
                '/hierarchy/logicControlPorts',
                'RTL logic control ports are not a bijection with top-port controls')
        @logic_control_ports.each do |id, port|
          exact_keys!(port, %w[id signal source direction], [],
                      pointer('/hierarchy/logicControlPorts', id))
          control = expected.fetch(id)
          expect!(port.fetch('signal') == control.fetch('signal') &&
                    port.fetch('source') == 'top-port' &&
                    port.fetch('direction') == 'input',
                  'power_implementation.logic_control_mismatch',
                  pointer('/hierarchy/logicControlPorts', id),
                  'RTL logic control differs from compiled power control')
        end
      end

      def validate_edge_bijection!
        expected = @context.edges.keys.flat_map do |edge|
          ORIENTATIONS.map { |orientation| edge + [orientation] }
        end.sort
        expect!(@hierarchy_directions.keys.sort == expected,
                'power_implementation.edge_bijection_mismatch',
                '/hierarchy/edgeDirections',
                'hierarchy directions are not a bijection with Domain edges')
        @hierarchy_directions.each do |key, record|
          validate_edge_direction!(key, record)
        end
      end

      def validate_edge_direction!(key, record)
        kind, id, orientation = key
        traffic = @context.traffic(kind, id, orientation)
        expect!(record.fetch('producer') == traffic.fetch('producer') &&
                  record.fetch('consumer') == traffic.fetch('consumer'),
                'power_implementation.traffic_mismatch',
                '/hierarchy/edgeDirections',
                'hierarchy traffic endpoints differ from Domain plan')
        producer_key = reference_key(record.fetch('producer'))
        consumer_key = reference_key(record.fetch('consumer'))
        producer = @hierarchy_elements.fetch(producer_key)
        consumer = @hierarchy_elements.fetch(consumer_key)
        expect!(record.fetch('producerInstance') == producer.fetch('instance') &&
                  record.fetch('consumerInstance') == consumer.fetch('instance'),
                'power_implementation.traffic_instance_mismatch',
                '/hierarchy/edgeDirections',
                'hierarchy traffic instances do not match element instances')
        source = @context.entity_domain(*producer_key, SUPPLY_ROLE).fetch('domain')
        destination = @context.entity_domain(*consumer_key,
                                             SUPPLY_ROLE).fetch('domain')
        expect!(record.fetch('sourceSupplyDomain') == source &&
                  record.fetch('destinationSupplyDomain') == destination,
                'power_implementation.traffic_supply_mismatch',
                '/hierarchy/edgeDirections',
                'hierarchy traffic supply Domains differ from Domain plan')
        validate_flow_wiring!(record)
        validate_power_boundary!(record, source, destination)
      end

      def validate_flow_wiring!(record)
        bridge = record['bridge']
        expected_count = bridge ? 6 : 3
        flows = record.fetch('signalFlows')
        expect!(flows.size == expected_count,
                'power_implementation.incomplete_signal_flows',
                '/hierarchy/edgeDirections',
                'signal flows do not completely describe the emitted channel')
        expected = expected_signal_flows(record)
        actual = flows.to_h do |flow|
          [[flow.fetch('side'), flow.fetch('type')], flow]
        end
        expect!(actual.size == flows.size && actual.keys.sort == expected.keys.sort,
                'power_implementation.signal_flow_bijection_mismatch',
                '/hierarchy/edgeDirections',
                'signal flow roles are duplicated or incomplete')
        expected.each do |key, value|
          flow = actual.fetch(key)
          expect!(flow == value,
                  'power_implementation.signal_flow_wiring_mismatch',
                  '/hierarchy/edgeDirections',
                  'signal flow driver/receiver wiring differs from hierarchy facts')
        end
      end

      def expected_signal_flows(record)
        producer = record.fetch('producerInstance')
        consumer = record.fetch('consumerInstance')
        producer_pins = record.fetch('producerPins')
        consumer_pins = record.fetch('consumerPins')
        bridge = record['bridge']
        unless bridge
          return SIGNAL_TYPES.to_h do |type|
            reverse = type == 'ready'
            flow = flow_fact(
              type, 'direct', record.dig('sourceBundle', type),
              reverse ? consumer : producer,
              reverse ? consumer_pins.fetch(type) : producer_pins.fetch(type),
              reverse ? producer : consumer,
              reverse ? producer_pins.fetch(type) : consumer_pins.fetch(type)
            )
            [['direct', type], flow]
          end
        end
        SIGNAL_TYPES.flat_map do |type|
          reverse = type == 'ready'
          source = flow_fact(
            type, 'source', record.dig('sourceBundle', type),
            reverse ? bridge.fetch('instance') : producer,
            reverse ? bridge.dig('sourcePins', type) : producer_pins.fetch(type),
            reverse ? producer : bridge.fetch('instance'),
            reverse ? producer_pins.fetch(type) : bridge.dig('sourcePins', type)
          )
          destination = flow_fact(
            type, 'destination', record.dig('destinationBundle', type),
            reverse ? consumer : bridge.fetch('instance'),
            reverse ? consumer_pins.fetch(type) : bridge.dig('destinationPins', type),
            reverse ? bridge.fetch('instance') : consumer,
            reverse ? bridge.dig('destinationPins', type) : consumer_pins.fetch(type)
          )
          [[['source', type], source], [['destination', type], destination]]
        end.to_h
      end

      def flow_fact(type, side, signal, driver_instance, driver_pin,
                    receiver_instance, receiver_pin)
        {
          'type' => type,
          'direction' => type == 'ready' ? 'consumer-to-producer' :
            'producer-to-consumer',
          'side' => side,
          'signal' => signal,
          'driver' => {'instance' => driver_instance, 'pin' => driver_pin},
          'receiver' => {'instance' => receiver_instance, 'pin' => receiver_pin}
        }
      end

      def validate_power_boundary!(record, source, destination)
        boundary = object!(record.fetch('powerBoundary'),
                           '/hierarchy/edgeDirections/powerBoundary')
        if record['bridge']
          expected = {
            'status' => 'deferred',
            'reasonCode' =>
              'rtl_hierarchy.infrastructure_bridge_supply_unowned'
          }
        elsif source == destination
          expected = {'status' => 'none'}
        else
          expected = {'status' => 'resolvable'}
        end
        expect!(boundary == expected,
                'power_implementation.power_boundary_mismatch',
                '/hierarchy/edgeDirections/powerBoundary',
                'hierarchy Power boundary status is inconsistent')
      end

      def compile_supplies(active_ids)
        active = []
        inactive = []
        @supplies.values.sort_by { |supply| supply.fetch('id') }.each do |supply|
          id = supply.fetch('id')
          unless active_ids.key?(id)
            inactive << id
            next
          end
          states = supply.fetch('states').sort_by { |state| state.fetch('id') }
                         .map do |state|
            state.merge('token' => token('supply_state', "#{id}:#{state.fetch('id')}"))
          end
          result = supply.merge(
            'token' => token('supply', id),
            'states' => states,
            'status' => 'expected',
            'reason' => 'power_implementation.supply_declared',
            'recipes' => [recipe(
              supply.fetch('exposure') == 'external-port' ?
                'external-supply-port-and-net' : 'internal-switched-supply-net'
            )]
          )
          active << result
        end
        [active, inactive]
      end

      def compile_controls(active_ids)
        active = []
        inactive = []
        @controls.values.sort_by { |control| control.fetch('id') }.each do |control|
          id = control.fetch('id')
          unless active_ids.key?(id)
            inactive << id
            next
          end
          active << control.merge(
            'token' => token('control', control.fetch('id')),
            'status' => 'expected',
            'reason' => control.fetch('source') == 'top-port' ?
              'power_implementation.rtl_control_port_materialized' :
              'power_implementation.upf_control_port_expected',
            'recipes' => [recipe('logic-control')]
          )
        end
        [active, inactive]
      end

      def active_resource_ids(domains)
        active_domains = domains.map { |domain| domain.fetch('domain') }
                                .to_h { |id| [id, true] }
        supplies = {}
        controls = {}
        @power_domains.each_value do |domain|
          next unless active_domains.key?(domain.fetch('domain'))

          %w[primaryPower primaryGround].each do |key|
            supplies[domain.fetch(key)] = true
          end
          if (config = domain['powerSwitch'])
            %w[inputSupply outputSupply].each do |key|
              supplies[config.fetch(key)] = true
            end
            controls[config.fetch('control')] = true
          end
          if (config = domain['retention'])
            supplies[config.fetch('supply')] = true
            controls[config.fetch('saveControl')] = true
            controls[config.fetch('restoreControl')] = true
          end
          if (config = domain['isolation'])
            supplies[config.fetch('supply')] = true
            controls[config.fetch('control')] = true
          end
        end
        @logic_control_ports.each_key { |id| controls[id] = true }
        [supplies, controls]
      end

      def compile_domains
        active = []
        inactive = []
        @power_domains.values.sort_by { |domain| domain.fetch('domain') }.each do |domain|
          record = compile_domain(domain)
          (domain.fetch('active') ? active : inactive) << record
        end
        [active, inactive]
      end

      def compile_domain(domain)
        bindings = domain.fetch('members').sort_by { |m| reference_key(m) }.map do |member|
          emitted = @hierarchy_elements.fetch(reference_key(member))
          {
            'element' => member,
            'module' => emitted.fetch('module'),
            'instance' => emitted.fetch('instance')
          }
        end
        states = domain.fetch('states').sort_by { |state| state.fetch('id') }.map do |state|
          state.merge(
            'token' => token('domain_state',
                             "#{domain.fetch('domain')}:#{state.fetch('id')}")
          )
        end
        status = domain.fetch('active') ? 'expected' : 'not-required'
        reason = domain.fetch('active') ?
          'power_implementation.active_domain' :
          'power_implementation.inactive_domain'
        {
          'domain' => domain.fetch('domain'),
          'token' => domain.fetch('token'),
          'name' => domain.fetch('name'),
          'mode' => domain.fetch('mode'),
          'primaryPower' => domain.fetch('primaryPower'),
          'primaryGround' => domain.fetch('primaryGround'),
          'defaultState' => domain.fetch('defaultState'),
          'states' => states,
          'parameters' => domain.fetch('parameters'),
          'elements' => bindings.map { |binding| binding.fetch('instance') },
          'elementBindings' => bindings,
          'status' => status,
          'reason' => reason,
          'recipes' => [recipe('power-domain')]
        }
      end

      def compile_power_switches(domains)
        active_ids = domains.to_h { |domain| [domain.fetch('domain'), true] }
        @power_domains.values.filter_map do |domain|
          config = domain['powerSwitch']
          next unless config && active_ids.key?(domain.fetch('domain'))

          id = "#{domain.fetch('domain')}:power-switch"
          config.merge(
            'id' => id,
            'token' => token('power_switch', id),
            'domain' => domain.fetch('domain'),
            'technologyCellMappingId' => technology_mapping('power-switch'),
            'status' => 'expected',
            'reason' => 'power_implementation.power_switch_configured',
            'recipes' => [recipe('power-switch')]
          )
        end.sort_by { |record| record.fetch('domain') }
      end

      def compile_retentions(domains)
        active_ids = domains.to_h { |domain| [domain.fetch('domain'), true] }
        @power_domains.values.filter_map do |domain|
          config = domain['retention']
          next unless config && active_ids.key?(domain.fetch('domain'))

          id = "#{domain.fetch('domain')}:retention"
          config.merge(
            'id' => id,
            'token' => token('retention', id),
            'domain' => domain.fetch('domain'),
            'technologyCellMappingId' => technology_mapping('retention'),
            'status' => 'expected',
            'reason' => 'power_implementation.retention_configured',
            'recipes' => [recipe('power-retention')]
          )
        end.sort_by { |record| record.fetch('domain') }
      end

      def compile_system_states
        @system_states.values.sort_by { |state| state.fetch('id') }.map do |state|
          entries = state.fetch('domainStates').sort_by do |entry|
            entry.fetch('domain')
          end
          {
            'id' => state.fetch('id'),
            'domainStates' => entries
          }
        end
      end

      def compile_technology
        {
          'profile' => @technology.fetch('profile'),
          'interfaceCells' => @technology.fetch('interfaceCells').sort_by do |cell|
            cell.fetch('id')
          end
        }
      end

      def compile_edge_orientations
        @hierarchy_directions.values.sort_by do |record|
          edge = record.fetch('edge')
          [EDGE_ORDER.fetch(edge.fetch('kind'), 99), edge.fetch('kind'),
           edge.fetch('id'), ORIENTATIONS.index(record.fetch('orientation'))]
        end.map { |record| compile_edge_orientation(record) }
      end

      def compile_edge_orientation(record)
        edge = record.fetch('edge')
        orientation = record.fetch('orientation')
        id = [edge.fetch('kind'), edge.fetch('id'), orientation].join(':')
        flows = record.fetch('signalFlows').sort_by do |flow|
          [%w[direct source destination].index(flow.fetch('side')),
           SIGNAL_TYPES.index(flow.fetch('type'))]
        end.map { |flow| compile_signal_flow(record, flow, id) }
        aggregate = aggregate_items(flows)
        {
          'id' => id,
          'token' => token('edge_orientation', id),
          'edge' => edge,
          'orientation' => orientation,
          'producer' => record.fetch('producer'),
          'consumer' => record.fetch('consumer'),
          'sourceSupplyDomain' => record.fetch('sourceSupplyDomain'),
          'destinationSupplyDomain' => record.fetch('destinationSupplyDomain'),
          'powerBoundary' => normalize_boundary(record.fetch('powerBoundary')),
          'status' => aggregate.fetch('status'),
          'reason' => aggregate.fetch('reason'),
          'recipes' => aggregate.fetch('recipes'),
          'signalFlows' => flows
        }
      end

      def compile_signal_flow(edge_record, flow, edge_id)
        id = [edge_id, flow.fetch('side'), flow.fetch('type')].join(':')
        driver_domain = terminal_domain(flow.fetch('driver'))
        receiver_domain = terminal_domain(flow.fetch('receiver'))
        boundary = flow_power_boundary(edge_record, driver_domain,
                                       receiver_domain)
        effective_orientation = flow.fetch('direction') ==
          'producer-to-consumer' ? edge_record.fetch('orientation') :
          opposite_orientation(edge_record.fetch('orientation'))
        stages = @context.traffic(
          edge_record.dig('edge', 'kind'), edge_record.dig('edge', 'id'),
          effective_orientation
        ).fetch('stages')
        isolation = compile_isolation_strategy(
          id, flow, driver_domain, receiver_domain, boundary, stages
        )
        shifter = compile_level_shifter_strategy(
          id, flow, driver_domain, receiver_domain, boundary, stages
        )
        aggregate = aggregate_items([isolation, shifter])
        {
          'id' => id,
          'token' => token('signal_flow', id),
          'net' => flow.fetch('signal'),
          'type' => flow.fetch('type'),
          'side' => flow.fetch('side'),
          'direction' => flow.fetch('direction'),
          'driver' => terminal_with_domain(flow.fetch('driver')),
          'receiver' => terminal_with_domain(flow.fetch('receiver')),
          'powerBoundary' => boundary,
          'status' => aggregate.fetch('status'),
          'reason' => aggregate.fetch('reason'),
          'recipes' => aggregate.fetch('recipes'),
          'isolation' => isolation,
          'levelShifter' => shifter
        }
      end

      def compile_isolation_strategy(id, flow, driver_domain, receiver_domain,
                                     boundary, stages)
        recipe_entries = recipes_for(stages, 'power-isolation')
        base = strategy_base(id, 'isolation', flow, driver_domain,
                             recipe_entries,
                             technology_mapping('isolation'))
        return base.merge(deferred_boundary_status(boundary)) if boundary['status'] == 'deferred'
        return base.merge(not_required('power_implementation.same_supply_domain')) if driver_domain == receiver_domain
        return base.merge(not_required('power_implementation.isolation_not_planned')) if recipe_entries.empty?
        driver = @power_domains.fetch(driver_domain)
        return base.merge(not_required('power_implementation.driver_domain_always_on')) if driver.fetch('mode') == 'always-on'
        config = driver['isolation']
        return base.merge(deferred('power_implementation.missing_isolation_configuration')) unless config

        control = @controls.fetch(config.fetch('control'))
        base.merge(
          'supply' => config.fetch('supply'),
          'clampValue' => config.fetch('clampValue'),
          'isolationControl' => config.fetch('control'),
          'isolationSense' => control.fetch('activeSense'),
          'location' => config.fetch('location')
        ).merge(expected('power_implementation.isolation_strategy_complete'))
      end

      def compile_level_shifter_strategy(id, flow, driver_domain,
                                         receiver_domain, boundary, stages)
        recipe_entries = recipes_for(stages, 'power-level-shifter')
        base = strategy_base(
          id, 'level-shifter', flow, driver_domain, recipe_entries, nil
        )
        return base.merge(deferred_boundary_status(boundary)) if boundary['status'] == 'deferred'
        return base.merge(not_required('power_implementation.same_supply_domain')) if driver_domain == receiver_domain
        return base.merge(not_required('power_implementation.level_shifter_not_planned')) if recipe_entries.empty?
        expect!(recipe_entries.size == 1,
                'power_implementation.ambiguous_level_shifter_recipe',
                "/edgeOrientations/#{pointer_token(id)}/levelShifter",
                'a signal flow must have at most one level-shifter recipe')
        rule = recipe_entries.first.dig(
          'directionParameters', 'translation-direction', 'value'
        )
        expect!(%w[up down].include?(rule),
                'power_implementation.invalid_level_shifter_direction',
                "/edgeOrientations/#{pointer_token(id)}/levelShifter",
                'typed level-shifter recipe must resolve to up or down')
        config = @power_domains.fetch(driver_domain)['levelShifter']
        return base.merge(deferred('power_implementation.missing_level_shifter_configuration')) unless config

        base.merge(
          'rule' => rule == 'up' ? 'low_to_high' : 'high_to_low',
          'location' => config.fetch('location'),
          'technologyCellMappingId' =>
            technology_mapping('level-shifter', rule)
        ).merge(expected('power_implementation.level_shifter_strategy_complete'))
      end

      def strategy_base(flow_id, kind, flow, domain, recipes, mapping)
        id = "#{flow_id}:#{kind}"
        driver = flow.fetch('driver')
        {
          'id' => id,
          'token' => token(kind, id),
          'domain' => domain,
          'elements' => [
            "#{driver.fetch('instance')}/#{driver.fetch('pin')}"
          ],
          'appliesTo' => 'outputs',
          'technologyCellMappingId' => mapping,
          'recipes' => recipes
        }
      end

      def terminal_with_domain(terminal)
        terminal.merge('domain' => terminal_domain(terminal))
      end

      def terminal_domain(terminal)
        record = @instances[terminal.fetch('instance')]
        record && record.fetch('supplyDomain')
      end

      def flow_power_boundary(edge_record, driver_domain, receiver_domain)
        edge_status = edge_record.dig('powerBoundary', 'status')
        if edge_status == 'deferred'
          normalize_boundary(edge_record.fetch('powerBoundary'))
        elsif edge_status == 'none' || driver_domain == receiver_domain
          {'status' => 'none'}
        else
          {'status' => 'resolvable'}
        end
      end

      def normalize_boundary(boundary)
        boundary.fetch('status') == 'deferred' ? {
          'status' => 'deferred',
          'reasonCode' => boundary.fetch('reasonCode')
        } : {'status' => boundary.fetch('status')}
      end

      def deferred_boundary_status(boundary)
        deferred(boundary.fetch('reasonCode'))
      end

      def recipes_for(stages, name)
        stages.select { |stage| stage.fetch('recipe') == name }.map do |stage|
          canonicalize(stage)
        end
      end

      def technology_mapping(kind, direction = nil)
        cell = @technology_mappings[[kind, direction]]
        cell && cell.fetch('id')
      end

      def compile_coverage(supplies, inactive_supplies, controls,
                           inactive_controls, domains, inactive_domains,
                           switches, retentions, edges)
        items = []
        [
          ['supply', 'supplies', supplies],
          ['control', 'controls', controls],
          ['domain', 'domains', domains],
          ['power-switch', 'powerSwitches', switches],
          ['retention', 'retentions', retentions]
        ].each do |kind, collection, records|
          records.each_with_index do |record, index|
            items << coverage_item(
              "#{record.fetch('token')}:coverage", kind,
              "/#{collection}/#{index}", record
            )
          end
        end
        inactive_domains.each_with_index do |domain, index|
          items << coverage_item(
            "#{domain.fetch('token')}:coverage", 'inactive-domain',
            "/inactiveIntent/domains/#{index}",
            domain
          )
        end
        inactive_supplies.each_with_index do |id, index|
          source = not_required('power_implementation.inactive_supply')
                   .merge('recipes' => [])
          items << coverage_item(
            "#{token('supply', id)}:inactive-coverage", 'inactive-supply',
            "/inactiveIntent/supplies/#{index}", source
          )
        end
        inactive_controls.each_with_index do |id, index|
          source = not_required('power_implementation.inactive_control')
                   .merge('recipes' => [])
          items << coverage_item(
            "#{token('control', id)}:inactive-coverage", 'inactive-control',
            "/inactiveIntent/controls/#{index}", source
          )
        end
        system_state_receipt = not_required(
          'power_implementation.system_states_receipt_only'
        ).merge('recipes' => [recipe('system-state-receipt')])
        items << coverage_item(
          'system-states:receipt-coverage', 'system-states',
          '/systemStates', system_state_receipt
        )
        items.concat(compile_infrastructure_ownership_coverage(edges))
        domains.each_with_index do |domain, domain_index|
          next unless domain.fetch('mode') == 'switchable'

          sequencing = deferred(
            'power_implementation.shutdown_sequence_unmaterialized'
          ).merge('recipes' => [recipe('power-shutdown-sequence')])
          items << coverage_item(
            "#{domain.fetch('token')}:shutdown-sequence",
            'shutdown-sequence',
            "/domains/#{domain_index}", sequencing
          )
          next unless domain.fetch('elementBindings').any? do |binding|
            binding.dig('element', 'kind') == 'router'
          end

          routing = deferred(
            'power_implementation.router_power_state_routing_unverified'
          ).merge('recipes' => [recipe('routing-connectivity-proof')])
          items << coverage_item(
            "#{domain.fetch('token')}:routing-connectivity",
            'routing-connectivity',
            "/domains/#{domain_index}", routing
          )
        end
        edges.each_with_index do |edge, edge_index|
          edge.fetch('signalFlows').each_with_index do |flow, flow_index|
            %w[isolation levelShifter].each do |strategy_name|
              strategy = flow.fetch(strategy_name)
              items << coverage_item(
                "#{strategy.fetch('id')}:coverage",
                strategy_name == 'levelShifter' ? 'level-shifter' : 'isolation',
                "/edgeOrientations/#{edge_index}" \
                  "/signalFlows/#{flow_index}" \
                  "/#{strategy_name}", strategy
              )
            end
          end
        end
        items.sort_by { |item| item.fetch('id') }
      end

      def compile_infrastructure_ownership_coverage(edges)
        recipe_entries = [recipe('power-domain-infrastructure-ownership')]
        reset_items = @hierarchy.fetch('resetSynchronizers').sort_by do |reset|
          [reset.fetch('timingDomain'), reset.fetch('instance')]
        end.each_with_index.map do |reset, index|
          source = deferred(
            'power_implementation.reset_synchronizer_supply_unowned'
          ).merge('recipes' => recipe_entries)
          coverage_item(
            token('infrastructure_ownership', reset.fetch('instance')),
            'infrastructure-supply-ownership',
            "/hierarchyFacts/resetSynchronizers/#{index}", source
          )
        end

        bridge_items = edges.each_with_index.filter_map do |edge, index|
          key = reference_key(edge.fetch('edge')) + [edge.fetch('orientation')]
          bridge = @hierarchy_directions.fetch(key)['bridge']
          next unless bridge

          source = deferred(
            'rtl_hierarchy.infrastructure_bridge_supply_unowned'
          ).merge('recipes' => recipe_entries)
          coverage_item(
            token('infrastructure_ownership', bridge.fetch('instance')),
            'infrastructure-supply-ownership',
            "/edgeOrientations/#{index}", source
          )
        end
        reset_items + bridge_items
      end

      def coverage_item(id, kind, subject, source)
        {
          'id' => id,
          'kind' => kind,
          'subject' => subject,
          'status' => source.fetch('status'),
          'reason' => source.fetch('reason'),
          'recipes' => source.fetch('recipes')
        }
      end

      def aggregate_items(items)
        status = if items.any? { |item| item.fetch('status') == 'deferred' }
                   'deferred'
                 elsif items.any? { |item| item.fetch('status') == 'expected' }
                   'expected'
                 else
                   'not-required'
                 end
        selected = items.find { |item| item.fetch('status') == status }
        {
          'status' => status,
          'reason' => selected.fetch('reason'),
          'recipes' => items.flat_map { |item| item.fetch('recipes') }
                            .uniq.sort_by do |entry|
            JSON.generate(canonicalize(entry))
          end
        }
      end

      def expected(reason)
        {'status' => 'expected', 'reason' => reason}
      end

      def not_required(reason)
        {'status' => 'not-required', 'reason' => reason}
      end

      def deferred(reason)
        {'status' => 'deferred', 'reason' => reason}
      end

      def recipe(name)
        {'recipe' => name}
      end

      def opposite_orientation(orientation)
        orientation == 'from-to' ? 'to-from' : 'from-to'
      end

      def header(value)
        {
          'format' => value.fetch('format'),
          'formatVersion' => value.fetch('formatVersion')
        }
      end

      def validate_header!(value, format, version, path)
        value = object!(value, path)
        exact_keys!(value, %w[format formatVersion], [], path)
        expect!(value.fetch('format') == format &&
                  value.fetch('formatVersion') == version,
                'power_implementation.source_contract_mismatch', path,
                'source contract header is incompatible')
      end

      def index_records!(values, key, path, code)
        values.each_with_index.each_with_object({}) do |(record, index), result|
          record = object!(record, "#{path}/#{index}")
          id = string!(record[key], "#{path}/#{index}/#{key}")
          expect!(!result.key?(id), code, "#{path}/#{index}/#{key}",
                  "duplicate #{key} #{id}")
          result[id] = record
        end
      end

      def reference!(value, path)
        value = object!(value, path)
        exact_keys!(value, %w[kind id], [], path)
        {'kind' => string!(value.fetch('kind'), "#{path}/kind"),
         'id' => string!(value.fetch('id'), "#{path}/id")}
      end

      def reference_key(reference)
        reference.values_at('kind', 'id')
      end

      def token(prefix, value)
        prefix = prefix.to_s.gsub(/[^A-Za-z0-9_]+/, '_')
        utf8 = value.to_s.encode(Encoding::UTF_8, invalid: :replace,
                                 undef: :replace, replace: "\uFFFD")
        readable = utf8.gsub(/[^A-Za-z0-9]+/, '_').gsub(/\A_+|_+\z/, '')
        readable = 'item' if readable.empty?
        readable = "i_#{readable}" unless readable.match?(/\A[A-Za-z_]/)
        "#{prefix}_#{readable[0, 32]}_#{Digest::SHA256.hexdigest(utf8)}"
      end

      def exact_keys!(object, required, optional, path)
        allowed = required + optional
        unknown = object.keys - allowed
        missing = required - object.keys
        fail!('power_implementation.unknown_field',
              pointer(path, unknown.first),
              "unknown field #{unknown.first}") unless unknown.empty?
        fail!('power_implementation.missing_field',
              pointer(path, missing.first),
              "missing field #{missing.first}") unless missing.empty?
      end

      def pointer(path, token_value)
        "#{path == '/' ? '' : path}/#{pointer_token(token_value)}"
      end

      def pointer_token(value)
        value.to_s.gsub('~', '~0').gsub('/', '~1')
      end

      def object!(value, path)
        expect!(value.is_a?(Hash), 'power_implementation.expected_object', path,
                'expected an object')
        value
      end

      def array!(value, path)
        expect!(value.is_a?(Array), 'power_implementation.expected_array', path,
                'expected an array')
        value
      end

      def string!(value, path)
        expect!(value.is_a?(String) && !value.strip.empty?,
                'power_implementation.expected_string', path,
                'expected a non-empty string')
        value
      end

      def boolean!(value, path)
        expect!(value == true || value == false,
                'power_implementation.expected_boolean', path,
                'expected a boolean')
        value
      end

      def enum!(value, choices, path)
        expect!(choices.include?(value), 'power_implementation.invalid_enum', path,
                "expected one of #{choices.join(', ')}")
        value
      end

      def canonicalize(value)
        case value
        when Hash
          value.keys.sort.to_h do |key|
            [canonicalize(key), canonicalize(value.fetch(key))]
          end
        when Array then value.map { |child| canonicalize(child) }
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
        raise PowerImplementationPlanError.new(code, path, message)
      end
    end
  end
end
