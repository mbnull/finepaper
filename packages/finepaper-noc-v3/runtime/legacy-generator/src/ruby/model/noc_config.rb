class NocConfig
  attr_reader :name, :version, :parameters, :xps, :connections, :endpoints,
              :domain_implementation

  def initialize(name, version, parameters, xps, connections, endpoints,
                 domain_implementation = nil)
    @name = name
    @version = version
    @parameters = parameters
    @xps = xps
    @connections = connections
    @endpoints = endpoints
    @domain_implementation = domain_implementation
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
