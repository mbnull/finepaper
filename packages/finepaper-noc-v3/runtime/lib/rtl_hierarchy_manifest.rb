# frozen_string_literal: true

require_relative 'domain_rtl_context'

module FinepaperNoc
  class RtlHierarchyManifestError < StandardError
    attr_reader :code, :path

    def initialize(code, path, message)
      @code = code
      @path = path
      super("#{message} at #{path}")
    end
  end

  # Captures names chosen by an RTL emitter without reconstructing its product
  # hierarchy. Instance and signal paths are relative to the generated top
  # scope, so downstream implementation tools can consume them directly.
  class RtlHierarchyManifestBuilder
    FORMAT = 'finepaper.noc-rtl-hierarchy'
    VERSION = 1
    TIMING_ROLE = 'timing-domain'
    SUPPLY_ROLE = 'supply-domain'
    ASYNC_FIFO_RECIPE = 'clock-async-fifo'
    ORIENTATIONS = DomainRtlContext::ORIENTATIONS
    SIGNAL_TYPES = %w[payload valid ready].freeze
    EDGE_ORDER = {'router-link' => 0, 'endpoint-attachment' => 1}.freeze
    HDL_IDENTIFIER = /\A[A-Za-z_][A-Za-z0-9_$]*\z/

    def initialize(context:, design:, top_module:, top_artifact:,
                   expected_logic_control_ports: [])
      @context = context
      @design = design
      @top_module = top_module
      @top_artifact = top_artifact
      @elements = {}
      @instances = {}
      @signals = {}
      @reset_synchronizers = {}
      @edge_directions = {}
      validate_header!
      @expected_logic_control_ports = index_expected_logic_control_ports!(
        expected_logic_control_ports
      )
      @logic_control_ports = {}
    end

    def register_logic_control_port(control_id:, signal:, source:, direction:)
      record = canonicalize(logic_control_port!({
        'id' => control_id,
        'signal' => signal,
        'source' => source,
        'direction' => direction
      }, '/logicControlPorts/new'))
      id = record.fetch('id')
      expected = @expected_logic_control_ports[id]
      expect!(expected, 'rtl_hierarchy.unknown_logic_control_port',
              '/logicControlPorts/new/id',
              "logic control #{id} is absent from the expected top ports")
      expect!(!@logic_control_ports.key?(id),
              'rtl_hierarchy.duplicate_logic_control_port',
              '/logicControlPorts/new/id',
              "logic control #{id} is already registered")
      expect!(record == expected,
              'rtl_hierarchy.logic_control_port_mismatch',
              '/logicControlPorts/new',
              "logic control #{id} differs from the expected top port")
      @logic_control_ports[id] = record
      self
    end

    def register_element(element:, module_name:, instance:)
      reference = reference!(element, '/elements/new/element')
      key = reference_key(reference)
      planned = @context.entity(*key)
      expect!(planned, 'rtl_hierarchy.unknown_element', '/elements/new/element',
              "element #{key.join(' ')} is absent from the implementation plan")
      expect!(!@elements.key?(key), 'rtl_hierarchy.duplicate_element',
              '/elements/new/element', "element #{key.join(' ')} is already registered")
      module_name = identifier!(module_name, '/elements/new/module')
      instance = relative_path!(instance, '/elements/new/instance')
      clock_domain = entity_domain_id(reference, TIMING_ROLE)
      supply_domain = entity_domain_id(reference, SUPPLY_ROLE)
      register_unique_instance!(instance, "element #{key.join(' ')}",
                                '/elements/new/instance')
      @elements[key] = {
        'element' => reference,
        'module' => module_name,
        'instance' => instance,
        'clockDomain' => clock_domain,
        'supplyDomain' => supply_domain
      }
      self
    end

    def register_reset_synchronizer(timing_domain:, module_name:, instance:,
                                    clock_signal:, async_reset_signal:,
                                    local_reset_signal:)
      timing_domain = non_empty_string!(timing_domain,
                                        '/resetSynchronizers/new/timingDomain')
      active = active_timing_domains.to_h do |domain|
        [domain.fetch('domain'), domain]
      end
      expect!(active.key?(timing_domain), 'rtl_hierarchy.unknown_timing_domain',
              '/resetSynchronizers/new/timingDomain',
              "timing Domain #{timing_domain} is not active")
      expect!(!@reset_synchronizers.key?(timing_domain),
              'rtl_hierarchy.duplicate_reset_synchronizer',
              '/resetSynchronizers/new/timingDomain',
              "timing Domain #{timing_domain} already has reset infrastructure")
      module_name = identifier!(module_name,
                                '/resetSynchronizers/new/module')
      instance = relative_path!(instance,
                                '/resetSynchronizers/new/instance')
      clock_signal = relative_path!(
        clock_signal, '/resetSynchronizers/new/clockSignal'
      )
      async_reset_signal = relative_path!(
        async_reset_signal, '/resetSynchronizers/new/asyncResetSignal'
      )
      local_reset_signal = relative_path!(
        local_reset_signal, '/resetSynchronizers/new/localResetSignal'
      )
      register_unique_instance!(instance, "reset synchronizer #{timing_domain}",
                                '/resetSynchronizers/new/instance')
      @reset_synchronizers[timing_domain] = {
        'timingDomain' => timing_domain,
        'module' => module_name,
        'instance' => instance,
        'placement' => 'infrastructure',
        'clockSignal' => clock_signal,
        'asyncResetSignal' => async_reset_signal,
        'localResetSignal' => local_reset_signal
      }
      self
    end

    def register_edge_direction(edge:, orientation:, producer:, consumer:,
                                producer_pins:, consumer_pins:,
                                source_bundle:, destination_bundle:, bridge: nil)
      edge = reference!(edge, '/edgeDirections/new/edge')
      orientation = non_empty_string!(orientation,
                                      '/edgeDirections/new/orientation')
      expect!(ORIENTATIONS.include?(orientation),
              'rtl_hierarchy.invalid_orientation',
              '/edgeDirections/new/orientation',
              "unknown traffic orientation #{orientation}")
      edge_key = reference_key(edge)
      planned_edge = @context.edge(*edge_key)
      expect!(planned_edge, 'rtl_hierarchy.unknown_edge',
              '/edgeDirections/new/edge',
              "edge #{edge_key.join(' ')} is absent from the implementation plan")
      key = edge_key + [orientation]
      expect!(!@edge_directions.key?(key),
              'rtl_hierarchy.duplicate_edge_direction',
              '/edgeDirections/new',
              "edge direction #{key.join(' ')} is already registered")

      producer = reference!(producer, '/edgeDirections/new/producer')
      consumer = reference!(consumer, '/edgeDirections/new/consumer')
      traffic = @context.traffic(*edge_key, orientation)
      expect!(producer == traffic.fetch('producer') &&
                consumer == traffic.fetch('consumer'),
              'rtl_hierarchy.traffic_mismatch', '/edgeDirections/new',
              'emitted producer and consumer differ from the implementation plan')
      producer_record = registered_element!(producer,
                                            '/edgeDirections/new/producer')
      consumer_record = registered_element!(consumer,
                                            '/edgeDirections/new/consumer')
      producer_pins = pin_bundle!(producer_pins,
                                  '/edgeDirections/new/producerPins')
      consumer_pins = pin_bundle!(consumer_pins,
                                  '/edgeDirections/new/consumerPins')
      source_bundle = bundle!(source_bundle,
                              '/edgeDirections/new/sourceBundle')
      destination_bundle = bundle!(destination_bundle,
                                   '/edgeDirections/new/destinationBundle')

      fifo_expected = !@context.edge_stage(*edge_key, ASYNC_FIFO_RECIPE).nil?
      bridge = bridge!(bridge, '/edgeDirections/new/bridge') if bridge
      expect!(fifo_expected == !bridge.nil?,
              'rtl_hierarchy.bridge_plan_mismatch',
              '/edgeDirections/new/bridge',
              'an emitted bridge is required exactly when the plan contains an async FIFO')
      if bridge
        expect!(source_bundle != destination_bundle,
                'rtl_hierarchy.bridged_bundle_alias',
                '/edgeDirections/new',
                'a bridge requires distinct source and destination bundles')
      else
        expect!(source_bundle == destination_bundle,
                'rtl_hierarchy.direct_bundle_mismatch',
                '/edgeDirections/new',
                'a direct edge direction must share one source/destination bundle')
      end

      signal_paths = new_signal_paths!(
        key, source_bundle, destination_bundle, !bridge.nil?
      )
      signal_flows = compile_signal_flows(
        producer_record: producer_record,
        consumer_record: consumer_record,
        producer_pins: producer_pins,
        consumer_pins: consumer_pins,
        source_bundle: source_bundle,
        destination_bundle: destination_bundle,
        bridge: bridge
      )
      source_supply = entity_domain_id(producer, SUPPLY_ROLE)
      destination_supply = entity_domain_id(consumer, SUPPLY_ROLE)
      power_boundary = compile_power_boundary(
        source_supply, destination_supply, bridge
      )
      if bridge
        register_unique_instance!(bridge.fetch('instance'),
                                  "bridge #{key.join(' ')}",
                                  '/edgeDirections/new/bridge/instance')
      end
      register_signal_paths!(signal_paths, key)

      @edge_directions[key] = {
        'edge' => edge,
        'orientation' => orientation,
        'producer' => producer,
        'consumer' => consumer,
        'producerInstance' => producer_record.fetch('instance'),
        'consumerInstance' => consumer_record.fetch('instance'),
        'producerPins' => producer_pins,
        'consumerPins' => consumer_pins,
        'sourceBundle' => source_bundle,
        'destinationBundle' => destination_bundle,
        'sourceClockDomain' => entity_domain_id(producer, TIMING_ROLE),
        'destinationClockDomain' => entity_domain_id(consumer, TIMING_ROLE),
        'sourceSupplyDomain' => source_supply,
        'destinationSupplyDomain' => destination_supply,
        'bridge' => bridge,
        'signalFlows' => signal_flows,
        'powerBoundary' => power_boundary
      }
      self
    end

    def build
      validate_completeness!
      canonicalize({
        'format' => FORMAT,
        'formatVersion' => VERSION,
        'design' => @design,
        'topModule' => @top_module,
        'topArtifact' => @top_artifact,
        'logicControlPorts' => @logic_control_ports.values.sort_by do |record|
          [record.fetch('id'), record.fetch('signal')]
        end,
        'elements' => @elements.values.sort_by do |record|
          reference_sort_key(record.fetch('element'))
        end,
        'resetSynchronizers' => @reset_synchronizers.values.sort_by do |record|
          record.fetch('timingDomain')
        end,
        'edgeDirections' => @edge_directions.values.sort_by do |record|
          edge_direction_sort_key(record)
        end
      })
    end

    private

    def validate_header!
      expect!(@context.is_a?(DomainRtlContext),
              'rtl_hierarchy.invalid_context', '/context',
              'context must be a DomainRtlContext')
      @design = non_empty_string!(@design, '/design')
      expect!(@design == @context.plan.fetch('design'),
              'rtl_hierarchy.design_mismatch', '/design',
              'manifest design differs from the implementation plan')
      @top_module = identifier!(@top_module, '/topModule')
      @top_artifact = artifact_path!(@top_artifact, '/topArtifact')
    end

    def validate_completeness!
      expected_elements = @context.entities.keys.sort
      expect!(@elements.keys.sort == expected_elements,
              'rtl_hierarchy.incomplete_elements', '/elements',
              'emitted element instances are not a bijection with the implementation plan')
      expected_resets = active_timing_domains.map { |domain| domain.fetch('domain') }
      expect!(@reset_synchronizers.keys.sort == expected_resets.sort,
              'rtl_hierarchy.incomplete_reset_synchronizers',
              '/resetSynchronizers',
              'active timing Domains are not a bijection with reset synchronizers')
      expected_directions = @context.edges.keys.flat_map do |edge_key|
        ORIENTATIONS.map { |orientation| edge_key + [orientation] }
      end.sort
      expect!(@edge_directions.keys.sort == expected_directions,
              'rtl_hierarchy.incomplete_edge_directions', '/edgeDirections',
              'emitted edge directions are not a bijection with physical edge orientations')
      expect!(@logic_control_ports == @expected_logic_control_ports,
              'rtl_hierarchy.incomplete_logic_control_ports',
              '/logicControlPorts',
              'emitted logic controls are not a bijection with expected top ports')
    end

    def index_expected_logic_control_ports!(value)
      expect!(value.is_a?(Array),
              'rtl_hierarchy.expected_logic_control_ports',
              '/expectedLogicControlPorts',
              'expected logic control ports must be an array')
      indexed = {}
      signals = {}
      value.each_with_index do |entry, index|
        path = "/expectedLogicControlPorts/#{index}"
        record = canonicalize(logic_control_port!(entry, path))
        id = record.fetch('id')
        signal = record.fetch('signal')
        expect!(!indexed.key?(id),
                'rtl_hierarchy.duplicate_expected_logic_control_port',
                "#{path}/id", "duplicate expected logic control #{id}")
        expect!(!signals.key?(signal),
                'rtl_hierarchy.duplicate_expected_logic_control_signal',
                "#{path}/signal",
                "duplicate expected logic control signal #{signal}")
        indexed[id] = record
        signals[signal] = id
      end
      indexed
    end

    def logic_control_port!(value, path)
      expect!(value.is_a?(Hash), 'rtl_hierarchy.expected_logic_control_port',
              path, 'logic control port must be an object')
      exact_keys!(value, %w[id signal source direction], path)
      id = non_empty_string!(value.fetch('id'), "#{path}/id")
      signal = identifier!(value.fetch('signal'), "#{path}/signal")
      source = non_empty_string!(value.fetch('source'), "#{path}/source")
      direction = non_empty_string!(value.fetch('direction'),
                                    "#{path}/direction")
      expect!(source == 'top-port', 'rtl_hierarchy.invalid_logic_control_source',
              "#{path}/source",
              'an RTL logic control port must have source top-port')
      expect!(direction == 'input',
              'rtl_hierarchy.invalid_logic_control_direction',
              "#{path}/direction",
              'a power-intent logic control port must be an input')
      {
        'id' => id,
        'signal' => signal,
        'source' => source,
        'direction' => direction
      }
    end

    def active_timing_domains
      @context.domains_for_role(TIMING_ROLE).reject do |domain|
        domain.fetch('members').empty?
      end
    end

    def entity_domain_id(reference, role)
      @context.entity_domain(
        reference.fetch('kind'), reference.fetch('id'), role
      ).fetch('domain')
    end

    def registered_element!(reference, path)
      record = @elements[reference_key(reference)]
      expect!(record, 'rtl_hierarchy.unregistered_traffic_element', path,
              "traffic element #{reference.values_at('kind', 'id').join(' ')} " \
              'has no emitted instance')
      record
    end

    def bridge!(value, path)
      expect!(value.is_a?(Hash), 'rtl_hierarchy.expected_bridge', path,
              'bridge must be an object')
      exact_keys!(value, %w[
        module instance placement sourcePins destinationPins
      ], path)
      expect!(value.fetch('placement') == 'infrastructure',
              'rtl_hierarchy.invalid_bridge_placement', "#{path}/placement",
              'async FIFO ownership must be explicitly placed in infrastructure')
      source_pins = pin_bundle!(value.fetch('sourcePins'),
                                "#{path}/sourcePins")
      destination_pins = pin_bundle!(value.fetch('destinationPins'),
                                     "#{path}/destinationPins")
      expect!((source_pins.values + destination_pins.values).uniq.size == 6,
              'rtl_hierarchy.duplicate_bridge_pin', path,
              'source-side and destination-side bridge pins must be distinct')
      {
        'module' => identifier!(value.fetch('module'), "#{path}/module"),
        'instance' => relative_path!(value.fetch('instance'),
                                     "#{path}/instance"),
        'placement' => 'infrastructure',
        'sourcePins' => source_pins,
        'destinationPins' => destination_pins
      }
    end

    def pin_bundle!(value, path)
      expect!(value.is_a?(Hash), 'rtl_hierarchy.expected_pin_bundle', path,
              'pin bundle must be an object')
      exact_keys!(value, SIGNAL_TYPES, path)
      pins = SIGNAL_TYPES.to_h do |type|
        [type, identifier!(value.fetch(type), "#{path}/#{type}")]
      end
      expect!(pins.values.uniq.size == SIGNAL_TYPES.size,
              'rtl_hierarchy.duplicate_pin', path,
              'payload, valid, and ready pins must be distinct')
      pins
    end

    def bundle!(value, path)
      expect!(value.is_a?(Hash), 'rtl_hierarchy.expected_bundle', path,
              'bundle must be an object')
      exact_keys!(value, %w[name payload valid ready], path)
      value.keys.to_h do |key|
        [key, relative_path!(value.fetch(key), "#{path}/#{key}")]
      end
    end

    def new_signal_paths!(key, source_bundle, destination_bundle, bridged)
      bundles = [source_bundle]
      bundles << destination_bundle if bridged
      paths = bundles.flat_map do |bundle|
        SIGNAL_TYPES.map { |type| bundle.fetch(type) }
      end
      expect!(paths.uniq.size == paths.size,
              'rtl_hierarchy.duplicate_signal_path',
              '/edgeDirections/new',
              "edge direction #{key.join(' ')} aliases distinct signal roles")
      paths.each do |signal|
        owner = @signals[signal]
        expect!(!owner, 'rtl_hierarchy.duplicate_signal_path',
                '/edgeDirections/new',
                "signal #{signal} is already owned by #{owner}")
      end
      paths
    end

    def register_signal_paths!(paths, key)
      owner = key.join(' ')
      paths.each { |signal| @signals[signal] = owner }
    end

    def compile_signal_flows(producer_record:, consumer_record:,
                             producer_pins:, consumer_pins:, source_bundle:,
                             destination_bundle:, bridge:)
      producer = producer_record.fetch('instance')
      consumer = consumer_record.fetch('instance')
      unless bridge
        return SIGNAL_TYPES.map do |type|
          direct_signal_flow(
            type, source_bundle.fetch(type), producer, consumer,
            producer_pins, consumer_pins
          )
        end
      end

      source = SIGNAL_TYPES.map do |type|
        bridged_signal_flow(
          type, 'source', source_bundle.fetch(type), producer, consumer,
          producer_pins, consumer_pins, bridge
        )
      end
      destination = SIGNAL_TYPES.map do |type|
        bridged_signal_flow(
          type, 'destination', destination_bundle.fetch(type),
          producer, consumer, producer_pins, consumer_pins, bridge
        )
      end
      source + destination
    end

    def direct_signal_flow(type, signal, producer, consumer,
                           producer_pins, consumer_pins)
      reverse = type == 'ready'
      {
        'type' => type,
        'direction' => reverse ? 'consumer-to-producer' :
          'producer-to-consumer',
        'side' => 'direct',
        'signal' => signal,
        'driver' => terminal(
          reverse ? consumer : producer,
          reverse ? consumer_pins.fetch(type) : producer_pins.fetch(type)
        ),
        'receiver' => terminal(
          reverse ? producer : consumer,
          reverse ? producer_pins.fetch(type) : consumer_pins.fetch(type)
        )
      }
    end

    def bridged_signal_flow(type, side, signal, producer, consumer,
                            producer_pins, consumer_pins, bridge)
      reverse = type == 'ready'
      bridge_instance = bridge.fetch('instance')
      bridge_pins = bridge.fetch(
        side == 'source' ? 'sourcePins' : 'destinationPins'
      )
      if side == 'source'
        driver = reverse ?
          terminal(bridge_instance, bridge_pins.fetch(type)) :
          terminal(producer, producer_pins.fetch(type))
        receiver = reverse ?
          terminal(producer, producer_pins.fetch(type)) :
          terminal(bridge_instance, bridge_pins.fetch(type))
      else
        driver = reverse ?
          terminal(consumer, consumer_pins.fetch(type)) :
          terminal(bridge_instance, bridge_pins.fetch(type))
        receiver = reverse ?
          terminal(bridge_instance, bridge_pins.fetch(type)) :
          terminal(consumer, consumer_pins.fetch(type))
      end
      {
        'type' => type,
        'direction' => reverse ? 'consumer-to-producer' :
          'producer-to-consumer',
        'side' => side,
        'signal' => signal,
        'driver' => driver,
        'receiver' => receiver
      }
    end

    def terminal(instance, pin)
      {'instance' => instance, 'pin' => pin}
    end

    def compile_power_boundary(source_supply, destination_supply, bridge)
      if bridge
        return {
          'status' => 'deferred',
          'reasonCode' =>
            'rtl_hierarchy.infrastructure_bridge_supply_unowned'
        }
      end
      return {'status' => 'none'} if source_supply == destination_supply

      {'status' => 'resolvable'}
    end

    def reference!(value, path)
      expect!(value.is_a?(Hash), 'rtl_hierarchy.expected_reference', path,
              'element reference must be an object')
      exact_keys!(value, %w[kind id], path)
      {
        'kind' => non_empty_string!(value.fetch('kind'), "#{path}/kind"),
        'id' => non_empty_string!(value.fetch('id'), "#{path}/id")
      }
    end

    def exact_keys!(object, allowed, path)
      unknown = object.keys - allowed
      missing = allowed - object.keys
      expect!(unknown.empty?, 'rtl_hierarchy.unknown_field',
              "#{path}/#{unknown.first}", "unknown field #{unknown.first}")
      expect!(missing.empty?, 'rtl_hierarchy.missing_field',
              "#{path}/#{missing.first}", "missing field #{missing.first}")
    end

    def artifact_path!(value, path)
      value = non_empty_string!(value, path)
      valid = !value.start_with?('/') && !value.match?(/\A[A-Za-z]:\//) &&
              !value.include?('\\') && value.split('/').none? do |part|
                part.empty? || %w[. ..].include?(part)
              end
      expect!(valid, 'rtl_hierarchy.invalid_artifact_path', path,
              'artifact path must be a contained relative POSIX path')
      value
    end

    def relative_path!(value, path)
      value = non_empty_string!(value, path)
      components = value.split('/')
      valid = !value.start_with?('/') && components.all? do |component|
        component.match?(HDL_IDENTIFIER)
      end
      valid &&= components.first != @top_module
      expect!(valid, 'rtl_hierarchy.invalid_top_scope_path', path,
              'path must be an HDL top-scope path without the top module prefix')
      value
    end

    def identifier!(value, path)
      value = non_empty_string!(value, path)
      expect!(value.match?(HDL_IDENTIFIER), 'rtl_hierarchy.invalid_identifier',
              path, 'value must be an HDL identifier')
      value
    end

    def non_empty_string!(value, path)
      expect!(value.is_a?(String) && !value.strip.empty?,
              'rtl_hierarchy.expected_string', path,
              'expected a non-empty string')
      value
    end

    def register_unique_instance!(instance, owner, path)
      previous = @instances[instance]
      expect!(!previous, 'rtl_hierarchy.duplicate_instance', path,
              "instance #{instance} is already owned by #{previous}")
      @instances[instance] = owner
    end

    def reference_key(reference)
      reference.values_at('kind', 'id')
    end

    def reference_sort_key(reference)
      reference_key(reference)
    end

    def edge_direction_sort_key(record)
      edge = record.fetch('edge')
      [EDGE_ORDER.fetch(edge.fetch('kind'), 99),
       edge.fetch('kind'), edge.fetch('id'),
       ORIENTATIONS.index(record.fetch('orientation'))]
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

    def expect!(condition, code, path, message)
      raise RtlHierarchyManifestError.new(code, path, message) unless condition

      condition
    end
  end
end
