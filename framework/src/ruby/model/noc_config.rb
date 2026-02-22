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
end
