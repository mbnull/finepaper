require 'erb'
require 'fileutils'
require 'json'
require_relative '../../../../lib/domain_rtl_context'
require_relative '../../../../lib/domain_rtl_evidence'
require_relative '../../../../lib/rtl_hierarchy_manifest'

class RtlGenerator
  TIMING_ROLE = 'timing-domain'.freeze
  SUPPLY_ROLE = 'supply-domain'.freeze
  ASYNC_FIFO_RECIPE = 'clock-async-fifo'.freeze
  POWER_INTENT_PLAN_FORMAT = 'finepaper.noc-power-intent-plan'.freeze
  POWER_INTENT_PLAN_VERSION = 1
  HDL_IDENTIFIER = /\A[A-Za-z_][A-Za-z0-9_$]*\z/
  CONTROL_CHARACTERS = /\p{Cc}/

  DIRECTION_ORDER = [
    { name: :east, abbr: 'e' },
    { name: :west, abbr: 'w' },
    { name: :north, abbr: 'n' },
    { name: :south, abbr: 's' }
  ].freeze

  OPPOSITE_DIRECTION = {
    east: :west,
    west: :east,
    north: :south,
    south: :north
  }.freeze

  NI_FEATURE_DEFAULTS = {
    'protocol_decode' => true,
    'request_queue' => true,
    'response_queue' => true,
    'credit_flow' => true,
    'qos' => true,
    'error_check' => true,
    'trace' => false
  }.freeze

  NI_FEATURE_CODES = {
    'protocol_decode' => 'p',
    'request_queue' => 'r',
    'response_queue' => 's',
    'credit_flow' => 'c',
    'qos' => 'o',
    'error_check' => 'e',
    'trace' => 't'
  }.freeze

  def initialize(noc, template_dir)
    @noc = noc
    @template_dir = template_dir
  end

  def render(template, output_path)
    tmpl = File.read(File.join(@template_dir, template))
    FileUtils.mkdir_p(File.dirname(output_path))
    File.write(output_path, ERB.new(tmpl).result(@noc.expose))
  end

  def generate_partitioned(output_dir, ipcore_dir: nil)
    domain_context = build_domain_rtl_context
    validate_domain_rtl_context!(domain_context)
    domain_rendering = build_domain_rtl_rendering(domain_context)
    logic_control_ports = build_logic_control_ports(domain_rendering)
    FileUtils.mkdir_p(output_dir)

    lookup = xp_module_lookup
    ni_lookup = ni_module_lookup
    link_directions = xp_link_directions_by_id
    set_context(:@domain_rtl_context, domain_context)
    set_context(:@domain_rtl_rendering, domain_rendering)
    set_context(:@xp_module_lookup, lookup)
    set_context(:@xp_link_directions_by_id, link_directions)
    set_context(:@ni_module_lookup, ni_lookup)
    set_context(:@ni_features, ni_features)
    set_context(:@logic_control_ports, logic_control_ports)

    xp_paths = xp_variants.map do |variant|
      xp = variant[:xp]
      set_context(:@xp, xp)
      set_context(:@xp_module_name, variant[:module_name])
      set_context(:@xp_variant_signature, variant[:signature])
      set_context(:@xp_port_directions, link_directions_for(xp))
      set_context(:@xp_local_port_count, xp.endpoints.size)
      output_path = File.join(output_dir, variant[:folder], xp_variant_filename(variant))
      render('xp.sv.erb', output_path)
      output_path
    end

    ni_paths = ni_variants.map do |variant|
      xp = variant[:xp]
      endpoint = variant[:endpoint]
      set_context(:@xp, xp)
      set_context(:@ni_module_name, variant[:module_name])
      set_context(:@ni_variant_signature, variant[:signature])
      set_context(:@ni_endpoint_slots, [ni_endpoint_slot(endpoint)])
      output_path = File.join(output_dir, variant[:folder], ni_variant_filename(variant))
      render('ni.sv.erb', output_path)
      output_path
    end

    stub_paths = emit_stub_modules(output_dir)
    endpoint_paths = ipcore_dir ? render_endpoint_templates(output_dir, ipcore_dir) : []
    library_dirs = []
    library_dirs << File.dirname(stub_paths.first) unless stub_paths.empty?
    library_dirs << output_dir unless endpoint_paths.empty?
    top_path = File.join(output_dir, "#{@noc.name}_top.v")
    render('top.v.erb', top_path)
    write_filelist(output_dir, xp_paths + ni_paths + [top_path], library_dirs: library_dirs)
    write_rtl_hierarchy(
      output_dir, domain_context, domain_rendering, lookup, ni_lookup,
      link_directions, logic_control_ports
    )
    write_domain_evidence(output_dir, domain_context, domain_rendering)
  ensure
    clear_context(:@xp, :@xp_module_name, :@xp_variant_signature,
                  :@xp_port_directions, :@xp_local_port_count,
                  :@xp_module_lookup, :@xp_link_directions_by_id,
                  :@ni_module_lookup, :@ni_module_name,
                  :@ni_variant_signature, :@ni_endpoint_slots,
                  :@ni_features, :@domain_rtl_context,
                  :@domain_rtl_rendering, :@logic_control_ports)
  end

  def build_domain_rtl_context
    plan = @noc.domain_implementation
    raise 'domain_implementation is required by the V3 RTL renderer' unless plan.is_a?(Hash)

    FinepaperNoc::DomainRtlContext.new(plan)
  end

  def validate_domain_rtl_context!(context)
    expected_entity_count = @noc.xps.size + @noc.endpoints.size
    expected_edge_count = @noc.connections.size + @noc.endpoints.size
    raise 'Domain RTL entity graph differs from the legacy Mesh graph' unless context.entities.size == expected_entity_count
    raise 'Domain RTL edge graph differs from the legacy Mesh graph' unless context.edges.size == expected_edge_count

    @noc.xps.each do |xp|
      validate_domain_entity!(context, 'router', domain_router_id(xp))
    end
    @noc.endpoints.each do |endpoint|
      validate_domain_entity!(context, 'endpoint', endpoint.id)
      edge = context.edge('endpoint-attachment', endpoint.id)
      raise "Domain RTL context is missing Endpoint attachment #{endpoint.id}" unless edge

      xp = xp_for_endpoint(endpoint.id)
      validate_edge_elements!(
        edge,
        {'kind' => 'router', 'id' => domain_router_id(xp)},
        {'kind' => 'endpoint', 'id' => endpoint.id}
      )
    end
    @noc.connections.each do |connection|
      id = domain_link_id(connection)
      edge = context.edge('router-link', id)
      raise "Domain RTL context is missing Router Link #{id}" unless edge

      endpoints = domain_link_endpoints(connection)
      validate_edge_elements!(
        edge,
        {'kind' => 'router', 'id' => domain_router_id(endpoints.fetch(0))},
        {'kind' => 'router', 'id' => domain_router_id(endpoints.fetch(1))}
      )
    end

    context.edges.each_value do |edge|
      validate_domain_edge!(context, edge)
    end
  end

  def build_logic_control_ports(domain_rendering)
    plan = @noc.power_intent_plan
    return [] if plan.nil?

    power_plan_expect!(plan.is_a?(Hash),
                       'rtl_hierarchy.invalid_power_intent_plan',
                       '/powerIntentPlan',
                       'power intent plan must be an object')
    power_plan_expect!(plan['format'] == POWER_INTENT_PLAN_FORMAT,
                       'rtl_hierarchy.invalid_power_intent_plan_format',
                       '/powerIntentPlan/format',
                       "expected #{POWER_INTENT_PLAN_FORMAT}")
    power_plan_expect!(plan['formatVersion'] == POWER_INTENT_PLAN_VERSION,
                       'rtl_hierarchy.invalid_power_intent_plan_version',
                       '/powerIntentPlan/formatVersion',
                       "expected format version #{POWER_INTENT_PLAN_VERSION}")
    power_plan_expect!(plan['design'] == @noc.name,
                       'rtl_hierarchy.power_intent_design_mismatch',
                       '/powerIntentPlan/design',
                       'power intent plan design differs from the emitted RTL')
    controls = plan['controls']
    power_plan_expect!(controls.is_a?(Array),
                       'rtl_hierarchy.invalid_power_intent_controls',
                       '/powerIntentPlan/controls',
                       'power intent controls must be an array')

    ids = {}
    signals = {}
    top_ports = controls.each_with_index.filter_map do |entry, index|
      path = "/powerIntentPlan/controls/#{index}"
      control = parse_power_logic_control!(entry, path)
      id = control.fetch('id')
      signal = control.fetch('signal')
      power_plan_expect!(!ids.key?(id),
                         'rtl_hierarchy.duplicate_power_control',
                         "#{path}/id", "duplicate power control #{id}")
      power_plan_expect!(!signals.key?(signal),
                         'rtl_hierarchy.duplicate_power_control_signal',
                         "#{path}/signal",
                         "duplicate power control signal #{signal}")
      ids[id] = true
      signals[signal] = true
      next unless control.fetch('source') == 'top-port'

      {
        'id' => id.dup,
        'signal' => signal.dup,
        'source' => 'top-port',
        'direction' => 'input'
      }
    end

    reserved = existing_top_port_names(domain_rendering)
    top_ports.each_with_index do |control, index|
      signal = control.fetch('signal')
      owner = reserved[signal]
      power_plan_expect!(!owner, 'rtl_hierarchy.logic_control_port_collision',
                         "/logicControlPorts/#{index}/signal",
                         "logic control signal #{signal} collides with #{owner}")
      reserved[signal] = "power control #{control.fetch('id')}"
    end
    top_ports.sort_by { |control| [control.fetch('id'), control.fetch('signal')] }
  end

  def parse_power_logic_control!(entry, path)
    power_plan_expect!(entry.is_a?(Hash),
                       'rtl_hierarchy.invalid_power_control', path,
                       'power control must be an object')
    required = %w[id signal source activeSense]
    allowed = required + ['ownerDomain']
    unknown = entry.keys - allowed
    missing = required - entry.keys
    power_plan_expect!(unknown.empty?, 'rtl_hierarchy.unknown_power_control_field',
                       "#{path}/#{unknown.first}",
                       "unknown power control field #{unknown.first}")
    power_plan_expect!(missing.empty?, 'rtl_hierarchy.missing_power_control_field',
                       "#{path}/#{missing.first}",
                       "missing power control field #{missing.first}")
    id = power_plan_string!(entry.fetch('id'), "#{path}/id")
    signal = power_plan_string!(entry.fetch('signal'), "#{path}/signal")
    power_plan_expect!(signal.match?(HDL_IDENTIFIER),
                       'rtl_hierarchy.invalid_logic_control_identifier',
                       "#{path}/signal",
                       'power control signal must be an HDL identifier')
    source = entry.fetch('source')
    power_plan_expect!(%w[top-port upf-port].include?(source),
                       'rtl_hierarchy.invalid_power_control_source',
                       "#{path}/source",
                       'power control source must be top-port or upf-port')
    active_sense = entry.fetch('activeSense')
    power_plan_expect!(%w[high low].include?(active_sense),
                       'rtl_hierarchy.invalid_power_control_sense',
                       "#{path}/activeSense",
                       'power control activeSense must be high or low')
    if entry.key?('ownerDomain')
      power_plan_string!(entry.fetch('ownerDomain'), "#{path}/ownerDomain")
    end
    {'id' => id, 'signal' => signal, 'source' => source}
  end

  def power_plan_string!(value, path)
    valid = value.is_a?(String) && value.match?(/\S/) &&
            !value.match?(CONTROL_CHARACTERS)
    power_plan_expect!(valid, 'rtl_hierarchy.invalid_power_control_string',
                       path,
                       'expected a non-empty string without control characters')
    value
  end

  def existing_top_port_names(domain_rendering)
    owners = {'rst_n' => 'reset port rst_n'}
    domain_rendering.fetch('clockDomains').each do |domain|
      signal = domain.fetch('clockSignal')
      owners[signal] = "clock Domain #{domain.fetch('domain')}"
    end
    @noc.endpoints.each do |endpoint|
      endpoint_top_port_names(endpoint).each do |signal|
        owners[signal] = "Endpoint #{endpoint.id}"
      end
    end
    owners
  end

  def endpoint_top_port_names(endpoint)
    if endpoint.ports&.any?
      return endpoint.ports.map { |port| "#{endpoint.id}_#{port.name}" }
    end

    %w[
      flit_in flit_in_valid flit_in_ready
      flit_out flit_out_valid flit_out_ready
    ].map { |suffix| "#{endpoint.id}_#{suffix}" }
  end

  def power_plan_expect!(condition, code, path, message)
    unless condition
      raise FinepaperNoc::RtlHierarchyManifestError.new(code, path, message)
    end

    condition
  end

  def validate_domain_entity!(context, kind, id)
    context.entity_domain(kind, id, TIMING_ROLE)
    context.entity_domain(kind, id, SUPPLY_ROLE)
  end

  def validate_edge_elements!(edge, expected_from, expected_to)
    return if edge.fetch('fromElement') == expected_from &&
              edge.fetch('toElement') == expected_to

    reference = edge.fetch('edge')
    raise FinepaperNoc::DomainRtlContextError.new(
      'rtl_context.legacy_edge_mismatch', '/edgeBindings',
      "edge #{reference.fetch('kind')} #{reference.fetch('id')} differs from the legacy Mesh graph"
    )
  end

  def validate_domain_edge!(context, edge)
    edge_kind = edge.dig('edge', 'kind')
    edge_id = edge.dig('edge', 'id')
    from = edge.fetch('fromElement')
    to = edge.fetch('toElement')
    from_timing = context.entity_domain(from.fetch('kind'), from.fetch('id'),
                                        TIMING_ROLE)
    to_timing = context.entity_domain(to.fetch('kind'), to.fetch('id'),
                                      TIMING_ROLE)
    fifo_stage = context.edge_stage(edge_kind, edge_id, ASYNC_FIFO_RECIPE)
    timing_differs = from_timing.fetch('domain') != to_timing.fetch('domain')
    if timing_differs != !fifo_stage.nil?
      raise FinepaperNoc::DomainRtlContextError.new(
        'rtl_context.timing_stage_mismatch', '/edgeBindings',
        "edge #{edge_id} must contain exactly one async FIFO iff its timing Domains differ"
      )
    end

    edge.fetch('stages').each do |stage|
      if stage.key?('directions')
        stage.fetch('directions').each do |direction|
          validate_plan_recipe!(direction.fetch('recipe'),
                                direction.fetch('recipeKind'),
                                direction.fetch('parameters'), edge_id)
        end
      else
        validate_plan_recipe!(stage.fetch('recipe'),
                              stage.fetch('recipeKind'),
                              stage.fetch('parameters'), edge_id)
      end
    end
  end

  def validate_plan_recipe!(recipe, kind, parameters, edge_id)
    case recipe
    when ASYNC_FIFO_RECIPE
      raise "#{recipe} on #{edge_id} must be bidirectional" unless kind == 'bidirectional-stage'
      raise "#{recipe} on #{edge_id} has unexpected parameters" unless parameters.keys.sort == %w[fifo-depth metastability-stages]
      depth = parameters.dig('fifo-depth', 'value')
      stages = parameters.dig('metastability-stages', 'value')
      unless depth.is_a?(Integer) && depth.between?(2, 1024) && (depth & (depth - 1)).zero?
        raise FinepaperNoc::DomainRtlContextError.new(
          'rtl_context.invalid_fifo_depth', '/edgeBindings',
          "async FIFO depth on #{edge_id} must be a power of two in 2..1024"
        )
      end
      unless stages.is_a?(Integer) && stages.between?(2, 8)
        raise FinepaperNoc::DomainRtlContextError.new(
          'rtl_context.invalid_synchronizer_stages', '/edgeBindings',
          "async FIFO synchronizer stages on #{edge_id} must be in 2..8"
        )
      end
    when 'power-isolation'
      raise "#{recipe} on #{edge_id} must be bidirectional" unless kind == 'bidirectional-stage'
      raise "#{recipe} on #{edge_id} has unexpected parameters" unless parameters.empty?
    when 'power-level-shifter'
      raise "#{recipe} on #{edge_id} must be directional" unless kind == 'directional-stage'
      raise "#{recipe} on #{edge_id} has unexpected parameters" unless parameters.keys == ['translation-direction']
    else
      raise FinepaperNoc::DomainRtlContextError.new(
        'rtl_context.unknown_recipe', '/edgeBindings',
        "V3 RTL renderer does not implement recipe #{recipe} on #{edge_id}"
      )
    end
  end

  def domain_router_id(xp)
    "r-#{xp.x}-#{xp.y}"
  end

  def domain_link_id(connection)
    endpoints = domain_link_endpoints(connection)
    "link-#{domain_router_id(endpoints.fetch(0))}--#{domain_router_id(endpoints.fetch(1))}"
  end

  def domain_link_endpoints(connection)
    by_id = @noc.xps.to_h { |xp| [xp.id, xp] }
    [by_id.fetch(connection.from), by_id.fetch(connection.to)]
      .sort_by { |xp| [xp.y, xp.x] }
  end

  def build_domain_rtl_rendering(context)
    active_domains = context.domains_for_role(TIMING_ROLE).select do |domain|
      !domain.fetch('members').empty?
    end
    if active_domains.empty?
      raise FinepaperNoc::DomainRtlContextError.new(
        'rtl_context.missing_timing_domain', '/domainBindings',
        'the RTL graph requires at least one active timing Domain'
      )
    end

    clock_port_mode = active_domains.one? ? 'legacy-single-clock' : 'domain-token'
    clock_domains = active_domains.map do |domain|
      token = domain.fetch('token')
      reset_release_stages = context.parameter_value(
        domain, 'reset-release-stages', expected_type: 'integer'
      )
      unless reset_release_stages.between?(2, 8)
        raise FinepaperNoc::DomainRtlContextError.new(
          'rtl_context.invalid_reset_release_stages', '/domainBindings',
          "reset release stages for Domain #{domain.fetch('domain')} must be in 2..8"
        )
      end
      {
        'domain' => domain.fetch('domain'),
        'name' => domain.fetch('name'),
        'token' => token,
        'clockSignal' => clock_port_mode == 'legacy-single-clock' ?
          'clk' : "clk_#{token}",
        'resetSignal' => "rst_n_#{token}",
        'resetReleaseStages' => reset_release_stages
      }
    end
    clock_by_domain = clock_domains.to_h do |domain|
      [domain.fetch('domain'), domain]
    end

    entity_signals = context.entities.each_value.each_with_object({}) do |entry, result|
      element = entry.fetch('element')
      timing = context.entity_domain(
        element.fetch('kind'), element.fetch('id'), TIMING_ROLE
      )
      result[[element.fetch('kind'), element.fetch('id')]] =
        clock_by_domain.fetch(timing.fetch('domain'))
    end

    router_domain_to_rtl = @noc.xps.to_h do |xp|
      [domain_router_id(xp), xp.id]
    end
    router_signals = @noc.xps.to_h do |xp|
      [xp.id, entity_signals.fetch(['router', domain_router_id(xp)])]
    end
    endpoint_signals = @noc.endpoints.to_h do |endpoint|
      [endpoint.id, entity_signals.fetch(['endpoint', endpoint.id])]
    end

    router_traffic = {}
    @noc.connections.each do |connection|
      edge_id = domain_link_id(connection)
      FinepaperNoc::DomainRtlContext::ORIENTATIONS.each do |orientation|
        traffic = context.traffic('router-link', edge_id, orientation)
        producer = traffic.fetch('producer')
        consumer = traffic.fetch('consumer')
        unless producer.fetch('kind') == 'router' && consumer.fetch('kind') == 'router'
          raise FinepaperNoc::DomainRtlContextError.new(
            'rtl_context.invalid_router_traffic', '/edgeBindings',
            "Router Link #{edge_id} traffic endpoints must both be Routers"
          )
        end
        producer_id = router_domain_to_rtl.fetch(producer.fetch('id'))
        consumer_id = router_domain_to_rtl.fetch(consumer.fetch('id'))
        base = "link_#{producer_id}_to_#{consumer_id}"
        router_traffic[[producer_id, consumer_id]] = compile_traffic_bridge(
          context, 'router-link', edge_id, orientation, traffic, base,
          entity_signals
        )
      end
    end

    endpoint_attachments = @noc.endpoints.to_h do |endpoint|
      edge_id = endpoint.id
      router_to_endpoint = context.traffic(
        'endpoint-attachment', edge_id, 'from-to'
      )
      endpoint_to_router = context.traffic(
        'endpoint-attachment', edge_id, 'to-from'
      )
      [endpoint.id, {
        'routerToEndpoint' => compile_traffic_bridge(
          context, 'endpoint-attachment', edge_id, 'from-to',
          router_to_endpoint, "router_to_ni_#{endpoint.id}", entity_signals
        ),
        'endpointToRouter' => compile_traffic_bridge(
          context, 'endpoint-attachment', edge_id, 'to-from',
          endpoint_to_router, "ni_#{endpoint.id}_to_router", entity_signals
        )
      }]
    end

    {
      'clockPortMode' => clock_port_mode,
      'clockDomains' => clock_domains,
      'routerSignals' => router_signals,
      'endpointSignals' => endpoint_signals,
      'routerTraffic' => router_traffic,
      'endpointAttachments' => endpoint_attachments
    }
  end

  def compile_traffic_bridge(context, edge_kind, edge_id, orientation, traffic,
                             base, entity_signals)
    fifo = context.edge_stage(edge_kind, edge_id, ASYNC_FIFO_RECIPE)
    crossing = !fifo.nil?
    producer = traffic.fetch('producer')
    consumer = traffic.fetch('consumer')
    source_domain = entity_signals.fetch(
      [producer.fetch('kind'), producer.fetch('id')]
    )
    destination_domain = entity_signals.fetch(
      [consumer.fetch('kind'), consumer.fetch('id')]
    )
    bridge = {
      'edge' => {'kind' => edge_kind, 'id' => edge_id},
      'orientation' => orientation,
      'producer' => producer,
      'consumer' => consumer,
      'baseSignal' => base,
      'sourceSignal' => crossing ? "#{base}_src" : base,
      'destinationSignal' => crossing ? "#{base}_dst" : base,
      'sourceDomain' => source_domain,
      'destinationDomain' => destination_domain,
      'crossing' => crossing
    }
    return bridge unless crossing

    bridge.merge(
      'instance' => "u_cdc_#{base}",
      'fifoDepth' => context.parameter_value(
        fifo, 'fifo-depth', expected_type: 'integer'
      ),
      'synchronizerStages' => context.parameter_value(
        fifo, 'metastability-stages', expected_type: 'integer'
      )
    )
  end

  def xp_variants
    @noc.xps.each_with_object({}) do |xp, variants|
      signature = xp_signature(xp)
      key = xp_variant_key(xp)
      variants[key] ||= {
        xp: xp,
        signature: signature,
        folder: "xp_#{key}",
        module_name: "xp_router_#{key}"
      }
    end.values
  end

  def xp_signature(xp)
    counts = link_directions_for(xp).map { |link| link[:name] }.tally
    DIRECTION_ORDER.map do |dir|
      count = counts.fetch(dir[:name], 0)
      next '0' if count.zero?

      count == 1 ? dir[:abbr] : "#{dir[:abbr]}#{count}"
    end.join
  end

  def xp_variant_filename(variant)
    "#{file_token(@noc.name)}_#{variant[:folder]}.v"
  end

  def ni_features
    configured = @noc.parameters.fetch('ni_features', {})
    configured = configured.transform_keys(&:to_s)
    NI_FEATURE_DEFAULTS.merge(configured)
  end

  def ni_feature_enabled?(feature)
    ni_features.fetch(feature.to_s)
  end

  def ni_variants
    @noc.endpoints.each_with_object({}) do |endpoint, variants|
      xp = xp_for_endpoint(endpoint.id)
      signature = ni_signature(endpoint)
      key = ni_variant_key(endpoint)
      variants[key] ||= {
        xp: xp,
        endpoint: endpoint,
        signature: signature,
        folder: "ni_#{key}",
        module_name: "ni_bridge_#{key}"
      }
    end.values
  end

  def ni_signature(endpoint)
    slot = ni_endpoint_slot(endpoint)
    base = "#{slot[:protocol]}_#{slot[:role_code]}#{slot[:data_width]}"
    base = "#{base}_#{slot[:port_signature]}" unless slot[:port_signature] == 'flit'
    qos = slot[:config][:qos_enabled] ? 1 : 0
    "#{base}_buf#{slot[:config][:buffer_depth]}_q#{qos}_feat_#{ni_feature_signature}"
  end

  def ni_variant_filename(variant)
    "#{file_token(@noc.name)}_#{variant[:folder]}.v"
  end

  def ni_endpoint_slot(endpoint)
    {
      index: 0,
      id: endpoint.id,
      generic_id: 'ep0',
      type: endpoint.type,
      role_code: endpoint_role_code(endpoint),
      protocol: file_token(endpoint.protocol.downcase),
      data_width: endpoint.data_width,
      config: endpoint.config,
      ports: endpoint.ports,
      port_signature: endpoint_port_signature(endpoint)
    }
  end

  def link_directions_for(xp)
    links = @noc.connections.each_with_index.filter_map do |conn, index|
      next unless conn.from == xp.id || conn.to == xp.id

      neighbor_id = conn.from == xp.id ? conn.to : conn.from
      neighbor = @noc.xps.find { |candidate| candidate.id == neighbor_id }
      next unless neighbor

      direction = direction_for_connection(xp, neighbor, conn)
      next unless direction

      direction_meta = DIRECTION_ORDER.find { |dir| dir[:name] == direction }
      direction_meta.merge(neighbor: neighbor, connection: conn, index: index)
    end.sort_by { |link| [DIRECTION_ORDER.index { |dir| dir[:name] == link[:name] }, link[:index]] }

    add_unique_ports(links)
  end

  private

  def file_token(value)
    value.to_s.gsub(/[^0-9A-Za-z_]+/, '_')
  end

  def xp_variant_key(xp)
    signature = xp_signature(xp)
    return signature if xp.endpoints.size == 1

    "#{signature}_ep#{xp.endpoints.size}"
  end

  def ni_variant_key(endpoint)
    ni_signature(endpoint)
  end

  def xp_module_lookup
    @noc.xps.to_h { |xp| [xp.id, "xp_router_#{xp_variant_key(xp)}"] }
  end

  def ni_module_lookup
    @noc.endpoints.to_h do |endpoint|
      [endpoint.id, "ni_bridge_#{ni_variant_key(endpoint)}"]
    end
  end

  def xp_for_endpoint(endpoint_id)
    matches = @noc.xps.select { |xp| xp.endpoints.include?(endpoint_id) }
    unless matches.size == 1
      raise "Endpoint #{endpoint_id} must belong to exactly one Router"
    end

    matches.first
  end

  def xp_link_directions_by_id
    @noc.xps.to_h { |xp| [xp.id, link_directions_for(xp)] }
  end

  def direction_for_connection(xp, neighbor, conn)
    direction = normalize_direction(conn.dir)
    return conn.from == xp.id ? direction : OPPOSITE_DIRECTION[direction] if direction

    infer_direction_from_position(xp, neighbor)
  end

  def add_unique_ports(links)
    counts = links.map { |link| link[:name] }.tally
    seen = Hash.new(0)

    links.map do |link|
      count = counts.fetch(link[:name])
      ordinal = seen[link[:name]]
      seen[link[:name]] += 1
      port = count == 1 ? link[:abbr] : "#{link[:abbr]}#{ordinal}"
      link.merge(port: port)
    end
  end

  def normalize_direction(direction)
    return nil unless direction

    case direction.to_s.downcase
    when 'e', 'east' then :east
    when 'w', 'west' then :west
    when 'n', 'north' then :north
    when 's', 'south' then :south
    end
  end

  def infer_direction_from_position(xp, neighbor)
    return nil unless neighbor

    dx = neighbor.x - xp.x
    dy = neighbor.y - xp.y
    return :east if dx.positive?
    return :west if dx.negative?
    return :south if dy.positive?
    return :north if dy.negative?
  end

  def render_endpoint_templates(output_dir, ipcore_dir)
    @noc.endpoints.filter_map do |ep|
      next unless ep.template

      set_context(:@ep, ep)
      output_path = File.join(output_dir, "#{ep.id}.sv")
      RtlGenerator.new(@noc, ipcore_dir)
                  .render(File.basename(ep.template), output_path)
      output_path
    end
  end

  def ni_feature_signature
    signature = NI_FEATURE_CODES.filter_map do |feature, code|
      code if ni_feature_enabled?(feature)
    end.join
    signature.empty? ? 'none' : signature
  end

  def endpoint_role_code(endpoint)
    case endpoint.type
    when 'master' then 'm'
    when 'slave' then 's'
    else file_token(endpoint.type.downcase)
    end
  end

  def endpoint_port_signature(endpoint)
    return 'flit' unless endpoint.ports&.any?

    endpoint.ports.map do |port|
      width = file_token((port.width || 'scalar').downcase)
      "#{port.dir}_#{width}_#{file_token(port.name)}"
    end.join('_')
  end

  def emit_stub_modules(output_dir)
    stub_dir = File.join(@template_dir, 'stubs')
    return [] unless Dir.exist?(stub_dir)

    out_dir = File.join(output_dir, 'stubs')
    FileUtils.mkdir_p(out_dir)
    Dir[File.join(stub_dir, '*.sv')].sort.map do |path|
      output_path = File.join(out_dir, File.basename(path))
      FileUtils.cp(path, output_path)
      output_path
    end
  end

  def write_filelist(output_dir, source_paths, library_dirs: [])
    lines = library_dirs.flat_map do |dir|
      expanded = File.expand_path(dir)
      ["+incdir+#{expanded}", "-y #{expanded}"]
    end
    lines.concat(source_paths.map { |path| File.expand_path(path) })
    lines.uniq!
    File.write(File.join(output_dir, 'filelist.f'), "#{lines.join("\n")}\n")
  end

  def write_domain_evidence(output_dir, context, rendering)
    plan_name = "#{@noc.name}_domain_implementation.json"
    evidence_name = "#{@noc.name}_domain_implementation_evidence.json"
    plan_contents = JSON.pretty_generate(context.plan) + "\n"
    File.write(File.join(output_dir, plan_name), plan_contents)
    evidence = FinepaperNoc::DomainRtlEvidenceBuilder.build(
      context: context,
      domain_rendering: rendering,
      top_module: "#{@noc.name}_top",
      top_artifact: "#{@noc.name}_top.v",
      filelist_artifact: 'filelist.f',
      source_plan_artifact: plan_name,
      source_plan_contents: plan_contents
    )
    File.write(
      File.join(output_dir, evidence_name),
      JSON.pretty_generate(evidence) + "\n"
    )
  end

  def write_rtl_hierarchy(output_dir, context, rendering, router_modules,
                          endpoint_modules, link_directions,
                          logic_control_ports)
    builder = FinepaperNoc::RtlHierarchyManifestBuilder.new(
      context: context,
      design: @noc.name,
      top_module: "#{@noc.name}_top",
      top_artifact: "#{@noc.name}_top.v",
      expected_logic_control_ports: logic_control_ports
    )
    logic_control_ports.each do |control|
      builder.register_logic_control_port(
        control_id: control.fetch('id'),
        signal: control.fetch('signal'),
        source: control.fetch('source'),
        direction: control.fetch('direction')
      )
    end
    @noc.xps.each do |xp|
      builder.register_element(
        element: {'kind' => 'router', 'id' => domain_router_id(xp)},
        module_name: router_modules.fetch(xp.id),
        instance: "u_#{xp.id}"
      )
    end
    @noc.endpoints.each do |endpoint|
      builder.register_element(
        element: {'kind' => 'endpoint', 'id' => endpoint.id},
        module_name: endpoint_modules.fetch(endpoint.id),
        instance: "u_ni_#{endpoint.id}"
      )
    end
    rendering.fetch('clockDomains').each do |domain|
      builder.register_reset_synchronizer(
        timing_domain: domain.fetch('domain'),
        module_name: 'fp_reset_synchronizer',
        instance: "u_reset_#{domain.fetch('token')}",
        clock_signal: domain.fetch('clockSignal'),
        async_reset_signal: 'rst_n',
        local_reset_signal: domain.fetch('resetSignal')
      )
    end
    rendered_bridges(rendering).each do |record|
      producer_pins, consumer_pins = emitted_traffic_pins(
        record, link_directions
      )
      bridge = if record.fetch('crossing')
                 {
                   'module' => 'fp_async_ready_valid_fifo',
                   'instance' => record.fetch('instance'),
                   'placement' => 'infrastructure',
                   'sourcePins' => {
                     'payload' => 'src_payload_i',
                     'valid' => 'src_valid_i',
                     'ready' => 'src_ready_o'
                   },
                   'destinationPins' => {
                     'payload' => 'dst_payload_o',
                     'valid' => 'dst_valid_o',
                     'ready' => 'dst_ready_i'
                   }
                 }
               end
      builder.register_edge_direction(
        edge: record.fetch('edge'),
        orientation: record.fetch('orientation'),
        producer: record.fetch('producer'),
        consumer: record.fetch('consumer'),
        producer_pins: producer_pins,
        consumer_pins: consumer_pins,
        source_bundle: ready_valid_bundle(record.fetch('sourceSignal')),
        destination_bundle: ready_valid_bundle(
          record.fetch('destinationSignal')
        ),
        bridge: bridge
      )
    end
    manifest = builder.build
    File.write(
      File.join(output_dir, "#{@noc.name}_rtl_hierarchy.json"),
      JSON.pretty_generate(manifest) + "\n"
    )
  end

  def rendered_bridges(rendering)
    values = rendering.fetch('routerTraffic').values
    rendering.fetch('endpointAttachments').keys.sort.each do |endpoint_id|
      attachment = rendering.fetch('endpointAttachments').fetch(endpoint_id)
      values += %w[routerToEndpoint endpointToRouter].map do |direction|
        attachment.fetch(direction)
      end
    end
    values
  end

  def ready_valid_bundle(name)
    {
      'name' => name,
      'payload' => "#{name}_flit",
      'valid' => "#{name}_valid",
      'ready' => "#{name}_ready"
    }
  end

  def emitted_traffic_pins(record, link_directions)
    edge = record.fetch('edge')
    case edge.fetch('kind')
    when 'router-link'
      emitted_router_link_pins(record, link_directions)
    when 'endpoint-attachment'
      emitted_attachment_pins(record)
    else
      fail_hierarchy!(
        'rtl_hierarchy.unknown_emitted_edge_kind', '/emitter/edges',
        "cannot map emitted pins for edge kind #{edge.fetch('kind')}"
      )
    end
  end

  def emitted_router_link_pins(record, link_directions)
    edge = record.fetch('edge')
    producer = emitted_router_for!(record.fetch('producer'), edge)
    consumer = emitted_router_for!(record.fetch('consumer'), edge)
    producer_port = emitted_router_port!(
      producer, consumer, edge.fetch('id'), link_directions
    )
    consumer_port = emitted_router_port!(
      consumer, producer, edge.fetch('id'), link_directions
    )
    [
      ready_valid_pins("flit_out_#{producer_port}"),
      ready_valid_pins("flit_in_#{consumer_port}")
    ]
  end

  def emitted_router_for!(reference, edge)
    unless reference.fetch('kind') == 'router'
      fail_hierarchy!(
        'rtl_hierarchy.invalid_emitted_router_endpoint', '/emitter/edges',
        "Router Link #{edge.fetch('id')} traffic endpoint is not a Router"
      )
    end
    matches = @noc.xps.select do |xp|
      domain_router_id(xp) == reference.fetch('id')
    end
    unless matches.one?
      fail_hierarchy!(
        'rtl_hierarchy.ambiguous_emitted_router', '/emitter/routers',
        "Router element #{reference.fetch('id')} maps to #{matches.size} RTL instances"
      )
    end
    matches.first
  end

  def emitted_router_port!(router, neighbor, edge_id, link_directions)
    matches = link_directions.fetch(router.id).select do |link|
      link.fetch(:neighbor).id == neighbor.id &&
        domain_link_id(link.fetch(:connection)) == edge_id
    end
    unless matches.one?
      fail_hierarchy!(
        'rtl_hierarchy.ambiguous_emitted_router_port', '/emitter/routerPorts',
        "edge #{edge_id} maps to #{matches.size} ports on Router #{router.id}"
      )
    end
    matches.first[:port] || matches.first.fetch(:abbr)
  end

  def emitted_attachment_pins(record)
    edge = record.fetch('edge')
    endpoint_id = edge.fetch('id')
    router = xp_for_endpoint(endpoint_id)
    slots = router.endpoints.each_index.select do |index|
      router.endpoints.fetch(index) == endpoint_id
    end
    unless slots.one?
      fail_hierarchy!(
        'rtl_hierarchy.ambiguous_emitted_endpoint_slot',
        '/emitter/endpointSlots',
        "Endpoint #{endpoint_id} maps to #{slots.size} local Router ports"
      )
    end
    slot = slots.first
    router_reference = {'kind' => 'router', 'id' => domain_router_id(router)}
    endpoint_reference = {'kind' => 'endpoint', 'id' => endpoint_id}
    case record.fetch('orientation')
    when 'from-to'
      validate_emitted_traffic_endpoints!(
        record, router_reference, endpoint_reference
      )
      [
        ready_valid_pins("local#{slot}_flit_out"),
        ready_valid_pins('ep0_router_flit_out')
      ]
    when 'to-from'
      validate_emitted_traffic_endpoints!(
        record, endpoint_reference, router_reference
      )
      [
        ready_valid_pins('ep0_router_flit_in'),
        ready_valid_pins("local#{slot}_flit_in")
      ]
    else
      fail_hierarchy!(
        'rtl_hierarchy.invalid_emitted_orientation', '/emitter/edges',
        "unknown attachment orientation #{record.fetch('orientation')}"
      )
    end
  end

  def validate_emitted_traffic_endpoints!(record, producer, consumer)
    return if record.fetch('producer') == producer &&
              record.fetch('consumer') == consumer

    edge = record.fetch('edge')
    fail_hierarchy!(
      'rtl_hierarchy.emitted_traffic_endpoint_mismatch', '/emitter/edges',
      "edge #{edge.fetch('id')} orientation #{record.fetch('orientation')} " \
      'does not match its emitted product pins'
    )
  end

  def ready_valid_pins(payload)
    {
      'payload' => payload,
      'valid' => "#{payload}_valid",
      'ready' => "#{payload}_ready"
    }
  end

  def fail_hierarchy!(code, path, message)
    raise FinepaperNoc::RtlHierarchyManifestError.new(code, path, message)
  end

  def set_context(name, value)
    @noc.instance_variable_set(name, value)
  end

  def clear_context(*names)
    names.each do |name|
      @noc.remove_instance_variable(name) if @noc.instance_variable_defined?(name)
    end
  end
end
