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

  def self.parse_endpoints(list)
    list.map do |e|
      Endpoint.new(e['id'], e['type'], e['protocol'], e['data_width'])
    end
  end

  def self.parse_xps(list)
    list.map { |x| Xp.new(x['id'], x['x'], x['y'], x['endpoints'] || []) }
  end

  def self.parse_connections(list)
    list.map { |c| Connection.new(c['from'], c['to'], c['dir']) }
  end
end
