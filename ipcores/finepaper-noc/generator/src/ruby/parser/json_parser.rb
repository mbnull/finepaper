require 'json'
require_relative '../model/noc_config'
require_relative '../model/xp'
require_relative '../model/connection'
require_relative '../model/endpoint'

class JsonParser
  IPCRAFT_PROJECT_SCHEMA = 'ipcraft.noc.project.v1'.freeze
  IPCORE_GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
  SUPPORTED_SCHEMAS = [IPCRAFT_PROJECT_SCHEMA, IPCORE_GRAPH_SCHEMA].freeze
  DEFAULTS = {
    'data_width' => 64,
    'flit_width' => 128,
    'addr_width' => 32
  }.freeze
  EDITOR_ONLY_XP_CONFIG_FIELDS = ['collapsed'].freeze
  GENERIC_XP_EDITOR_FIELDS = ['x', 'y', 'display_name', 'external_id', 'mesh_col', 'mesh_row', 'collapsed'].freeze
  GENERIC_ENDPOINT_EDITOR_FIELDS = ['display_name', 'external_id'].freeze
  ROUTER_FORWARD_PORTS = ['east', 'south'].freeze

  def self.parse(path)
    data = JSON.parse(File.read(path))
    unless SUPPORTED_SCHEMAS.include?(data['schema'])
      raise "expected schema #{IPCRAFT_PROJECT_SCHEMA} (or #{IPCORE_GRAPH_SCHEMA} for legacy compatibility)"
    end
    data = normalize_ipcraft_project(data, path) if data['schema'] == IPCRAFT_PROJECT_SCHEMA

    parse_ipcore_graph(data, path)
  end

  private

  def self.normalize_ipcraft_project(data, path)
    package = required_string(data, 'package', path)
    project = required_hash(data, 'project', path)
    project_instance = required_hash(project, 'instance', path)
    instances = required_array(data, 'instances', path)

    {
      'schema' => IPCORE_GRAPH_SCHEMA,
      'name' => project_name(data, project, path),
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id', path),
      'ipcore_state' => [ipcraft_project_state(package, project_instance, path)],
      'modules' => normalize_ipcraft_instances(instances, package, project_instance.fetch('id')),
      'connections' => normalize_ipcraft_connections(data.fetch('connections', []), instances, path)
    }
  end

  def self.ipcraft_project_state(package, project_instance, path)
    state = project_instance.fetch('state', {})
    raise "ipcraft.noc.project.v1 project.instance.state must be an object in #{path}" unless state.is_a?(Hash)

    {
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id', path),
      'schema' => project_instance['schema'],
      'state' => state
    }
  end

  def self.normalize_ipcraft_instances(instances, package, instance_id)
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

  def self.normalize_ipcraft_connections(connections, instances, path)
    return [] unless connections
    raise "ipcraft.noc.project.v1 connections must be an array in #{path}" unless connections.is_a?(Array)

    interface_port_by_instance = ipcraft_interface_ports(instances)
    connections.map do |connection|
      refs = connection.fetch('interfaces', nil)
      unless refs.is_a?(Array) && refs.size == 2
        raise "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
      end

      {
        'id' => connection.fetch('id', '<unnamed>'),
        'source' => ipcraft_connection_endpoint(refs[0], interface_port_by_instance, connection),
        'target' => ipcraft_connection_endpoint(refs[1], interface_port_by_instance, connection)
      }
    end
  end

  def self.ipcraft_interface_ports(instances)
    instances.to_h do |instance|
      interfaces = instance.fetch('interfaces', [])
      unless interfaces.is_a?(Array)
        raise "ipcraft.noc.project.v1 instance #{instance.fetch('id', '<unnamed>')} interfaces must be an array"
      end

      ports = interfaces.to_h do |interface|
        id = interface.fetch('id', interface['port'])
        port = interface.fetch('port', id)
        [id, port]
      end
      [instance.fetch('id'), ports]
    end
  end

  def self.ipcraft_connection_endpoint(ref, interface_port_by_instance, connection)
    instance_id = ref.fetch('instance', nil)
    interface_id = ref.fetch('interface', nil)
    port = interface_port_by_instance.fetch(instance_id, {})[interface_id]
    unless port
      raise "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{interface_id}"
    end

    { 'module' => instance_id, 'port' => port }
  end

  def self.project_name(data, project, path)
    name = data.dig('graph', 'name') || project['name']
    raise "Missing 'graph.name' in #{path}" unless name

    name
  end

  def self.required_hash(data, key, path)
    value = data[key]
    raise "ipcraft.noc.project.v1 #{key} must be an object in #{path}" unless value.is_a?(Hash)

    value
  end

  def self.required_array(data, key, path)
    value = data[key]
    raise "ipcraft.noc.project.v1 #{key} must be an array in #{path}" unless value.is_a?(Array)

    value
  end

  def self.required_string(data, key, path)
    value = data[key]
    raise "ipcraft.noc.project.v1 #{key} must be a string in #{path}" unless value.is_a?(String) && !value.empty?

    value
  end

  def self.parse_config(json_config, schema, ignored_fields = [])
    return {} if json_config.nil?

    config = {}
    json_config.each do |key, value|
      next if ignored_fields.include?(key)

      key_sym = key.to_sym
      raise "Unknown config field: #{key}" unless schema[key_sym]

      expected_type = schema[key_sym][:type]
      validate_type!(key, value, expected_type)
      config[key_sym] = value
    end
    config
  end

  def self.validate_type!(key, value, expected_type)
    valid = case expected_type
    when :integer then value.is_a?(Integer)
    when :string then value.is_a?(String)
    when :boolean then [true, false].include?(value)
    else true
    end
    raise "Invalid type for #{key}: expected #{expected_type}, got #{value.class}" unless valid
  end

  def self.parse_ipcore_graph(data, path)
    raise "Missing 'name' in #{path}" unless data['name']

    modules = data.fetch('modules', [])
    module_by_id = modules.to_h { |mod| [mod['id'], mod] }
    endpoint_ids_by_xp = Hash.new { |hash, key| hash[key] = [] }
    connections = parse_ipcore_connections(data.fetch('connections', []), module_by_id, endpoint_ids_by_xp)
    xps = modules
          .select { |mod| noc_module_type?(mod, 'XP') }
          .map { |mod| parse_ipcore_xp(mod, endpoint_ids_by_xp[mod['id']]) }
    endpoints = modules
                .select { |mod| noc_module_type?(mod, 'Endpoint') }
                .map { |mod| parse_ipcore_endpoint(mod) }

    NocConfig.new(data['name'],
                  data.fetch('version', '1.0'),
                  DEFAULTS.merge(ipcore_parameters(data)),
                  xps,
                  connections,
                  endpoints)
  end

  def self.noc_module_type?(mod, type)
    mod.is_a?(Hash) && mod['ipcore'] == 'finepaper.noc' && mod['type'] == type
  end

  def self.ipcore_parameters(data)
    ipcore_state = data['ipcore_state']
    return data['parameters'] || {} unless ipcore_state.is_a?(Array)

    state_record = ipcore_state.find do |record|
      record.is_a?(Hash) && record['ipcore'] == 'finepaper.noc'
    end
    state = state_record && state_record['state']
    parameters = state.is_a?(Hash) ? state['global_parameters'] : nil
    parameters.is_a?(Hash) ? parameters : (data['parameters'] || {})
  end

  def self.parse_ipcore_xp(mod, endpoints)
    params = mod['parameters'] || {}
    x, y = ipcore_xp_coordinates(mod['id'], params)
    config = parse_config(select_schema_fields(params, Xp.config_schema),
                          Xp.config_schema,
                          EDITOR_ONLY_XP_CONFIG_FIELDS + GENERIC_XP_EDITOR_FIELDS)
    Xp.new(mod['id'], x, y, endpoints, config)
  end

  def self.parse_ipcore_endpoint(mod)
    params = mod['parameters'] || {}
    config = parse_config(select_schema_fields(params, Endpoint.config_schema),
                          Endpoint.config_schema,
                          GENERIC_ENDPOINT_EDITOR_FIELDS)
    Endpoint.new(mod['id'],
                 params.fetch('type', 'master'),
                 params.fetch('protocol', 'axi4'),
                 params.fetch('data_width', DEFAULTS.fetch('data_width')),
                 config)
  end

  def self.parse_ipcore_connections(list, module_by_id, endpoint_ids_by_xp)
    list.filter_map do |conn|
      source = conn['source'] || {}
      target = conn['target'] || {}
      source_module = module_by_id[source['module']]
      target_module = module_by_id[target['module']]

      if noc_module_type?(source_module, 'XP') && noc_module_type?(target_module, 'XP')
        ipcore_router_connection(source, target)
      elsif noc_module_type?(source_module, 'Endpoint') && noc_module_type?(target_module, 'XP')
        endpoint_ids_by_xp[target_module['id']] << source_module['id']
        nil
      elsif noc_module_type?(source_module, 'XP') && noc_module_type?(target_module, 'Endpoint')
        endpoint_ids_by_xp[source_module['id']] << target_module['id']
        nil
      end
    end
  end

  def self.ipcore_router_connection(source, target)
    if ROUTER_FORWARD_PORTS.include?(source['port'])
      Connection.new(source['module'], target['module'], source['port'])
    elsif ROUTER_FORWARD_PORTS.include?(target['port'])
      Connection.new(target['module'], source['module'], target['port'])
    else
      Connection.new(source['module'], target['module'], source['port'])
    end
  end

  def self.ipcore_xp_coordinates(id, params)
    if params['mesh_col'].is_a?(Integer) && params['mesh_row'].is_a?(Integer)
      return [params['mesh_col'], params['mesh_row']]
    end

    match = id.to_s.match(/\Axp_(\d+)_(\d+)\z/)
    return [match[2].to_i, match[1].to_i] if match

    [integer_param(params, 'x', 0), integer_param(params, 'y', 0)]
  end

  def self.integer_param(params, name, fallback)
    value = params[name]
    value.is_a?(Integer) ? value : fallback
  end

  def self.select_schema_fields(values, schema)
    field_names = schema.keys.map(&:to_s)
    values.select { |key, _| field_names.include?(key) }
  end

end
