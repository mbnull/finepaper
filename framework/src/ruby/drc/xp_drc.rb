class ValidXpConfig < DrcBase
  def check(noc)
    noc.xps.flat_map do |xp|
      xp.config.flat_map do |key, value|
        schema = Xp.config_schema[key]
        next [] unless schema
        expected = schema[:type]
        valid = case expected
        when :integer then value.is_a?(Integer)
        when :string then value.is_a?(String)
        when :boolean then [true, false].include?(value)
        else true
        end
        valid ? [] : ["XP #{xp.id}: invalid type for #{key}, expected #{expected}"]
      end
    end
  end
end

class XpRoutingAlgorithm < DrcBase
  VALID_ALGORITHMS = ['xy', 'yx'].freeze

  def check(noc)
    noc.xps.flat_map do |xp|
      algo = xp.config[:routing_algorithm]
      VALID_ALGORITHMS.include?(algo) ? [] : ["XP #{xp.id}: invalid routing_algorithm '#{algo}'"]
    end
  end
end

class XpVirtualChannels < DrcBase
  def check(noc)
    noc.xps.flat_map do |xp|
      vc = xp.config[:vc_count]
      (1..8).include?(vc) ? [] : ["XP #{xp.id}: vc_count must be 1-8, got #{vc}"]
    end
  end
end

class XpBufferDepth < DrcBase
  def check(noc)
    noc.xps.flat_map do |xp|
      depth = xp.config[:buffer_depth]
      depth > 0 ? [] : ["XP #{xp.id}: buffer_depth must be > 0"]
    end
  end
end
