class UniqueEndpointIds < DrcBase
  def check(noc)
    noc.endpoints.map(&:id).tally.select { |_, n| n > 1 }.keys.map { |id| "Duplicate endpoint id: #{id}" }
  end
end

class ValidEndpointConfig < DrcBase
  def check(noc)
    noc.endpoints.flat_map do |ep|
      ep.config.flat_map do |key, value|
        schema = Endpoint.config_schema[key]
        next [] unless schema
        expected = schema[:type]
        valid = case expected
        when :integer then value.is_a?(Integer)
        when :string then value.is_a?(String)
        when :boolean then [true, false].include?(value)
        else true
        end
        valid ? [] : ["Endpoint #{ep.id}: invalid type for #{key}, expected #{expected}"]
      end
    end
  end
end

class EndpointBufferDepth < DrcBase
  def check(noc)
    noc.endpoints.flat_map do |ep|
      depth = ep.config[:buffer_depth]
      depth > 0 ? [] : ["Endpoint #{ep.id}: buffer_depth must be > 0"]
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
