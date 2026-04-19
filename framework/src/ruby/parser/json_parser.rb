require 'json'
require_relative '../model/noc_config'
require_relative '../model/xp'
require_relative '../model/connection'
require_relative '../model/endpoint'

class JsonParser
  DEFAULTS = {
    'data_width' => 64,
    'flit_width' => 128,
    'addr_width' => 32
  }.freeze

  def self.parse(path)
    data = JSON.parse(File.read(path))
    validate_required!(data, path)

    params = DEFAULTS.merge(data['parameters'] || {})
    endpoints = parse_endpoints(data['endpoints'] || [])
    xps = parse_xps(data['xps'] || [])
    connections = parse_connections(data['connections'] || [])

    NocConfig.new(data['name'], data['version'], params, xps, connections, endpoints)
  end

  private

  def self.validate_required!(data, path)
    raise "Missing 'name' in #{path}" unless data['name']
    raise "Missing 'version' in #{path}" unless data['version']
  end

  def self.parse_config(json_config, schema)
    return {} if json_config.nil?

    config = {}
    json_config.each do |key, value|
      key_sym = key.to_sym
      raise "Unknown config field: #{key}" unless schema[key_sym]

      expected_type = schema[key_sym][:type]
      validate_type!(key, value, expected_type)
      config[key_sym] = value
    end
    config
  end

  def self.validate_type!(key, value, expected_type)
    valid = ModuleCatalog.value_matches_type?(value, expected_type)
    raise "Invalid type for #{key}: expected #{expected_type}, got #{value.class}" unless valid
  end

  def self.parse_endpoints(list)
    list.map do |e|
      module_name = e['module'] || Endpoint.default_module_name
      ModuleCatalog.assert_family!(module_name, :endpoint)
      config = parse_config(e['config'], Endpoint.config_schema(module_name))
      Endpoint.new(e['id'], e['type'], e['protocol'], e['data_width'], config, module_name)
    end
  end

  def self.parse_xps(list)
    list.map do |x|
      module_name = x['module'] || Xp.default_module_name
      ModuleCatalog.assert_family!(module_name, :xp)
      config = parse_config(x['config'], Xp.config_schema(module_name))
      Xp.new(x['id'], x['x'], x['y'], x['endpoints'] || [], config, module_name)
    end
  end

  def self.parse_connections(list)
    case list
    when Array
      list.map { |entry| parse_connection_entry(entry) }
    when Hash
      parse_connection_groups(list)
    else
      []
    end
  end

  def self.parse_connection_groups(groups)
    connections = []

    (groups['router_links'] || []).each do |group|
      from = group.fetch('from')
      links = group.fetch('links', {})
      links.each do |dir, to|
        connections << Connection.new(from, to, dir.to_s, kind: 'router')
      end
    end

    (groups['adjacency'] || {}).each do |from, links|
      (links || {}).each do |dir, to|
        connections << Connection.new(from, to, dir.to_s, kind: 'router')
      end
    end

    (groups['explicit'] || []).each do |entry|
      connections << parse_connection_entry(entry)
    end

    connections
  end

  def self.parse_connection_entry(entry)
    from_ref = normalize_connection_ref(entry['from'], entry['from_port'])
    to_ref = normalize_connection_ref(entry['to'], entry['to_port'])

    Connection.new(from_ref[:node],
                   to_ref[:node],
                   entry['dir'],
                   from_port: from_ref[:port],
                   to_port: to_ref[:port],
                   kind: entry['kind'] || 'router')
  end

  def self.normalize_connection_ref(ref, legacy_port = nil)
    if ref.is_a?(Hash)
      {
        node: ref.fetch('node'),
        port: ref['port'] || legacy_port
      }
    else
      {
        node: ref,
        port: legacy_port
      }
    end
  end
end
