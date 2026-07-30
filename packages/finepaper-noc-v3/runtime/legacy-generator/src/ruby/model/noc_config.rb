class NocConfig
  attr_reader :name, :version, :parameters, :xps, :connections, :endpoints

  def initialize(name, version, parameters, xps, connections, endpoints)
    @name = name
    @version = version
    @parameters = parameters
    @xps = xps
    @connections = connections
    @endpoints = endpoints
  end

  def expose
    @noc = self
    binding
  end

  def catalog
    { masters:     @endpoints.select { |e| e.type == 'master' },
      slaves:      @endpoints.select { |e| e.type == 'slave' },
      by_protocol: @endpoints.group_by(&:protocol) }
  end
end
