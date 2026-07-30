# frozen_string_literal: true

require 'set'
require_relative '../domain_rtl_context'

module FinepaperNoc
  module PowerIntent
    class ControlPortPreflightError < StandardError
      attr_reader :code, :path, :detail

      def initialize(code, path, message, power_intent_relative: true)
        @code = code
        @path = path
        @detail = message
        @power_intent_relative = power_intent_relative
        super("#{message} at #{path}")
      end

      def power_intent_relative?
        @power_intent_relative
      end
    end

    # Builds a collision-free namespace for the legacy RTL shell, then validates
    # optional Package-owned Power controls against it. The naming helpers are
    # shared with the generator so validation and emitted SystemVerilog align.
    class ControlPortPreflight
      PLAN_FORMAT = 'finepaper.noc-power-intent-plan'.freeze
      PLAN_VERSION = 1
      TIMING_ROLE = 'timing-domain'.freeze
      ASYNC_FIFO_RECIPE = 'clock-async-fifo'.freeze
      HDL_IDENTIFIER = /\A[A-Za-z_][A-Za-z0-9_$]*\z/
      CONTROL_CHARACTERS = /\p{Cc}/
      TOP_PARAMETERS = %w[DATA_WIDTH FLIT_WIDTH ADDR_WIDTH].freeze
      BUNDLE_SUFFIXES = %w[flit valid ready].freeze
      DEFAULT_ENDPOINT_PORT_SUFFIXES = %w[
        flit_in flit_in_valid flit_in_ready
        flit_out flit_out_valid flit_out_ready
      ].freeze

      # IEEE 1800 reserved words share the same lexical namespace as simple
      # identifiers. Keep this grammar rule here instead of scattering ad-hoc
      # checks across the adapter and emitter.
      SYSTEMVERILOG_KEYWORDS = Set.new(%w[
        accept_on alias always always_comb always_ff always_latch and assert
        assign assume automatic before begin bind bins binsof bit break buf
        bufif0 bufif1 byte case casex casez cell chandle checker class clocking
        cmos config const constraint context continue cover covergroup
        coverpoint cross deassign default defparam design disable dist do edge
        else end endcase endchecker endclass endclocking endconfig endfunction
        endgenerate endgroup endinterface endmodule endpackage endprimitive
        endprogram endproperty endspecify endsequence endtable endtask enum
        event eventually expect export extends extern final first_match for
        force foreach forever fork forkjoin function generate genvar global
        highz0 highz1 if iff ifnone ignore_bins illegal_bins implements implies
        import incdir include initial inout input inside instance int integer
        interconnect interface intersect join join_any join_none large let
        liblist library local localparam logic longint macromodule matches medium
        modport module nand negedge nettype new nexttime nmos nor
        noshowcancelled not notif0
        notif1 null or output package packed parameter pmos posedge primitive
        priority program property protected pull0 pull1 pulldown pullup pulmos
        pulsestyle_ondetect pulsestyle_onevent pure rand randc randcase
        randomize randsequence rcmos real realtime ref reg
        reject_on release repeat restrict return rnmos rpmos rtran rtranif0
        rtranif1 s_always s_eventually s_nexttime s_until s_until_with scalared
        sequence shortint shortreal showcancelled signed small soft solve specify
        specparam srandom static string strong strong0 strong1 struct super supply0
        supply1 sync_accept_on sync_reject_on table tagged task this throughout
        time timeprecision timeunit tran tranif0 tranif1 tri tri0 tri1 triand
        trior trireg type typedef union unique unique0 unsigned untyped until
        until_with use uwire var
        vectored virtual void wait wait_order wand weak weak0 weak1 while
        wildcard wire with within wor xnor xor
      ]).freeze

      class << self
        def validate!(plan:, design:, namespace:, control_paths_by_id: nil)
          new(
            plan: plan,
            design: design,
            namespace: namespace,
            control_paths_by_id: control_paths_by_id
          ).validate!
        end

        def timing_infrastructure(context)
          active = context.domains_for_role(TIMING_ROLE).reject do |domain|
            domain.fetch('members').empty?
          end
          single_clock = active.one?
          active.map do |domain|
            token = domain.fetch('token')
            {
              'domain' => domain.fetch('domain'),
              'token' => token,
              'clockSignal' => single_clock ? 'clk' : "clk_#{token}",
              'resetSignal' => "rst_n_#{token}"
            }
          end
        end

        def top_namespace(context:, endpoint_ports:, rtl_element_ids: {},
                          endpoint_paths_by_id: {})
          namespace = {}
          TOP_PARAMETERS.each do |parameter|
            reserve(namespace, parameter, "top parameter #{parameter}")
          end
          reserve(namespace, 'rst_n', 'reset port rst_n')

          timing_infrastructure(context).each do |record|
            domain = record.fetch('domain')
            reserve(namespace, record.fetch('clockSignal'),
                    "clock Domain #{domain}")
            reserve(namespace, record.fetch('resetSignal'),
                    "local reset for clock Domain #{domain}")
            reserve(namespace, "u_reset_#{record.fetch('token')}",
                    "reset synchronizer for clock Domain #{domain}")
          end

          endpoint_ports.keys.sort.each do |endpoint_id|
            endpoint_path = endpoint_paths_by_id[endpoint_id]
            exposed_endpoint_ports(
              endpoint_id, endpoint_ports.fetch(endpoint_id)
            ).each do |signal|
              reserve(namespace, signal, "Endpoint #{endpoint_id}",
                      path: endpoint_path)
            end
            reserve(namespace, "u_ni_#{endpoint_id}",
                    "Endpoint #{endpoint_id} instance", path: endpoint_path)
          end

          context.entities.each_value do |entry|
            element = entry.fetch('element')
            next unless element.fetch('kind') == 'router'

            rtl_id = rtl_element_id(element, rtl_element_ids)
            reserve(namespace, "u_#{rtl_id}",
                    "Router #{element.fetch('id')} instance")
          end

          context.edges.each_value do |edge|
            edge_reference = edge.fetch('edge')
            crossing = !context.edge_stage(
              edge_reference.fetch('kind'), edge_reference.fetch('id'),
              ASYNC_FIFO_RECIPE
            ).nil?
            DomainRtlContext::ORIENTATIONS.each do |orientation|
              traffic = context.traffic(
                edge_reference.fetch('kind'), edge_reference.fetch('id'),
                orientation
              )
              base = traffic_bundle_base(
                edge_kind: edge_reference.fetch('kind'),
                producer: traffic.fetch('producer'),
                consumer: traffic.fetch('consumer'),
                rtl_element_ids: rtl_element_ids
              )
              bundle_bases = crossing ? ["#{base}_src", "#{base}_dst"] : [base]
              bundle_bases.each do |bundle|
                BUNDLE_SUFFIXES.each do |suffix|
                  reserve(namespace, "#{bundle}_#{suffix}",
                          "#{edge_reference.fetch('kind')} " \
                          "#{edge_reference.fetch('id')} traffic")
                end
              end
              reserve(namespace, "u_cdc_#{base}",
                      "#{edge_reference.fetch('id')} CDC instance") if crossing
            end
          end
          namespace.freeze
        end

        def traffic_bundle_base(edge_kind:, producer:, consumer:,
                                rtl_element_ids: {})
          case edge_kind
          when 'router-link'
            "link_#{rtl_element_id(producer, rtl_element_ids)}_to_" \
              "#{rtl_element_id(consumer, rtl_element_ids)}"
          when 'endpoint-attachment'
            if producer.fetch('kind') == 'router' &&
               consumer.fetch('kind') == 'endpoint'
              "router_to_ni_#{consumer.fetch('id')}"
            elsif producer.fetch('kind') == 'endpoint' &&
                  consumer.fetch('kind') == 'router'
              "ni_#{producer.fetch('id')}_to_router"
            else
              raise ArgumentError,
                    'Endpoint attachment traffic must connect a Router and Endpoint'
            end
          else
            raise ArgumentError, "unsupported RTL edge kind #{edge_kind}"
          end
        end

        def exposed_endpoint_ports(endpoint_id, port_names)
          names = if port_names&.any?
                    port_names
                  else
                    DEFAULT_ENDPOINT_PORT_SUFFIXES
                  end
          names.map { |name| "#{endpoint_id}_#{name}" }
        end

        private

        def reserve(namespace, name, owner, path: nil)
          unless namespace.key?(name)
            namespace[name] = owner
            return
          end

          existing_owner = namespace.fetch(name)
          escaped_name = name.to_s.gsub('~', '~0').gsub('/', '~1')
          raise ControlPortPreflightError.new(
            'rtl_hierarchy.top_namespace_collision',
            path || "/rtl/topNamespace/#{escaped_name}",
            "generated top-level identifier #{name} for #{owner} " \
            "collides with #{existing_owner}",
            power_intent_relative: false
          )
        end

        def rtl_element_id(reference, mappings)
          mapped = mappings[[reference.fetch('kind'), reference.fetch('id')]]
          return mapped if mapped
          return reference.fetch('id') if reference.fetch('kind') == 'endpoint'

          match = /\Ar-(\d+)-(\d+)\z/.match(reference.fetch('id'))
          unless reference.fetch('kind') == 'router' && match
            raise ArgumentError,
                  "unsupported RTL element #{reference.fetch('kind')} " \
                  "#{reference.fetch('id')}"
          end
          "xp_#{match[1]}_#{match[2]}"
        end
      end

      def initialize(plan:, design:, namespace:, control_paths_by_id: nil)
        @plan = plan
        @design = design
        @namespace = namespace
        @control_paths_by_id = control_paths_by_id
      end

      def validate!
        return [] if @plan.nil?

        expect!(@plan.is_a?(Hash),
                'rtl_hierarchy.invalid_power_intent_plan', '/',
                'power intent plan must be an object')
        expect!(@plan['format'] == PLAN_FORMAT,
                'rtl_hierarchy.invalid_power_intent_plan_format', '/format',
                "expected #{PLAN_FORMAT}")
        expect!(@plan['formatVersion'] == PLAN_VERSION,
                'rtl_hierarchy.invalid_power_intent_plan_version',
                '/formatVersion',
                "expected format version #{PLAN_VERSION}")
        expect!(@plan['design'] == @design,
                'rtl_hierarchy.power_intent_design_mismatch', '/design',
                'power intent plan design differs from the emitted RTL')
        controls = @plan['controls']
        expect!(controls.is_a?(Array),
                'rtl_hierarchy.invalid_power_intent_controls', '/controls',
                'power intent controls must be an array')

        ids = {}
        signals = {}
        top_ports = controls.each_with_index.filter_map do |entry, index|
          canonical_path = "/controls/#{index}"
          control = parse_control!(entry, canonical_path)
          id = control.fetch('id')
          signal = control.fetch('signal')
          path = control_source_path(id, canonical_path)
          expect!(!ids.key?(id), 'rtl_hierarchy.duplicate_power_control',
                  "#{path}/id", "duplicate power control #{id}")
          expect!(!signals.key?(signal),
                  'rtl_hierarchy.duplicate_power_control_signal',
                  "#{path}/signal",
                  "duplicate power control signal #{signal}")
          ids[id] = true
          signals[signal] = true
          expect!(!SYSTEMVERILOG_KEYWORDS.include?(signal),
                  'rtl_hierarchy.reserved_logic_control_identifier',
                  "#{path}/signal",
                  "logic control signal #{signal} is a SystemVerilog keyword")
          owner = @namespace[signal]
          expect!(!owner, 'rtl_hierarchy.logic_control_port_collision',
                  "#{path}/signal",
                  "logic control signal #{signal} collides with #{owner}")
          next unless control.fetch('source') == 'top-port'

          {
            'id' => id.dup,
            'signal' => signal.dup,
            'source' => 'top-port',
            'direction' => 'input'
          }
        end
        top_ports.sort_by { |control| [control.fetch('id'), control.fetch('signal')] }
      end

      private

      def control_source_path(id, canonical_path)
        return canonical_path unless @control_paths_by_id.is_a?(Hash)

        @control_paths_by_id.fetch(id, canonical_path)
      end

      def parse_control!(entry, path)
        expect!(entry.is_a?(Hash),
                'rtl_hierarchy.invalid_power_control', path,
                'power control must be an object')
        required = %w[id signal source activeSense]
        allowed = required + ['ownerDomain']
        unknown = entry.keys - allowed
        missing = required - entry.keys
        expect!(unknown.empty?, 'rtl_hierarchy.unknown_power_control_field',
                "#{path}/#{unknown.first}",
                "unknown power control field #{unknown.first}")
        expect!(missing.empty?, 'rtl_hierarchy.missing_power_control_field',
                "#{path}/#{missing.first}",
                "missing power control field #{missing.first}")
        id = string!(entry.fetch('id'), "#{path}/id")
        signal = string!(entry.fetch('signal'), "#{path}/signal")
        expect!(signal.match?(HDL_IDENTIFIER),
                'rtl_hierarchy.invalid_logic_control_identifier',
                "#{path}/signal",
                'power control signal must be an HDL identifier')
        source = entry.fetch('source')
        expect!(%w[top-port upf-port].include?(source),
                'rtl_hierarchy.invalid_power_control_source',
                "#{path}/source",
                'power control source must be top-port or upf-port')
        active_sense = entry.fetch('activeSense')
        expect!(%w[high low].include?(active_sense),
                'rtl_hierarchy.invalid_power_control_sense',
                "#{path}/activeSense",
                'power control activeSense must be high or low')
        string!(entry.fetch('ownerDomain'), "#{path}/ownerDomain") if entry.key?('ownerDomain')
        {'id' => id, 'signal' => signal, 'source' => source}
      end

      def string!(value, path)
        valid = value.is_a?(String) && value.match?(/\S/) &&
                !value.match?(CONTROL_CHARACTERS)
        expect!(valid, 'rtl_hierarchy.invalid_power_control_string', path,
                'expected a non-empty string without control characters')
        value
      end

      def expect!(condition, code, path, message)
        unless condition
          raise ControlPortPreflightError.new(code, path, message)
        end

        condition
      end
    end
  end
end
