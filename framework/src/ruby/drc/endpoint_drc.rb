require_relative '../model/module_catalog'

class UniqueEndpointIds < DrcBase
  def check(noc)
    noc.endpoints.map(&:id).tally.select { |_, n| n > 1 }.keys.map { |id| "Duplicate endpoint id: #{id}" }
  end
end

class ValidEndpointConfig < DrcBase
  def check(noc)
    noc.endpoints.flat_map do |ep|
      endpoint_values = {
        type: ep.type,
        protocol: ep.protocol,
        data_width: ep.data_width
      }.merge(ep.config)

      endpoint_values.flat_map do |key, value|
        parameter = ModuleCatalog.parameter(ep.module_name, key)
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
