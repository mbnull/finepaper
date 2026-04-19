require_relative '../model/module_catalog'

class UniqueEndpointIds < DrcBase
  def check(noc)
    noc.endpoints.map(&:id).tally.select { |_, n| n > 1 }.keys.map { |id| "Duplicate endpoint id: #{id}" }
  end
end

class ValidEndpointConfig < DrcBase
  def check(noc)
    noc.endpoints.flat_map do |ep|
      ep.config.flat_map do |key, value|
        parameter = ModuleCatalog.config_parameter(ep.module_name, key)
        next [] unless parameter

        ModuleCatalog.validate_parameter_value(parameter, value).map do |message|
          "Endpoint #{ep.id}: #{message}"
        end
      end
    end
  end
end

class EndpointProtocol < DrcBase
  def check(noc)
    noc.endpoints.flat_map do |ep|
      ep.protocol && !ep.protocol.empty? ? [] : ["Endpoint #{ep.id}: protocol cannot be empty"]
    end
  end
end
