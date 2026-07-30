# Migration-only legacy schema handling. Not used by normal runtime loading.
# ProjectDesign input plus migration-only legacy schema handling.
require 'json'
require_relative '../model/noc_config'
require_relative '../model/xp'
require_relative '../model/connection'
require_relative '../model/endpoint'

class JsonParser
  IPCRAFT_PROJECT_SCHEMA = 'ipcraft.project.v1'.freeze
  LEGACY_IPCRAFT_PROJECT_SCHEMA = 'ipcraft.noc.project.v1'.freeze
  IPCORE_GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
  SUPPORTED_SCHEMAS = [IPCRAFT_PROJECT_SCHEMA, LEGACY_IPCRAFT_PROJECT_SCHEMA, IPCORE_GRAPH_SCHEMA].freeze
  FINEPAPER_NOC_PACKAGE = 'finepaper.noc'.freeze
  TOPOLOGY_PARAMETRIC_SCHEMA = 'ipcraft.topology.parametric.v1'.freeze
  CONFIG_BUNDLE_KEYS = ['parameters', 'tables', 'documents', 'files', 'preserved'].freeze
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
      raise "expected schema #{IPCRAFT_PROJECT_SCHEMA} (or #{LEGACY_IPCRAFT_PROJECT_SCHEMA}/#{IPCORE_GRAPH_SCHEMA} for legacy compatibility)"
    end
    data = normalize_project_design(data, path) if data['schema'] == IPCRAFT_PROJECT_SCHEMA
    data = normalize_legacy_ipcraft_project(data, path) if data['schema'] == LEGACY_IPCRAFT_PROJECT_SCHEMA

    parse_ipcore_graph(data, path)
  end

  private

  def self.normalize_project_design(data, path)
    packages = required_array(data, 'packages', path, IPCRAFT_PROJECT_SCHEMA)
    components = required_array(data, 'components', path, IPCRAFT_PROJECT_SCHEMA)
    interfaces = optional_array(data, 'interfaces', path, IPCRAFT_PROJECT_SCHEMA)
    connections = optional_array(data, 'connections', path, IPCRAFT_PROJECT_SCHEMA)
    topologies = optional_array(data, 'topologies', path, IPCRAFT_PROJECT_SCHEMA)
    instance_id = required_string(data, 'id', path, IPCRAFT_PROJECT_SCHEMA)

    {
      'schema' => IPCORE_GRAPH_SCHEMA,
      'name' => required_string(data, 'name', path, IPCRAFT_PROJECT_SCHEMA),
      'ipcore' => FINEPAPER_NOC_PACKAGE,
      'instance' => instance_id,
      'version' => project_design_package_version(packages) || '1.0',
      'ipcore_state' => [
        {
          'ipcore' => FINEPAPER_NOC_PACKAGE,
          'instance' => instance_id,
          'state' => {
            'global_parameters' => project_design_global_parameters(data, components, topologies)
          }
        }
      ],
      'modules' => normalize_project_design_components(components, instance_id),
      'connections' => normalize_project_design_connections(connections, interfaces, topologies, path)
    }
  end

  def self.normalize_project_design_components(components, instance_id)
    components.map do |component|
      {
        'id' => component.fetch('id'),
        'ipcore' => package_id_from_ref(component.fetch('packageRef', '')),
        'instance' => instance_id,
        'type' => project_design_component_type(component),
        'parameters' => project_design_component_parameters(component)
      }
    end
  end

  def self.project_design_component_type(component)
    project_design_extension_value(component, 'module') ||
      project_design_extension_value(component, 'module_type') ||
      component.fetch('type')
  end

  def self.project_design_component_parameters(component)
    params = {}
    merge_hash!(params, project_design_extension_parameters(component.fetch('metadata', nil)))
    merge_hash!(params, project_design_extension_parameters(component.fetch('extensionData', nil)))
    merge_hash!(params, project_design_config_parameters(component.fetch('config', nil)))

    identity = component.fetch('identity', nil)
    params['display_name'] ||= identity['label'] if identity.is_a?(Hash) && identity['label'].is_a?(String)
    params
  end

  def self.project_design_global_parameters(data, components, topologies)
    params = {}
    merge_hash!(params, project_design_extension_parameters(data.fetch('metadata', nil)))

    optional_array(data, 'extensions', '<project>', IPCRAFT_PROJECT_SCHEMA).each do |extension|
      next unless extension.is_a?(Hash)

      owner = extension['ownerPackageId']
      schema = extension['schemaId']
      next unless owner == FINEPAPER_NOC_PACKAGE || schema.to_s.include?(FINEPAPER_NOC_PACKAGE)

      merge_hash!(params, project_design_extension_parameters(extension.fetch('data', nil)))
    end

    components.each do |component|
      next unless package_id_from_ref(component.fetch('packageRef', '')) == FINEPAPER_NOC_PACKAGE
      next if ['XP', 'Endpoint'].include?(project_design_component_type(component))

      merge_hash!(params, project_design_component_parameters(component))
    end

    mesh = project_design_mesh_parameters(topologies)
    params['mesh'] ||= mesh if mesh
    params
  end

  def self.project_design_mesh_parameters(topologies)
    topology = topologies.find do |entry|
      entry.is_a?(Hash) &&
        entry['schema'] == TOPOLOGY_PARAMETRIC_SCHEMA &&
        entry['family'] == 'mesh'
    end
    return nil unless topology

    parameters = topology.fetch('parameters', {})
    return nil unless parameters.is_a?(Hash)

    width, height = project_design_mesh_dimensions(parameters)
    return nil unless width && height

    mesh = { 'width' => width, 'height' => height }
    endpoint_map = project_design_mesh_endpoint_map(topology.fetch('attachments', []))
    mesh['endpoint_map'] = endpoint_map unless endpoint_map.empty?
    mesh
  end

  def self.project_design_mesh_dimensions(parameters)
    dimensions = parameters['dimensions']
    width = parameters['width'] || parameters['cols'] || (dimensions[0] if dimensions.is_a?(Array))
    height = parameters['height'] || parameters['rows'] || (dimensions[1] if dimensions.is_a?(Array))
    [integer_or_nil(width), integer_or_nil(height)]
  end

  def self.project_design_mesh_endpoint_map(attachments)
    return {} unless attachments.is_a?(Array)

    attachments.each_with_object({}) do |attachment, map|
      next unless attachment.is_a?(Hash)

      xp_id = project_design_attachment_xp_id(attachment)
      component_id = attachment['componentRef']
      next unless xp_id && component_id

      map[xp_id] ||= []
      map[xp_id] << component_id
    end
  end

  def self.normalize_project_design_connections(connections, interfaces, topologies, path)
    interface_ports = project_design_interface_ports(interfaces)
    graph_connections = connections.map do |connection|
      {
        'id' => connection.fetch('id', '<unnamed>'),
        'source' => project_design_connection_endpoint(connection.fetch('from', nil),
                                                       interface_ports,
                                                       connection,
                                                       'from',
                                                       path),
        'target' => project_design_connection_endpoint(connection.fetch('to', nil),
                                                       interface_ports,
                                                       connection,
                                                       'to',
                                                       path)
      }
    end

    graph_connections + normalize_project_design_topology_attachments(topologies, interface_ports)
  end

  def self.project_design_interface_ports(interfaces)
    interfaces.each_with_object({}) do |interface, ports|
      next unless interface.is_a?(Hash)

      owner = interface.fetch('ownerComponentId', nil)
      id = interface.fetch('id', nil)
      next unless owner && id

      ports[[owner, id]] = project_design_interface_port(interface)
    end
  end

  def self.project_design_interface_port(interface)
    metadata = interface.fetch('metadata', nil)
    config = interface.fetch('config', nil)
    project_design_extension_value(interface, 'port') ||
      project_design_extension_value(interface, 'ipcore_port') ||
      (metadata['port'] if metadata.is_a?(Hash)) ||
      (config['port'] if config.is_a?(Hash)) ||
      interface.fetch('id')
  end

  def self.project_design_connection_endpoint(ref, interface_ports, connection, endpoint_key, path)
    unless ref.is_a?(Hash)
      raise "ipcraft.project.v1 connection #{connection.fetch('id', '<unnamed>')} #{endpoint_key} must be an object in #{path}"
    end

    component_id = ref.fetch('component', nil)
    interface_id = ref.fetch('interface', nil)
    unless component_id && interface_id
      raise "ipcraft.project.v1 connection #{connection.fetch('id', '<unnamed>')} #{endpoint_key} must reference component and interface"
    end

    { 'module' => component_id, 'port' => interface_ports.fetch([component_id, interface_id], interface_id) }
  end

  def self.normalize_project_design_topology_attachments(topologies, interface_ports)
    topologies.flat_map do |topology|
      attachments = topology.is_a?(Hash) ? topology.fetch('attachments', []) : []
      next [] unless attachments.is_a?(Array)

      attachments.filter_map do |attachment|
        next unless attachment.is_a?(Hash)

        xp_id = project_design_attachment_xp_id(attachment)
        component_id = attachment['componentRef']
        next unless xp_id && component_id

        interface_id = attachment.fetch('interfaceRef', 'noc')
        {
          'id' => attachment.fetch('id', "#{component_id}_to_#{xp_id}"),
          'source' => {
            'module' => component_id,
            'port' => interface_ports.fetch([component_id, interface_id], interface_id)
          },
          'target' => {
            'module' => xp_id,
            'port' => project_design_attachment_port(attachment)
          }
        }
      end
    end
  end

  def self.project_design_attachment_xp_id(attachment)
    point = attachment.fetch('attachmentPoint', {})
    return point['component'] if point.is_a?(Hash) && point['component']
    return point['node'] if point.is_a?(Hash) && point['node']
    return point['xp'] if point.is_a?(Hash) && point['xp']

    col, row = project_design_attachment_coordinates(point)
    return nil unless col && row

    "xp_#{col}_#{row}"
  end

  def self.project_design_attachment_coordinates(point)
    return [nil, nil] unless point.is_a?(Hash)

    tile = point['tile']
    return [integer_or_nil(tile[0]), integer_or_nil(tile[1])] if tile.is_a?(Array)

    col = point['mesh_col'] || point['col'] || point['x']
    row = point['mesh_row'] || point['row'] || point['y']
    [integer_or_nil(col), integer_or_nil(row)]
  end

  def self.project_design_attachment_port(attachment)
    point = attachment.fetch('attachmentPoint', {})
    return point['slot'] if point.is_a?(Hash) && point['slot']
    return point['port'] if point.is_a?(Hash) && point['port']

    'local0'
  end

  def self.project_design_package_version(packages)
    package = packages.find { |entry| entry.is_a?(Hash) && entry['id'] == FINEPAPER_NOC_PACKAGE }
    package && package['version']
  end

  def self.package_id_from_ref(ref)
    ref.to_s.split('@', 2).first
  end

  def self.project_design_config_parameters(config)
    return {} unless config.is_a?(Hash)

    if config['parameters'].is_a?(Hash)
      config['parameters']
    elsif CONFIG_BUNDLE_KEYS.any? { |key| config.key?(key) }
      {}
    else
      config
    end
  end

  def self.project_design_extension_parameters(value)
    return {} unless value.is_a?(Hash)

    specific = value[FINEPAPER_NOC_PACKAGE]
    if specific.is_a?(Hash)
      return project_design_extension_parameters(specific)
    end

    return value['global_parameters'] if value['global_parameters'].is_a?(Hash)
    return value['parameters'] if value['parameters'].is_a?(Hash)

    {}
  end

  def self.project_design_extension_value(object, key)
    [object.fetch('metadata', nil), object.fetch('extensionData', nil)].each do |container|
      next unless container.is_a?(Hash)

      return container[key] if container.key?(key)

      package_data = container[FINEPAPER_NOC_PACKAGE]
      return package_data[key] if package_data.is_a?(Hash) && package_data.key?(key)
    end
    nil
  end

  def self.merge_hash!(target, value)
    target.merge!(value) if value.is_a?(Hash)
  end

  def self.integer_or_nil(value)
    value.is_a?(Integer) ? value : nil
  end

  def self.normalize_legacy_ipcraft_project(data, path)
    package = required_string(data, 'package', path, LEGACY_IPCRAFT_PROJECT_SCHEMA)
    project = required_hash(data, 'project', path, LEGACY_IPCRAFT_PROJECT_SCHEMA)
    project_instance = required_hash(project, 'instance', path, LEGACY_IPCRAFT_PROJECT_SCHEMA)
    instances = required_array(data, 'instances', path, LEGACY_IPCRAFT_PROJECT_SCHEMA)

    {
      'schema' => IPCORE_GRAPH_SCHEMA,
      'name' => project_name(data, project, path),
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id', path, LEGACY_IPCRAFT_PROJECT_SCHEMA),
      'ipcore_state' => [legacy_ipcraft_project_state(package, project_instance, path)],
      'modules' => normalize_legacy_ipcraft_instances(instances, package, project_instance.fetch('id')),
      'connections' => normalize_legacy_ipcraft_connections(data.fetch('connections', []), instances, path)
    }
  end

  def self.legacy_ipcraft_project_state(package, project_instance, path)
    state = project_instance.fetch('state', {})
    raise "ipcraft.noc.project.v1 project.instance.state must be an object in #{path}" unless state.is_a?(Hash)

    {
      'ipcore' => package,
      'instance' => required_string(project_instance, 'id', path),
      'schema' => project_instance['schema'],
      'state' => state
    }
  end

  def self.normalize_legacy_ipcraft_instances(instances, package, instance_id)
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

  def self.normalize_legacy_ipcraft_connections(connections, instances, path)
    return [] unless connections
    raise "ipcraft.noc.project.v1 connections must be an array in #{path}" unless connections.is_a?(Array)

    interface_port_by_instance = legacy_ipcraft_interface_ports(instances)
    connections.map do |connection|
      refs = connection.fetch('interfaces', nil)
      unless refs.is_a?(Array) && refs.size == 2
        raise "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
      end

      {
        'id' => connection.fetch('id', '<unnamed>'),
        'source' => legacy_ipcraft_connection_endpoint(refs[0], interface_port_by_instance, connection),
        'target' => legacy_ipcraft_connection_endpoint(refs[1], interface_port_by_instance, connection)
      }
    end
  end

  def self.legacy_ipcraft_interface_ports(instances)
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

  def self.legacy_ipcraft_connection_endpoint(ref, interface_port_by_instance, connection)
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

  def self.required_hash(data, key, path, schema = LEGACY_IPCRAFT_PROJECT_SCHEMA)
    value = data[key]
    raise "#{schema} #{key} must be an object in #{path}" unless value.is_a?(Hash)

    value
  end

  def self.required_array(data, key, path, schema = LEGACY_IPCRAFT_PROJECT_SCHEMA)
    value = data[key]
    raise "#{schema} #{key} must be an array in #{path}" unless value.is_a?(Array)

    value
  end

  def self.optional_array(data, key, path, schema)
    value = data.fetch(key, [])
    raise "#{schema} #{key} must be an array in #{path}" unless value.is_a?(Array)

    value
  end

  def self.required_string(data, key, path, schema = LEGACY_IPCRAFT_PROJECT_SCHEMA)
    value = data[key]
    raise "#{schema} #{key} must be a string in #{path}" unless value.is_a?(String) && !value.empty?

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
                  endpoints,
                  ipcore_domain_implementation(data),
                  ipcore_power_intent_plan(data))
  end

  def self.noc_module_type?(mod, type)
    mod.is_a?(Hash) && mod['ipcore'] == FINEPAPER_NOC_PACKAGE && mod['type'] == type
  end

  def self.ipcore_parameters(data)
    state = ipcore_package_state(data)
    parameters = state.is_a?(Hash) ? state['global_parameters'] : nil
    parameters.is_a?(Hash) ? parameters : (data['parameters'] || {})
  end

  def self.ipcore_domain_implementation(data)
    state = ipcore_package_state(data)
    state.is_a?(Hash) ? state['domain_implementation'] : nil
  end

  def self.ipcore_power_intent_plan(data)
    state = ipcore_package_state(data)
    state.is_a?(Hash) ? state['power_intent_plan'] : nil
  end

  def self.ipcore_package_state(data)
    ipcore_state = data['ipcore_state']
    return nil unless ipcore_state.is_a?(Array)

    state_record = ipcore_state.find do |record|
      record.is_a?(Hash) && record['ipcore'] == FINEPAPER_NOC_PACKAGE
    end
    state_record && state_record['state']
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
        add_endpoint_xp(endpoint_ids_by_xp, target_module['id'], source_module['id'])
        nil
      elsif noc_module_type?(source_module, 'XP') && noc_module_type?(target_module, 'Endpoint')
        add_endpoint_xp(endpoint_ids_by_xp, source_module['id'], target_module['id'])
        nil
      end
    end
  end

  def self.add_endpoint_xp(endpoint_ids_by_xp, xp_id, endpoint_id)
    endpoints = endpoint_ids_by_xp[xp_id]
    endpoints << endpoint_id unless endpoints.include?(endpoint_id)
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
