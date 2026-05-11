require 'json'

class OpenNoCGenerator
  class GenerationError < StandardError; end

  GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
  IPCORE_ID = 'finepaper.opennoc'.freeze
  XP_TYPE = 'OpenNoCXP'.freeze

  AGENT_TYPE_TO_ENUM = {
    'OpenNoCRNF' => 'RNF',
    'OpenNoCRNI' => 'RNI',
    'OpenNoCHNF' => 'HNF',
    'OpenNoCHNI' => 'HNI',
    'OpenNoCSNF' => 'SNF'
  }.freeze

  AGENT_RTL_DIR = {
    'OpenNoCRNI' => 'rni',
    'OpenNoCHNF' => 'hnf',
    'OpenNoCHNI' => 'hni',
    'OpenNoCSNF' => 'snf'
  }.freeze

  DEFAULTS = {
    'req_flit_width' => 128,
    'rsp_flit_width' => 64,
    'dat_flit_width' => 256,
    'snp_flit_width' => 128
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
    build_model(graph)
    raise GenerationError, 'OpenNoC artifact generation is not implemented yet'
  end

  def validate
    graph = read_graph
    build_model(graph)
    true
  end

  def read_graph
    data = JSON.parse(File.read(input_path))
    raise GenerationError, "expected schema #{GRAPH_SCHEMA}" unless data['schema'] == GRAPH_SCHEMA
    raise GenerationError, "expected ipcore #{IPCORE_ID}" unless data['ipcore'] == IPCORE_ID

    data.delete('errors')
    data
  rescue Errno::ENOENT
    raise GenerationError, "input graph not found: #{input_path}"
  rescue JSON::ParserError => error
    raise GenerationError, "invalid JSON input: #{error.message}"
  end

  def global_parameters(graph)
    ipcore_state = graph.fetch('ipcore_state', nil)
    raise GenerationError, 'missing ipcore_state' unless ipcore_state.is_a?(Array)

    states = ipcore_state.select do |state|
      state.is_a?(Hash) && state.fetch('ipcore', nil) == IPCORE_ID
    end
    raise GenerationError, 'missing ipcore_state' if states.empty?
    if states.size > 1
      raise GenerationError, "expected exactly one #{IPCORE_ID} ipcore_state, found #{states.size}"
    end

    state = states.first.fetch('state', nil)
    raise GenerationError, 'ipcore_state.state must be an object' unless state.is_a?(Hash)

    parameters = state.fetch('global_parameters', nil)
    raise GenerationError, 'ipcore_state.state.global_parameters must be an object' unless parameters.is_a?(Hash)

    DEFAULTS.merge(parameters)
  end

  def build_model(graph)
    parameters = global_parameters(graph)
    validate_parameters!(parameters)

    modules = graph.fetch('modules', [])
    xps = modules.select { |mod| mod['ipcore'] == IPCORE_ID && mod['type'] == XP_TYPE }
    raise GenerationError, 'expected at least one OpenNoCXP module, found none' if xps.empty?

    coordinates = infer_coordinates(xps, graph.fetch('connections', []))
    validate_coordinate_range!(coordinates)
    coordinate_by_id = coordinates.to_h { |id, x, y| [id, [x, y]] }
    rows = coordinates.map { |(_, _, y)| y }.max + 1
    cols = coordinates.map { |(_, x, _)| x }.max + 1

    validate_mesh_connections!(graph.fetch('connections', []), coordinate_by_id, modules)
    attachments = agent_bindings(graph.fetch('connections', []), modules, coordinate_by_id.keys)
    mesh_json = projected_mesh_json(xps, coordinate_by_id, attachments)

    {
      graph: graph,
      parameters: parameters,
      coordinate_by_id: coordinate_by_id,
      rows: rows,
      cols: cols,
      attachments: attachments,
      mesh_json: mesh_json
    }
  end

  private

  def validate_parameters!(parameters)
    DEFAULTS.each_key { |name| positive_integer!(parameters, name) }
  end

  def positive_integer!(parameters, name)
    value = parameters[name]
    raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

    value
  end

  def infer_coordinates(xps, connections)
    coordinates = rectangular_coordinates(logical_xp_coordinates(xps)) ||
                  rectangular_coordinates(connection_xp_coordinates(xps, connections)) ||
                  rectangular_coordinates(canvas_xp_coordinates(xps))
    unless coordinates
      raise GenerationError, "OpenNoCXP graph must be rectangular, found #{xps.size} XPs"
    end

    coordinates
  end

  def logical_xp_coordinates(xps)
    return nil unless xps.all? do |xp|
      params = xp.fetch('parameters', {})
      params.key?('mesh_col') && params.key?('mesh_row')
    end

    xps.map do |xp|
      params = xp.fetch('parameters', {})
      x = params.fetch('mesh_col')
      y = params.fetch('mesh_row')
      unless x.is_a?(Integer) && y.is_a?(Integer)
        raise GenerationError, "OpenNoCXP #{artifact_id(xp)} mesh_col/mesh_row must be integers"
      end

      [xp.fetch('id'), x, y]
    end
  end

  def connection_xp_coordinates(xps, connections)
    xp_ids = xps.map { |xp| xp.fetch('id') }
    adjacency = Hash.new { |hash, key| hash[key] = [] }

    connections.each do |connection|
      source = connection.fetch('source', {})
      target = connection.fetch('target', {})
      source_module = source.fetch('module', nil)
      target_module = target.fetch('module', nil)
      next unless xp_ids.include?(source_module) && xp_ids.include?(target_module)

      key = mesh_link_key(source_module, source.fetch('port', nil), target_module, target.fetch('port', nil))
      next unless key

      dx, dy = key.fetch(:axis) == :east ? [1, 0] : [0, 1]
      from = key.fetch(:from)
      to = key.fetch(:to)
      adjacency[from] << [to, dx, dy]
      adjacency[to] << [from, -dx, -dy]
    end
    return nil if adjacency.empty?

    coordinates = {}
    xps.sort_by { |xp| xp_sort_key(xp) }.each do |xp|
      start = xp.fetch('id')
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
    return nil unless coordinates.size == xp_ids.size

    min_x = coordinates.values.map(&:first).min
    min_y = coordinates.values.map(&:last).min
    coordinates.map { |id, (x, y)| [id, x - min_x, y - min_y] }
  end

  def canvas_xp_coordinates(xps)
    raw = xps.map do |xp|
      params = xp.fetch('parameters', {})
      x = params.fetch('x', nil)
      y = params.fetch('y', nil)
      unless x.is_a?(Numeric) && y.is_a?(Numeric)
        raise GenerationError, "OpenNoCXP #{artifact_id(xp)} x/y must be numbers"
      end

      [xp.fetch('id'), x, y]
    end
    xs = raw.map { |(_, x, _)| x }.uniq.sort
    ys = raw.map { |(_, _, y)| y }.uniq.sort
    x_index = xs.each_with_index.to_h
    y_index = ys.each_with_index.to_h

    raw.map { |id, x, y| [id, x_index.fetch(x), y_index.fetch(y)] }
  end

  def rectangular_coordinates(coordinates)
    return nil unless coordinates

    return nil if coordinates.any? { |(_, x, y)| x.negative? || y.negative? }

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

  def validate_coordinate_range!(coordinates)
    bad = coordinates.find { |(_, x, y)| !(0..7).include?(x) || !(0..7).include?(y) }
    return unless bad

    raise GenerationError, "OpenNoCXP #{bad.first} coordinate must be in range 0..7"
  end

  def validate_mesh_connections!(connections, coordinate_by_id, modules)
    module_by_id = modules.to_h { |mod| [mod.fetch('id'), mod] }
    expected = expected_mesh_links(coordinate_by_id)
    actual = actual_mesh_links(connections, coordinate_by_id)

    missing = expected - actual
    unless missing.empty?
      raise GenerationError, "missing mesh link #{mesh_link_description(missing.first, module_by_id)}"
    end

    duplicate = actual.tally.find { |_, count| count > 1 }
    return unless duplicate

    raise GenerationError, "duplicate mesh link #{mesh_link_description(duplicate.first, module_by_id)}"
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
    xp_ids = coordinate_by_id.keys
    connections.filter_map do |connection|
      source = connection.fetch('source', {})
      target = connection.fetch('target', {})
      source_module = source.fetch('module', nil)
      target_module = target.fetch('module', nil)
      next unless xp_ids.include?(source_module) && xp_ids.include?(target_module)

      key = mesh_link_key(source_module, source.fetch('port', nil), target_module, target.fetch('port', nil))
      unless key && adjacent_coordinates?(coordinate_by_id.fetch(key.fetch(:from)),
                                          coordinate_by_id.fetch(key.fetch(:to)),
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

  def mesh_link_description(link, module_by_id)
    from = artifact_id(module_by_id.fetch(link.fetch(:from)))
    to = artifact_id(module_by_id.fetch(link.fetch(:to)))
    "#{from} #{link.fetch(:axis)} #{to}"
  end

  def agent_bindings(connections, modules, xp_ids)
    module_by_id = modules.to_h { |mod| [mod.fetch('id'), mod] }
    agent_ids = modules.select do |mod|
      mod['ipcore'] == IPCORE_ID && AGENT_TYPE_TO_ENUM.key?(mod['type'])
    end.map { |mod| mod.fetch('id') }
    bindings = []

    connections.each do |connection|
      binding = agent_connection(connection, agent_ids, xp_ids)
      bindings << binding if binding
    end

    duplicate_slot = bindings.group_by { |binding| [binding.fetch(:xp_id), binding.fetch(:slot)] }
                             .find { |_, items| items.size > 1 }
    if duplicate_slot
      xp_id, slot = duplicate_slot.first
      raise GenerationError, "multiple OpenNoC agents connect to #{artifact_id(module_by_id.fetch(xp_id))}.#{slot}"
    end

    agent_ids.each do |agent_id|
      count = bindings.count { |binding| binding.fetch(:agent_id) == agent_id }
      next if count == 1

      agent = module_by_id.fetch(agent_id)
      raise GenerationError, "#{agent.fetch('type')} #{agent_id} must connect to exactly one XP slot"
    end

    bindings.each do |binding|
      agent = module_by_id.fetch(binding.fetch(:agent_id))
      binding[:agent_type] = agent.fetch('type')
      binding[:agent_enum] = AGENT_TYPE_TO_ENUM.fetch(agent.fetch('type'))
    end

    bindings.sort_by { |binding| [binding.fetch(:xp_id), binding.fetch(:slot), binding.fetch(:agent_id)] }
  end

  def agent_connection(connection, agent_ids, xp_ids)
    source = connection.fetch('source', {})
    target = connection.fetch('target', {})
    source_module = source.fetch('module', nil)
    target_module = target.fetch('module', nil)
    source_is_agent = agent_ids.include?(source_module)
    target_is_agent = agent_ids.include?(target_module)
    source_is_xp = xp_ids.include?(source_module)
    target_is_xp = xp_ids.include?(target_module)
    involves_agent = source_is_agent || target_is_agent
    involves_xp = source_is_xp || target_is_xp

    return nil unless involves_agent || involves_xp
    return nil if source_is_xp && target_is_xp

    if source_is_agent && target_is_xp &&
       source.fetch('port', nil) == 'chi' && %w[p0 p1].include?(target.fetch('port', nil))
      return { agent_id: source_module, xp_id: target_module, slot: target.fetch('port') }
    end

    if target_is_agent && source_is_xp &&
       target.fetch('port', nil) == 'chi' && %w[p0 p1].include?(source.fetch('port', nil))
      return { agent_id: target_module, xp_id: source_module, slot: source.fetch('port') }
    end

    raise GenerationError, "invalid OpenNoC agent connection #{connection.fetch('id', '<unnamed>')}"
  end

  def projected_mesh_json(xps, coordinate_by_id, attachments)
    attachment_by_slot = attachments.to_h { |binding| [[binding.fetch(:xp_id), binding.fetch(:slot)], binding] }
    module_by_id = xps.to_h { |xp| [xp.fetch('id'), xp] }

    coordinate_by_id.keys.sort_by { |id| coordinate_by_id.fetch(id).reverse }.to_h do |xp_id|
      x, y = coordinate_by_id.fetch(xp_id)
      xp = module_by_id.fetch(xp_id)
      [artifact_id(xp), {
        'X' => x,
        'Y' => y,
        'P0' => agent_enum(attachment_by_slot[[xp_id, 'p0']]),
        'P1' => agent_enum(attachment_by_slot[[xp_id, 'p1']])
      }]
    end
  end

  def agent_enum(binding)
    return 'NONE' unless binding

    binding.fetch(:agent_enum)
  end

  def artifact_id(mod)
    params = mod.fetch('parameters', {})
    safe_identifier(params.fetch('external_id', mod.fetch('id')))
  end

  def safe_identifier(value)
    identifier = value.to_s.gsub(/[^a-zA-Z0-9_$]/, '_')
    identifier = "opennoc_#{identifier}" unless identifier.match?(/\A[a-zA-Z_]/)
    identifier
  end

  def xp_sort_key(xp)
    params = xp.fetch('parameters', {})
    [params.fetch('y', 0), params.fetch('x', 0), xp.fetch('id')]
  end
end
