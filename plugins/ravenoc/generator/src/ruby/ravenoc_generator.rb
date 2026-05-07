require 'erb'
require 'fileutils'
require 'json'
require 'open3'

class RaveNoCGenerator
  class GenerationError < StandardError; end

  REQUIRED_VENDOR_FILES = [
    'bus_arch_sv_pkg/amba_axi_pkg.sv',
    'src/include/ravenoc_axi_fnc.svh',
    'src/include/ravenoc_defines.svh',
    'src/include/ravenoc_structs.svh',
    'src/include/ravenoc_pkg.sv',
    'src/ni/axi_csr.sv',
    'src/ni/axi_slave_if.sv',
    'src/ni/router_wrapper.sv',
    'src/ni/async_gp_fifo.sv',
    'src/ni/cdc_pkt.sv',
    'src/ni/pkt_proc.sv',
    'src/router/fifo.sv',
    'src/router/output_module.sv',
    'src/router/router_if.sv',
    'src/router/router_ravenoc.sv',
    'src/router/rr_arbiter.sv',
    'src/router/vc_buffer.sv',
    'src/router/input_router.sv',
    'src/router/input_module.sv',
    'src/router/input_datapath.sv',
    'src/ravenoc.sv'
  ].freeze

  VENDOR_SOURCE_FILES = [
    'bus_arch_sv_pkg/amba_axi_pkg.sv',
    'src/include/ravenoc_pkg.sv',
    'src/ni/axi_csr.sv',
    'src/ni/axi_slave_if.sv',
    'src/ni/router_wrapper.sv',
    'src/ni/async_gp_fifo.sv',
    'src/ni/cdc_pkt.sv',
    'src/ni/pkt_proc.sv',
    'src/router/fifo.sv',
    'src/router/output_module.sv',
    'src/router/router_if.sv',
    'src/router/router_ravenoc.sv',
    'src/router/rr_arbiter.sv',
    'src/router/vc_buffer.sv',
    'src/router/input_router.sv',
    'src/router/input_module.sv',
    'src/router/input_datapath.sv',
    'src/ravenoc.sv'
  ].freeze

  VENDOR_COPY_FILES = (REQUIRED_VENDOR_FILES + VENDOR_SOURCE_FILES).uniq.freeze

  DEFAULTS = {
    'rows' => 2,
    'cols' => 2,
    'flit_data_width' => 32,
    'flit_type_width' => 2,
    'flit_buffer_depth' => 2,
    'virtual_channels' => 3,
    'routing_algorithm' => 'xy',
    'priority' => 'zero_high',
    'max_packet_flits' => 256,
    'axi_addr_width' => 32,
    'axi_data_width' => 32,
    'axi_cdc_required' => 'all',
    'bypass_cdc' => false
  }.freeze

  ROUTING_MAP = {
    'xy' => 'XYAlg',
    'yx' => 'YXAlg'
  }.freeze

  PRIORITY_MAP = {
    'zero_high' => 'ZeroHighPrior',
    'zero_low' => 'ZeroLowPrior'
  }.freeze

  DEFINE_NAMES = {
    'rows' => 'NOC_CFG_SZ_ROWS',
    'cols' => 'NOC_CFG_SZ_COLS',
    'flit_data_width' => 'FLIT_DATA_WIDTH',
    'flit_type_width' => 'FLIT_TP_WIDTH',
    'flit_buffer_depth' => 'FLIT_BUFF',
    'virtual_channels' => 'N_VIRT_CHN',
    'routing_algorithm' => 'ROUTING_ALG',
    'priority' => 'H_PRIORITY',
    'max_packet_flits' => 'MAX_SZ_PKT',
    'axi_addr_width' => 'AXI_ADDR_WIDTH',
    'axi_data_width' => 'AXI_DATA_WIDTH'
  }.freeze

  attr_reader :input_path, :output_dir, :template_dir, :vendor_dir

  def initialize(input_path:, output_dir:, template_dir:, vendor_dir:)
    @input_path = input_path
    @output_dir = output_dir
    @template_dir = template_dir
    @vendor_dir = vendor_dir
  end

  def generate
    graph = read_graph
    module_record = ravenoc_module_record(graph)
    parameters = generation_parameters(graph, module_record)
    validate_vendor!
    validate_parameters!(parameters)

    FileUtils.mkdir_p(output_dir)
    copy_vendor_sources!
    template_binding = binding_for(module_record, parameters)
    render('ravenoc_config.svh.erb', File.join(output_dir, 'ravenoc_config.svh'), template_binding)
    render('ravenoc_endpoint_dummy.sv.erb', File.join(output_dir, 'ravenoc_endpoint_dummy.sv'), template_binding)
    render('ravenoc_top.sv.erb', File.join(output_dir, 'ravenoc_top.sv'), template_binding)
    render('ravenoc_filelist.f.erb', File.join(output_dir, 'ravenoc_filelist.f'), template_binding)
    render('verify.sh.erb', File.join(output_dir, 'verify.sh'), template_binding)
    FileUtils.chmod(0o755, File.join(output_dir, 'verify.sh'))
    write_manifest(module_record, parameters)
    puts "Generated RaveNoC integration in #{output_dir}"
  end

  def validate
    graph = read_graph
    module_record = ravenoc_module_record(graph)
    parameters = generation_parameters(graph, module_record)
    validate_parameters!(parameters)
    true
  end

  private

  def read_graph
    data = JSON.parse(File.read(input_path))
    raise GenerationError, 'expected schema finepaper-plugin-graph-v1' unless data['schema'] == 'finepaper-plugin-graph-v1'

    data
  rescue Errno::ENOENT
    raise GenerationError, "input graph not found: #{input_path}"
  rescue JSON::ParserError => error
    raise GenerationError, "invalid JSON input: #{error.message}"
  end

  def ravenoc_module_record(graph)
    legacy = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveNoC'
    end
    raise GenerationError, "expected exactly one RaveNoC module, found #{legacy.size}" if legacy.size > 1
    return legacy.first if legacy.size == 1

    tiles = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveTile'
    end
    raise GenerationError, 'expected RaveNoC module or RaveTile graph, found none' if tiles.empty?

    internal_graph_module_record(graph, tiles)
  end

  def internal_graph_module_record(graph, tiles)
    coordinates = rectangular_coordinates(connection_tile_coordinates(tiles, graph.fetch('connections', []))) ||
                  rectangular_coordinates(logical_tile_coordinates(tiles)) ||
                  rectangular_coordinates(canvas_tile_coordinates(tiles)) ||
                  raise(GenerationError,
                        "RaveTile graph must be rectangular, found #{tiles.size} tiles")

    cols = coordinates.map { |(_, x, _)| x }.max + 1
    rows = coordinates.map { |(_, _, y)| y }.max + 1
    coordinate_by_id = coordinates.to_h { |id, x, y| [id, [x, y]] }
    validate_mesh_connections!(graph, coordinate_by_id)

    first_tile = tiles.min_by do |tile|
      x, y = coordinate_by_id.fetch(tile.fetch('id'))
      [y, x]
    end
    parameters = { 'rows' => rows, 'cols' => cols }
    {
      'id' => graph.fetch('name', 'ravenoc_internal_graph'),
      'type' => 'internal_graph',
      'parameters' => parameters,
      'tiles' => tiles,
      'endpoints' => endpoint_bindings(graph, coordinate_by_id, cols)
    }
  end

  def generation_parameters(graph, module_record)
    dimensions = module_record.fetch('parameters', {}).slice('rows', 'cols')
    ip_parameters = ip_instance_parameters(graph)
    dimensions.merge(ip_parameters)
  end

  def ip_instance_parameters(graph)
    ip_instance = graph.fetch('ip_instance', nil)
    unless ip_instance.is_a?(Hash)
      raise GenerationError, 'missing ip_instance'
    end
    unless ip_instance.fetch('plugin', nil) == 'finepaper.ravenoc'
      raise GenerationError, 'ip_instance plugin must be finepaper.ravenoc'
    end

    parameters = ip_instance.fetch('parameters', nil)
    raise GenerationError, 'ip_instance.parameters must be an object' unless parameters.is_a?(Hash)

    (DEFAULTS.keys - %w[rows cols]).each do |name|
      raise GenerationError, "missing IP instance parameter #{name}" unless parameters.key?(name)
    end
    parameters
  end

  def logical_tile_coordinates(tiles)
    return nil unless tiles.all? do |tile|
      params = tile.fetch('parameters', {})
      params.key?('mesh_col') && params.key?('mesh_row')
    end

    tiles.map do |tile|
      params = tile.fetch('parameters', {})
      x = params.fetch('mesh_col')
      y = params.fetch('mesh_row')
      unless x.is_a?(Integer) && y.is_a?(Integer)
        raise GenerationError, "RaveTile #{tile['id']} mesh_col/mesh_row must be integers"
      end

      [tile.fetch('id'), x, y]
    end
  end

  def canvas_tile_coordinates(tiles)
    raw = tiles.map do |tile|
      params = tile.fetch('parameters', {})
      x = params.fetch('x')
      y = params.fetch('y')
      unless x.is_a?(Numeric) && y.is_a?(Numeric)
        raise GenerationError, "RaveTile #{tile['id']} x/y must be numbers"
      end

      [tile.fetch('id'), x, y]
    end
    xs = raw.map { |(_, x, _)| x }.uniq.sort
    ys = raw.map { |(_, _, y)| y }.uniq.sort
    x_index = xs.each_with_index.to_h
    y_index = ys.each_with_index.to_h

    raw.map { |id, x, y| [id, x_index.fetch(x), y_index.fetch(y)] }
  end

  def connection_tile_coordinates(tiles, connections)
    tile_ids = tiles.map { |tile| tile.fetch('id') }
    adjacency = Hash.new { |hash, key| hash[key] = [] }

    connections.each do |connection|
      source = connection.fetch('source', {})
      target = connection.fetch('target', {})
      source_module = source.fetch('module', nil)
      target_module = target.fetch('module', nil)
      next unless tile_ids.include?(source_module) && tile_ids.include?(target_module)

      key = mesh_link_key(source_module,
                          source.fetch('port', nil),
                          target_module,
                          target.fetch('port', nil))
      next unless key

      dx, dy = key.fetch(:axis) == :east ? [1, 0] : [0, 1]
      from = key.fetch(:from)
      to = key.fetch(:to)
      adjacency[from] << [to, dx, dy]
      adjacency[to] << [from, -dx, -dy]
    end
    return nil if adjacency.empty?

    coordinates = {}
    tiles.sort_by do |tile|
      params = tile.fetch('parameters', {})
      [params.fetch('y', 0), params.fetch('x', 0), tile.fetch('id')]
    end.each do |tile|
      start = tile.fetch('id')
      next if coordinates.key?(start) || !adjacency.key?(start)

      coordinates[start] = [0, 0]
      frontier = [start]
      until frontier.empty?
        current = frontier.shift
        current_x, current_y = coordinates.fetch(current)

        adjacency.fetch(current, []).each do |next_id, dx, dy|
          candidate = [current_x + dx, current_y + dy]
          if coordinates.key?(next_id)
            return nil unless coordinates.fetch(next_id) == candidate
            next
          end

          coordinates[next_id] = candidate
          frontier << next_id
        end
      end
    end
    return nil unless coordinates.size == tile_ids.size

    min_x = coordinates.values.map(&:first).min
    min_y = coordinates.values.map(&:last).min
    coordinates.map { |id, (x, y)| [id, x - min_x, y - min_y] }
  end

  def rectangular_coordinates(coordinates)
    return nil unless coordinates

    cols = coordinates.map { |(_, x, _)| x }.max + 1
    rows = coordinates.map { |(_, _, y)| y }.max + 1
    return nil unless coordinates.size == rows * cols

    occupied = coordinates.map { |(_, x, y)| [x, y] }
    (0...rows).each do |row|
      (0...cols).each do |col|
        return nil unless occupied.include?([col, row])
      end
    end

    coordinates
  end

  def validate_mesh_connections!(graph, coordinate_by_id)
    expected = expected_mesh_links(coordinate_by_id)
    actual = actual_mesh_links(graph.fetch('connections', []), coordinate_by_id)

    missing = expected - actual
    unless missing.empty?
      raise GenerationError, "missing mesh link #{mesh_link_description(missing.first)}"
    end

    duplicates = actual.tally.select { |_, count| count > 1 }.keys
    unless duplicates.empty?
      raise GenerationError, "duplicate mesh link #{mesh_link_description(duplicates.first)}"
    end
  end

  def expected_mesh_links(coordinate_by_id)
    id_by_coordinate = coordinate_by_id.to_h { |id, coordinate| [coordinate, id] }
    coordinate_by_id.flat_map do |id, (x, y)|
      [
        mesh_link_key(id, 'east', id_by_coordinate[[x + 1, y]], 'west'),
        mesh_link_key(id, 'south', id_by_coordinate[[x, y + 1]], 'north')
      ].compact
    end
  end

  def actual_mesh_links(connections, coordinate_by_id)
    tile_ids = coordinate_by_id.keys
    connections.filter_map do |connection|
      source = connection.fetch('source', {})
      target = connection.fetch('target', {})
      source_module = source.fetch('module', nil)
      target_module = target.fetch('module', nil)
      next unless tile_ids.include?(source_module) && tile_ids.include?(target_module)

      key = mesh_link_key(source_module,
                          source.fetch('port', nil),
                          target_module,
                          target.fetch('port', nil))
      unless key && adjacent_coordinates?(coordinate_by_id.fetch(source_module),
                                          coordinate_by_id.fetch(target_module),
                                          key.fetch(:axis))
        raise GenerationError, "invalid mesh link #{connection.fetch('id', '<unnamed>')}"
      end

      key
    end
  end

  def mesh_link_key(source_module, source_port, target_module, target_port)
    return nil unless source_module && target_module

    case [source_port, target_port]
    when ['east', 'west']
      { from: source_module, to: target_module, axis: :east }
    when ['west', 'east']
      { from: target_module, to: source_module, axis: :east }
    when ['south', 'north']
      { from: source_module, to: target_module, axis: :south }
    when ['north', 'south']
      { from: target_module, to: source_module, axis: :south }
    end
  end

  def adjacent_coordinates?(from_coordinate, to_coordinate, axis)
    from_x, from_y = from_coordinate
    to_x, to_y = to_coordinate
    case axis
    when :east
      to_x == from_x + 1 && to_y == from_y
    when :south
      to_x == from_x && to_y == from_y + 1
    else
      false
    end
  end

  def mesh_link_description(link)
    "#{link.fetch(:from)} #{link.fetch(:axis)} #{link.fetch(:to)}"
  end

  def endpoint_bindings(graph, coordinate_by_id, cols)
    modules_by_id = graph.fetch('modules', []).to_h { |mod| [mod.fetch('id'), mod] }
    tile_ids = coordinate_by_id.keys
    endpoint_ids = modules_by_id.values.select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveEndpoint'
    end.map { |mod| mod.fetch('id') }

    bindings = []
    graph.fetch('connections', []).each do |connection|
      endpoint_id, tile_id = endpoint_connection(connection, endpoint_ids, tile_ids)
      next unless endpoint_id

      endpoint = modules_by_id.fetch(endpoint_id)
      col, row = coordinate_by_id.fetch(tile_id)
      slot = (row * cols) + col
      endpoint_artifact_id = module_artifact_id(endpoint)
      tile_artifact_id = module_artifact_id(modules_by_id.fetch(tile_id))
      bindings << {
        'id' => endpoint_artifact_id,
        'tile' => tile_artifact_id,
        'slot' => slot,
        'instance' => "u_ep#{slot}_#{safe_sv_identifier(endpoint_artifact_id)}_dummy"
      }
    end

    duplicate_slot = bindings.group_by { |binding| binding.fetch('slot') }.find { |_, items| items.size > 1 }
    raise GenerationError, "multiple RaveEndpoint connections target RaveNoC slot #{duplicate_slot.first}" if duplicate_slot

    bindings.sort_by { |binding| [binding.fetch('slot'), binding.fetch('id')] }
  end

  def endpoint_connection(connection, endpoint_ids, tile_ids)
    source = connection.fetch('source', {})
    target = connection.fetch('target', {})
    source_module = source.fetch('module', nil)
    target_module = target.fetch('module', nil)

    if endpoint_ids.include?(source_module) && source.fetch('port', nil) == 'noc' &&
       tile_ids.include?(target_module) && target.fetch('port', nil) == 'local'
      return [source_module, target_module]
    end

    if endpoint_ids.include?(target_module) && target.fetch('port', nil) == 'noc' &&
       tile_ids.include?(source_module) && source.fetch('port', nil) == 'local'
      return [target_module, source_module]
    end

    return [nil, nil] unless endpoint_ids.include?(source_module) || endpoint_ids.include?(target_module)

    raise GenerationError, "invalid RaveEndpoint connection #{connection.fetch('id', '<unnamed>')}"
  end

  def safe_sv_identifier(value)
    identifier = value.to_s.gsub(/[^a-zA-Z0-9_$]/, '_')
    identifier = "ep_#{identifier}" unless identifier.match?(/\A[a-zA-Z_]/)
    identifier
  end

  def module_artifact_id(mod)
    params = mod.fetch('parameters', {})
    value = params.fetch('external_id', mod.fetch('id'))
    safe_sv_identifier(value)
  end

  def validate_vendor!
    missing = REQUIRED_VENDOR_FILES.find { |relative| !File.file?(File.join(vendor_dir, relative)) }
    return unless missing

    raise GenerationError,
          "RaveNoC vendor source is missing or incomplete. Run: git submodule update --init --recursive. Missing: #{missing}"
  end

  def positive_integer!(parameters, name)
    value = parameters[name]
    raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

    value
  end

  def validate_parameters!(parameters)
    rows = positive_integer!(parameters, 'rows')
    cols = positive_integer!(parameters, 'cols')
    raise GenerationError, '1x1 is not a legal RaveNoC mesh' if rows == 1 && cols == 1

    buffer_depth = positive_integer!(parameters, 'flit_buffer_depth')
    raise GenerationError, 'flit_buffer_depth must be a power of two' unless (buffer_depth & (buffer_depth - 1)).zero?

    %w[flit_data_width flit_type_width virtual_channels max_packet_flits axi_addr_width axi_data_width].each do |name|
      positive_integer!(parameters, name)
    end
    unless [32, 64].include?(parameters.fetch('flit_data_width'))
      raise GenerationError, 'flit_data_width must be 32 or 64'
    end
    raise GenerationError, 'flit_type_width must be 2' unless parameters.fetch('flit_type_width') == 2
    unless (1..32).include?(parameters.fetch('virtual_channels'))
      raise GenerationError, 'virtual_channels must be 1-32'
    end
    unless parameters.fetch('axi_data_width') == parameters.fetch('flit_data_width')
      raise GenerationError, 'axi_data_width must equal flit_data_width'
    end

    raise GenerationError, 'routing_algorithm must be xy or yx' unless ROUTING_MAP.key?(parameters['routing_algorithm'])
    raise GenerationError, 'priority must be zero_high or zero_low' unless PRIORITY_MAP.key?(parameters['priority'])
    validate_axi_cdc_required!(parameters)
  end

  def validate_axi_cdc_required!(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase.delete('_')
    return if %w[all none].include?(value)
    return if value.match?(/\A[01]+\z/) && value.length == noc_size

    raise GenerationError, "axi_cdc_required must be all, none, or a #{noc_size}-bit binary mask"
  end

  def define_values(parameters)
    DEFINE_NAMES.to_h do |parameter_name, define_name|
      value = parameters.fetch(parameter_name)
      value = ROUTING_MAP.fetch(value) if parameter_name == 'routing_algorithm'
      value = PRIORITY_MAP.fetch(value) if parameter_name == 'priority'
      [define_name, value]
    end
  end

  def axi_cdc_literal(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase
    return "{#{noc_size}{1'b1}}" if value == 'all'
    return "{#{noc_size}{1'b0}}" if value == 'none'

    "#{noc_size}'b#{value.delete('_')}"
  end

  def bypass_cdc_literal(parameters)
    parameters.fetch('bypass_cdc') ? "1'b1" : "1'b0"
  end

  def vendor_files
    VENDOR_SOURCE_FILES
  end

  def copy_vendor_sources!
    VENDOR_COPY_FILES.each do |relative|
      source = File.join(vendor_dir, relative)
      destination = File.join(output_dir, relative)
      FileUtils.mkdir_p(File.dirname(destination))
      FileUtils.cp(source, destination)
    end
  end

  def render(template_name, output_path, template_binding)
    template = File.read(File.join(template_dir, template_name))
    File.write(output_path, ERB.new(template, trim_mode: '-').result(template_binding))
  end

  def binding_for(module_record, parameters)
    define_values = define_values(parameters)
    axi_cdc_literal = axi_cdc_literal(parameters)
    bypass_cdc_literal = bypass_cdc_literal(parameters)
    vendor_files = vendor_files()
    output_dir = self.output_dir
    endpoint_bindings = module_record.fetch('endpoints', [])
    endpoint_by_slot = endpoint_bindings.to_h { |endpoint| [endpoint.fetch('slot'), endpoint] }
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    binding
  end

  def source_commit
    stdout, _stderr, status = Open3.capture3('git', '-C', vendor_dir, 'rev-parse', 'HEAD')
    status.success? ? stdout.strip : 'unknown'
  rescue StandardError
    'unknown'
  end

  def write_manifest(module_record, parameters)
    manifest = {
      plugin: 'finepaper.ravenoc',
      source: {
        repository: 'https://github.com/aignacio/ravenoc.git',
        commit: source_commit
      },
      module: {
        id: module_record['id'],
        type: module_record['type'],
        tiles: module_record.fetch('tiles', []).size,
        endpoints: module_record.fetch('endpoints', [])
      },
      parameters: parameters,
      verification: {
        command: 'bash verify.sh'
      }
    }
    File.write(File.join(output_dir, 'manifest.json'), "#{JSON.pretty_generate(manifest)}\n")
  end
end
