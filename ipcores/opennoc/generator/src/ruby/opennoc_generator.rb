# Migration-only legacy schema handling. Not used by normal runtime loading.
require 'json'
require 'erb'
require 'fileutils'
require 'open3'
require 'tmpdir'

class OpenNoCGenerator
  class GenerationError < StandardError; end

  GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
  IPCRAFT_PROJECT_SCHEMA = 'ipcraft.project.v1'.freeze
  LEGACY_IPCRAFT_PROJECT_SCHEMA = 'ipcraft.noc.project.v1'.freeze
  SUPPORTED_SCHEMAS = [IPCRAFT_PROJECT_SCHEMA, LEGACY_IPCRAFT_PROJECT_SCHEMA, GRAPH_SCHEMA].freeze
  IPCORE_ID = 'finepaper.opennoc'.freeze
  PROJECT_STATE_SCHEMA = 'finepaper.opennoc-project-state-v1'.freeze
  XP_TYPE = 'OpenNoCXP'.freeze
  FABRIC_TYPE = 'OpenNoC'.freeze

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

  REQUIRED_VENDOR_PATHS = [
    'LICENSE',
    'tools/mesh_generator/mesh_gen.py',
    'tools/mesh_generator/template/mesh_wrapper.j2',
    'tools/mesh_generator/chi_xp_node.sv',
    'rtl/misc/chi_xp_channel.v',
    'rtl/include/chie_defines.v',
    'rtl/include/rni_param.v',
    'rtl/include/hnf_param.v',
    'rtl/include/hni_param.v',
    'rtl/include/snf_param.v',
    'rtl/src/rni/rni.v',
    'rtl/src/hnf/hnf.v',
    'rtl/src/hni/hni.v',
    'rtl/src/snf/snf.v'
  ].freeze

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
    model = build_model(graph)
    validate_vendor!

    FileUtils.mkdir_p(output_dir)
    mesh_config = File.join(output_dir, 'opennoc_mesh.json')
    File.write(mesh_config, JSON.pretty_generate(model.fetch(:mesh_json)))

    wrapper = "mesh_wrapper_#{model.fetch(:cols)}x#{model.fetch(:rows)}.sv"
    run_mesh_generator(mesh_config, wrapper)
    copy_vendor_artifacts(model, wrapper)
    render_outputs(model, wrapper)

    puts "Generated OpenNoC mesh integration in #{output_dir}"
  end

  def validate
    graph = read_graph
    build_model(graph)
    true
  end

  def read_graph
    data = JSON.parse(File.read(input_path))
    unless SUPPORTED_SCHEMAS.include?(data['schema'])
      raise GenerationError,
            "expected schema #{IPCRAFT_PROJECT_SCHEMA} " \
            "(or #{LEGACY_IPCRAFT_PROJECT_SCHEMA}/#{GRAPH_SCHEMA} for legacy compatibility)"
    end
    data = normalize_project_design(data) if data['schema'] == IPCRAFT_PROJECT_SCHEMA
    data = normalize_legacy_ipcraft_project(data) if data['schema'] == LEGACY_IPCRAFT_PROJECT_SCHEMA
    raise GenerationError, "expected ipcore #{IPCORE_ID}" unless data['ipcore'] == IPCORE_ID

    data.delete('errors')
    data
  rescue Errno::ENOENT
    raise GenerationError, "input graph not found: #{input_path}"
  rescue JSON::ParserError => error
    raise GenerationError, "invalid JSON input: #{error.message}"
  end

  def normalize_project_design(data)
    packages = project_design_required_array(data, 'packages')
    components = project_design_required_array(data, 'components')
    interfaces = project_design_optional_array(data, 'interfaces')
    connections = project_design_optional_array(data, 'connections')
    package_ref = project_design_package_ref(packages)
    owner = project_design_owner_component(components)
    instance_id = owner ? required_project_design_string(owner, 'id') : required_project_design_string(data, 'id')
    opennoc_components = project_design_opennoc_components(components)
    modules = normalize_project_design_components(opennoc_components, instance_id)

    {
      'schema' => GRAPH_SCHEMA,
      'name' => required_project_design_string(data, 'name'),
      'ipcore' => IPCORE_ID,
      'instance' => instance_id,
      'ipcore_state' => [project_design_state(package_ref, owner, instance_id)],
      'modules' => modules,
      'connections' => normalize_project_design_connections(connections, interfaces, modules)
    }
  end

  def project_design_package_ref(packages)
    package = packages.find do |item|
      item.is_a?(Hash) && item.fetch('id', nil) == IPCORE_ID
    end
    raise GenerationError, "ipcraft.project.v1 packages must include #{IPCORE_ID}" unless package

    "#{IPCORE_ID}@#{required_project_design_string(package, 'version')}"
  end

  def project_design_owner_component(components)
    project_design_opennoc_components(components).find do |component|
      project_design_component_type(component) == FABRIC_TYPE
    end
  end

  def project_design_opennoc_components(components)
    components.select do |component|
      next false unless component.is_a?(Hash)

      package_ref = component.fetch('packageRef', nil).to_s
      type = project_design_component_type(component)
      package_ref == IPCORE_ID || package_ref.start_with?("#{IPCORE_ID}@") ||
        [FABRIC_TYPE, XP_TYPE, *AGENT_TYPE_TO_ENUM.keys].include?(type)
    end
  end

  def normalize_project_design_components(components, instance_id)
    components.map do |component|
      {
        'id' => required_project_design_string(component, 'id'),
        'ipcore' => IPCORE_ID,
        'instance' => instance_id,
        'type' => project_design_component_type(component),
        'parameters' => project_design_component_parameters(component),
        'ports' => []
      }
    end
  end

  def project_design_state(package_ref, owner, instance_id)
    {
      'ipcore' => IPCORE_ID,
      'instance' => instance_id,
      'schema' => PROJECT_STATE_SCHEMA,
      'state' => project_design_state_payload(owner),
      'package_ref' => package_ref
    }
  end

  def project_design_state_payload(owner)
    return default_project_design_state unless owner

    config = project_design_hash_field(owner, 'config')
    extension_data = project_design_hash_field(owner, 'extensionData')
    extension_state = extension_data.fetch('state', nil)
    return extension_state if extension_state.is_a?(Hash)

    config_state = config.fetch('state', nil)
    return config_state if config_state.is_a?(Hash)

    if config.fetch('global_parameters', nil).is_a?(Hash)
      return default_project_design_state.merge('global_parameters' => config.fetch('global_parameters'))
    end

    if config.fetch('parameters', nil).is_a?(Hash)
      return default_project_design_state.merge('global_parameters' => config.fetch('parameters'))
    end

    flat_parameters = config.select { |key, _value| DEFAULTS.key?(key) }
    default_project_design_state.merge('global_parameters' => flat_parameters)
  end

  def default_project_design_state
    {
      'kind' => 'noc',
      'type' => FABRIC_TYPE,
      'global_parameters' => {}
    }
  end

  def project_design_component_parameters(component)
    config = project_design_hash_field(component, 'config')
    parameters = if config.key?('parameters')
                   config.fetch('parameters')
                 else
                   config.reject { |key, _value| %w[state global_parameters tables documents files].include?(key) }
                 end
    unless parameters.is_a?(Hash)
      raise GenerationError,
            "ipcraft.project.v1 component #{component.fetch('id', '<unnamed>')} config.parameters must be an object"
    end

    parameters = parameters.dup
    identity = project_design_hash_field(component, 'identity')
    external_id = identity['external_id'] || identity['externalId']
    parameters['external_id'] ||= external_id if external_id
    parameters
  end

  def normalize_project_design_connections(connections, interfaces, modules)
    module_ids = modules.map { |mod| mod.fetch('id') }
    interface_port_by_component = project_design_interface_ports(interfaces)

    connections.filter_map do |connection|
      from = project_design_connection_ref(connection, 'from')
      to = project_design_connection_ref(connection, 'to')
      next unless module_ids.include?(from.fetch('component')) || module_ids.include?(to.fetch('component'))

      {
        'id' => connection.fetch('id', '<unnamed>'),
        'source' => project_design_connection_endpoint(from, interface_port_by_component, connection),
        'target' => project_design_connection_endpoint(to, interface_port_by_component, connection)
      }
    end
  end

  def project_design_connection_ref(connection, key)
    ref = connection.fetch(key, nil)
    unless ref.is_a?(Hash)
      raise GenerationError,
            "ipcraft.project.v1 connection #{connection.fetch('id', '<unnamed>')} #{key} must be an endpoint object"
    end

    {
      'component' => required_project_design_string(ref, 'component'),
      'interface' => required_project_design_string(ref, 'interface')
    }
  end

  def project_design_interface_ports(interfaces)
    ports = Hash.new { |hash, key| hash[key] = {} }
    interfaces.each do |interface|
      owner = required_project_design_string(interface, 'ownerComponentId')
      id = required_project_design_string(interface, 'id')
      config = project_design_hash_field(interface, 'config')
      metadata = project_design_hash_field(interface, 'metadata')
      port = config['port'] || metadata['port'] || id
      ports[owner][id] = port
    end
    ports
  end

  def project_design_connection_endpoint(ref, interface_port_by_component, connection)
    component = ref.fetch('component')
    interface = ref.fetch('interface')
    ports = interface_port_by_component.fetch(component, nil)
    port = ports && !ports.empty? ? ports[interface] : interface
    unless port
      raise GenerationError,
            "ipcraft.project.v1 connection #{connection.fetch('id', '<unnamed>')} " \
            "references unknown interface #{component}.#{interface}"
    end

    { 'module' => component, 'port' => port }
  end

  def project_design_component_type(component)
    required_project_design_string(component, 'type').split('::').last
  end

  def project_design_hash_field(data, key)
    value = data.fetch(key, {})
    raise GenerationError, "ipcraft.project.v1 #{key} must be an object" unless value.is_a?(Hash)

    value
  end

  def project_design_optional_array(data, key)
    value = data.fetch(key, [])
    raise GenerationError, "ipcraft.project.v1 #{key} must be an array" unless value.is_a?(Array)

    value
  end

  def project_design_required_array(data, key)
    value = data[key]
    raise GenerationError, "ipcraft.project.v1 #{key} must be an array" unless value.is_a?(Array)

    value
  end

  def required_project_design_string(data, key)
    value = data[key]
    raise GenerationError, "ipcraft.project.v1 #{key} must be a string" unless value.is_a?(String) && !value.empty?

    value
  end

  def normalize_legacy_ipcraft_project(data)
    package = required_string(data, 'package')
    project = required_hash(data, 'project')
    project_instance = required_hash(project, 'instance')
    instances = required_array(data, 'instances')

    {
      'schema' => GRAPH_SCHEMA,
      'name' => project_name(data, project),
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id'),
      'ipcore_state' => [ipcraft_project_state(package, project_instance)],
      'modules' => normalize_ipcraft_instances(instances, package, project_instance.fetch('id')),
      'connections' => normalize_ipcraft_connections(data.fetch('connections', []), instances)
    }
  end

  def ipcraft_project_state(package, project_instance)
    state = project_instance.fetch('state', {})
    unless state.is_a?(Hash)
      raise GenerationError, 'ipcraft.noc.project.v1 project.instance.state must be an object'
    end

    {
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id'),
      'schema' => project_instance['schema'],
      'state' => state
    }
  end

  def normalize_ipcraft_instances(instances, package, instance_id)
    instances.map do |instance|
      {
        'id' => instance.fetch('id'),
        'ipcore' => package,
        'instance' => instance_id,
        'type' => instance.fetch('module'),
        'parameters' => instance.fetch('parameters', {}),
        'ports' => instance.fetch('interfaces', [])
      }
    end
  end

  def normalize_ipcraft_connections(connections, instances)
    return [] unless connections
    raise GenerationError, 'ipcraft.noc.project.v1 connections must be an array' unless connections.is_a?(Array)

    interface_port_by_instance = ipcraft_interface_ports(instances)
    connections.map do |connection|
      refs = connection.fetch('interfaces', nil)
      unless refs.is_a?(Array) && refs.size == 2
        raise GenerationError,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
      end

      {
        'id' => connection.fetch('id', '<unnamed>'),
        'source' => ipcraft_connection_endpoint(refs[0], interface_port_by_instance, connection),
        'target' => ipcraft_connection_endpoint(refs[1], interface_port_by_instance, connection)
      }
    end
  end

  def ipcraft_interface_ports(instances)
    instances.to_h do |instance|
      interfaces = instance.fetch('interfaces', [])
      unless interfaces.is_a?(Array)
        raise GenerationError,
              "ipcraft.noc.project.v1 instance #{instance.fetch('id', '<unnamed>')} interfaces must be an array"
      end

      ports = interfaces.to_h do |interface|
        id = interface.fetch('id', interface['port'])
        port = interface.fetch('port', id)
        [id, port]
      end
      [instance.fetch('id'), ports]
    end
  end

  def ipcraft_connection_endpoint(ref, interface_port_by_instance, connection)
    instance_id = ref.fetch('instance', nil)
    interface_id = ref.fetch('interface', nil)
    port = interface_port_by_instance.fetch(instance_id, {})[interface_id]
    unless port
      raise GenerationError,
            "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{interface_id}"
    end

    { 'module' => instance_id, 'port' => port }
  end

  def project_name(data, project)
    name = data.dig('graph', 'name') || project['name']
    raise GenerationError, 'ipcraft.noc.project.v1 graph.name must be a string' unless name.is_a?(String) && !name.empty?

    name
  end

  def required_hash(data, key)
    value = data[key]
    raise GenerationError, "ipcraft.noc.project.v1 #{key} must be an object" unless value.is_a?(Hash)

    value
  end

  def required_array(data, key)
    value = data[key]
    raise GenerationError, "ipcraft.noc.project.v1 #{key} must be an array" unless value.is_a?(Array)

    value
  end

  def required_string(data, key)
    value = data[key]
    raise GenerationError, "ipcraft.noc.project.v1 #{key} must be a string" unless value.is_a?(String) && !value.empty?

    value
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

  def validate_vendor!
    missing = REQUIRED_VENDOR_PATHS.any? do |relative|
      !File.file?(File.join(vendor_dir, relative))
    end
    raise GenerationError, 'OpenNoC vendor source is missing or incomplete.' if missing
  end

  def run_mesh_generator(mesh_config, wrapper)
    mesh_dir = File.join(vendor_dir, 'tools/mesh_generator')
    Dir.mktmpdir('finepaper-opennoc-mesh') do |dir|
      FileUtils.cp_r(mesh_dir, dir)
      working_dir = File.join(dir, 'mesh_generator')
      stdout, stderr, status = Open3.capture3('python3', 'mesh_gen.py', '-f', File.expand_path(mesh_config),
                                              chdir: working_dir)
      unless status.success?
        message = stderr.empty? ? stdout : stderr
        raise GenerationError, "OpenNoC mesh generator failed: #{message.strip}"
      end

      generated_wrapper = File.join(working_dir, wrapper)
      raise GenerationError, "OpenNoC mesh generator did not create #{wrapper}" unless File.file?(generated_wrapper)

      FileUtils.cp(generated_wrapper, File.join(output_dir, wrapper))
    end
  end

  def copy_vendor_artifacts(model, _wrapper)
    copy_vendor_file('LICENSE')
    copy_vendor_file('tools/mesh_generator/chi_xp_node.sv')
    copy_vendor_dir('rtl/include')
    copy_vendor_dir('rtl/misc')

    selected_agent_types(model).each do |agent_type|
      rtl_dir = AGENT_RTL_DIR[agent_type]
      copy_vendor_dir("rtl/src/#{rtl_dir}") if rtl_dir
    end
  end

  def selected_agent_types(model)
    model.fetch(:attachments).map { |attachment| attachment.fetch(:agent_type) }.uniq
  end

  def copy_vendor_file(relative)
    destination = File.join(output_dir, relative)
    FileUtils.mkdir_p(File.dirname(destination))
    FileUtils.cp(File.join(vendor_dir, relative), destination)
  end

  def copy_vendor_dir(relative)
    source = File.join(vendor_dir, relative)
    destination = File.join(output_dir, relative)
    FileUtils.rm_rf(destination)
    FileUtils.mkdir_p(File.dirname(destination))
    FileUtils.cp_r(source, destination)
  end

  def render_outputs(model, wrapper)
    filelist_entries = filelist_entries(model, wrapper)
    render_template('opennoc_filelist.f.erb', 'opennoc_filelist.f',
                    model: model, wrapper: wrapper, filelist_entries: filelist_entries)
    File.chmod(0o644, File.join(output_dir, 'opennoc_filelist.f'))

    render_template('verify.sh.erb', 'verify.sh', model: model, wrapper: wrapper)
    File.chmod(0o755, File.join(output_dir, 'verify.sh'))

    File.write(File.join(output_dir, 'manifest.json'), JSON.pretty_generate(manifest(model, wrapper)))
  end

  def render_template(template_name, output_name, locals)
    template_path = File.join(template_dir, template_name)
    raise GenerationError, "missing template #{template_name}" unless File.file?(template_path)

    rendered = ERB.new(File.read(template_path), trim_mode: '-').result_with_hash(locals)
    File.write(File.join(output_dir, output_name), rendered)
  end

  def filelist_entries(model, wrapper)
    entries = [
      wrapper,
      'tools/mesh_generator/chi_xp_node.sv',
      'rtl/misc/chi_xp_channel.v'
    ]

    include_files = Dir.glob(File.join(output_dir, 'rtl/include/*')).select { |path| File.file?(path) }
                       .map { |path| relative_output_path(path) }
    misc_files = Dir.glob(File.join(output_dir, 'rtl/misc/*')).select { |path| File.file?(path) }
                    .map { |path| relative_output_path(path) }
    agent_files = selected_agent_types(model).filter_map { |type| AGENT_RTL_DIR[type] }
                                       .flat_map do |dir|
      Dir.glob(File.join(output_dir, "rtl/src/#{dir}/*")).select { |path| File.file?(path) }
         .map { |path| relative_output_path(path) }
    end

    (entries + include_files + misc_files + agent_files).uniq
  end

  def relative_output_path(path)
    path = File.expand_path(path)
    root = File.expand_path(output_dir)
    path.delete_prefix("#{root}/")
  end

  def manifest(model, wrapper)
    {
      ipcore: IPCORE_ID,
      topology: 'mesh',
      rows: model.fetch(:rows),
      cols: model.fetch(:cols),
      parameters: model.fetch(:parameters),
      wrapper: wrapper,
      agents: manifest_agents(model),
      verify: './verify.sh'
    }
  end

  def manifest_agents(model)
    module_by_id = model.fetch(:graph).fetch('modules').to_h { |mod| [mod.fetch('id'), mod] }
    model.fetch(:attachments).map do |attachment|
      agent = module_by_id.fetch(attachment.fetch(:agent_id))
      type = agent.fetch('type')
      rtl_dir = AGENT_RTL_DIR[type]
      {
        id: artifact_id(agent),
        type: type,
        upstream_type: attachment.fetch(:upstream_type),
        xp: artifact_id(module_by_id.fetch(attachment.fetch(:xp_id))),
        slot: attachment.fetch(:slot),
        rtl: rtl_dir ? "rtl/src/#{rtl_dir}" : 'external'
      }
    end
  end

  def validate_parameters!(parameters)
    DEFAULTS.each_key { |name| positive_integer!(parameters, name) }
  end

  def positive_integer!(parameters, name)
    value = parameters[name]
    raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

    value
  end

  def infer_coordinates(xps, connections)
    logical_coordinates = logical_xp_coordinates(xps)
    if logical_coordinates
      coordinates = rectangular_coordinates(logical_coordinates)
      unless coordinates
        raise GenerationError, "OpenNoCXP graph must be rectangular, found #{xps.size} XPs"
      end

      return coordinates
    end

    coordinates = rectangular_coordinates(connection_xp_coordinates(xps, connections)) ||
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
      binding[:upstream_type] = binding.fetch(:agent_enum)
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
