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
    valid = case expected_type
    when :integer then value.is_a?(Integer)
    when :string then value.is_a?(String)
    when :boolean then [true, false].include?(value)
    else true
    end
    raise "Invalid type for #{key}: expected #{expected_type}, got #{value.class}" unless valid
  end

  def self.parse_endpoints(list)
    list.map do |e|
      config = parse_config(e['config'], Endpoint.config_schema)
      Endpoint.new(e['id'], e['type'], e['protocol'], e['data_width'], config)
    end
  end

  def self.parse_xps(list)
    list.map do |x|
      config = parse_config(x['config'], Xp.config_schema)
      Xp.new(x['id'], x['x'], x['y'], x['endpoints'] || [], config)
    end
  end

  def self.parse_connections(list)
    list.map { |c| Connection.new(c['from'], c['to'], c['dir']) }
  end
end
