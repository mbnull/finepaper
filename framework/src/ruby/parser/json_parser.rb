require 'json'
require_relative '../model/noc_config'
require_relative '../model/xp'
require_relative '../model/connection'
require_relative '../model/endpoint'

class JsonParser
  def self.parse(path)
    data = JSON.parse(File.read(path))

    endpoints = data['endpoints'].map { |e| Endpoint.new(e['id'], e['type'], e['protocol'], e['data_width']) }
    xps = data['xps'].map { |x| Xp.new(x['id'], x['x'], x['y'], x['endpoints'] || []) }
    connections = data['connections'].map { |c| Connection.new(c['from'], c['to'], c['dir']) }

    NocConfig.new(data['name'], data['version'], data['parameters'], xps, connections, endpoints)
  end
end
