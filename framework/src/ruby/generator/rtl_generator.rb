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

    xp_variants.each do |variant|
      xp = variant[:xp]
      set_context(:@xp, xp)
      set_context(:@xp_module_name, variant[:module_name])
      set_context(:@xp_variant_signature, variant[:signature])
      set_context(:@xp_port_directions, link_directions_for(xp))
      set_context(:@xp_local_port_count, xp.endpoints.size)
      render('xp.sv.erb', File.join(output_dir, variant[:folder], xp_variant_filename(variant)))
    end

    @noc.xps.each do |xp|
      next if xp.endpoints.empty?

      set_context(:@xp, xp)
      render('ni.sv.erb', File.join(output_dir, "ni_xp_#{xp.id}.sv"))
    end

    render_endpoint_templates(output_dir, ipcore_dir) if ipcore_dir
    render('top.v.erb', File.join(output_dir, "#{@noc.name}_top.v"))
  ensure
    clear_context(:@xp, :@xp_module_name, :@xp_variant_signature,
                  :@xp_port_directions, :@xp_local_port_count,
                  :@xp_module_lookup, :@xp_link_directions_by_id)
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

  def xp_module_lookup
    @noc.xps.to_h { |xp| [xp.id, "xp_router_#{xp_variant_key(xp)}"] }
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

  def set_context(name, value)
    @noc.instance_variable_set(name, value)
  end

  def clear_context(*names)
    names.each do |name|
      @noc.remove_instance_variable(name) if @noc.instance_variable_defined?(name)
    end
  end
end
