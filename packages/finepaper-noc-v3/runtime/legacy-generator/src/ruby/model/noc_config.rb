class NocConfig
  attr_reader :name, :version, :parameters, :xps, :connections, :endpoints,
              :domain_implementation, :power_intent_plan

  def initialize(name, version, parameters, xps, connections, endpoints,
                 domain_implementation = nil, power_intent_plan = nil)
    @name = name
    @version = version
    @parameters = parameters
    @xps = xps
    @connections = connections
    @endpoints = endpoints
    @domain_implementation = domain_implementation
    @power_intent_plan = deep_copy_json(power_intent_plan)
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

  private

  # The compiler returns a deeply frozen plan, while parsed legacy graphs are
  # mutable JSON. Keep NocConfig ownership identical in both cases and never
  # freeze or alias the caller's document.
  def deep_copy_json(value)
    case value
    when Hash
      value.each_with_object({}) do |(key, child), copy|
        copy[deep_copy_json(key)] = deep_copy_json(child)
      end
    when Array
      value.map { |child| deep_copy_json(child) }
    when String
      value.dup
    else
      value
    end
  end
end
