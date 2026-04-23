require 'erb'
require 'fileutils'

class RtlGenerator
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
    FileUtils.mkdir_p(output_dir)

    lookup = xp_module_lookup
    set_context(:@xp_module_lookup, lookup)
    set_context(:@xp_link_directions_by_id, xp_link_directions_by_id)
    set_context(:@ni_module_lookup, ni_module_lookup)
    set_context(:@ni_features, ni_features)

    xp_variants.each do |variant|
      xp = variant[:xp]
      set_context(:@xp, xp)
      set_context(:@xp_module_name, variant[:module_name])
      set_context(:@xp_variant_signature, variant[:signature])
      set_context(:@xp_port_directions, link_directions_for(xp))
      set_context(:@xp_local_port_count, xp.endpoints.size)
      render('xp.sv.erb', File.join(output_dir, variant[:folder], xp_variant_filename(variant)))
    end

    ni_variants.each do |variant|
      xp = variant[:xp]
      set_context(:@xp, xp)
      set_context(:@ni_module_name, variant[:module_name])
      set_context(:@ni_variant_signature, variant[:signature])
      set_context(:@ni_endpoint_slots, ni_endpoint_slots(xp))
      render('ni.sv.erb', File.join(output_dir, variant[:folder], ni_variant_filename(variant)))
    end

    emit_stub_modules(output_dir)
    render_endpoint_templates(output_dir, ipcore_dir) if ipcore_dir
    render('top.v.erb', File.join(output_dir, "#{@noc.name}_top.v"))
  ensure
    clear_context(:@xp, :@xp_module_name, :@xp_variant_signature,
                  :@xp_port_directions, :@xp_local_port_count,
                  :@xp_module_lookup, :@xp_link_directions_by_id,
                  :@ni_module_lookup, :@ni_module_name,
                  :@ni_variant_signature, :@ni_endpoint_slots,
                  :@ni_features)
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
    @noc.xps.each_with_object({}) do |xp, variants|
      next if xp.endpoints.empty?

      signature = ni_signature(xp)
      key = ni_variant_key(xp)
      variants[key] ||= {
        xp: xp,
        signature: signature,
        folder: "ni_#{key}",
        module_name: "ni_bridge_#{key}"
      }
    end.values
  end

  def ni_signature(xp)
    slot_signature = ni_endpoint_slots(xp).map do |slot|
      base = "#{slot[:protocol]}_#{slot[:role_code]}#{slot[:data_width]}"
      slot[:port_signature] == 'flit' ? base : "#{base}_#{slot[:port_signature]}"
    end.join('_')

    "#{slot_signature}_feat_#{ni_feature_signature}"
  end

  def ni_variant_filename(variant)
    "#{file_token(@noc.name)}_#{variant[:folder]}.v"
  end

  def ni_endpoint_slots(xp)
    endpoint_by_id = @noc.endpoints.to_h { |ep| [ep.id, ep] }
    xp.endpoints.each_with_index.map do |ep_id, index|
      ep = endpoint_by_id.fetch(ep_id)
      {
        index: index,
        id: ep.id,
        generic_id: "ep#{index}",
        type: ep.type,
        role_code: endpoint_role_code(ep),
        protocol: file_token(ep.protocol.downcase),
        data_width: ep.data_width,
        config: ep.config,
        ports: ep.ports,
        port_signature: endpoint_port_signature(ep)
      }
    end
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

  def ni_variant_key(xp)
    ni_signature(xp)
  end

  def xp_module_lookup
    @noc.xps.to_h { |xp| [xp.id, "xp_router_#{xp_variant_key(xp)}"] }
  end

  def ni_module_lookup
    @noc.xps.each_with_object({}) do |xp, lookup|
      next if xp.endpoints.empty?

      lookup[xp.id] = "ni_bridge_#{ni_variant_key(xp)}"
    end
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
    @noc.endpoints.each do |ep|
      next unless ep.template

      set_context(:@ep, ep)
      RtlGenerator.new(@noc, ipcore_dir)
                  .render(File.basename(ep.template), File.join(output_dir, "#{ep.id}.sv"))
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
    return unless Dir.exist?(stub_dir)

    out_dir = File.join(output_dir, 'stubs')
    FileUtils.mkdir_p(out_dir)
    Dir[File.join(stub_dir, '*.sv')].each do |path|
      FileUtils.cp(path, File.join(out_dir, File.basename(path)))
    end
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
