class DrcBase
  def check(noc) = raise NotImplementedError
end

require_relative 'endpoint_drc'
require_relative 'connection_drc'
require_relative 'xp_drc'

class UniqueXpIds < DrcBase
  def check(noc)
    noc.xps.map(&:id).tally.select { |_, n| n > 1 }.keys.map { |id| "Duplicate XP id: #{id}" }
  end
end

class DrcRunner
  def initialize
    @drcs = [
      UniqueXpIds.new,
      UniqueEndpointIds.new,
      ConnectionXpReferences.new,
      ValidEndpointConfig.new,
      EndpointBufferDepth.new,
      EndpointProtocol.new,
      ValidXpConfig.new,
      XpRoutingAlgorithm.new,
      XpVirtualChannels.new,
      XpBufferDepth.new
    ]
  end

  def register(drc) = @drcs << drc

  def run(noc)
    errors = @drcs.flat_map { |d| d.check(noc) }
    raise "DRC violations:\n#{errors.join("\n")}" unless errors.empty?
  end
end
