class TopologyExpander
  DIRS = { [1, 0] => 'east', [0, 1] => 'south' }

  def self.expand(noc)
    return noc if noc.xps.any?
    mesh = noc.parameters['mesh'] or raise "Topology not defined: add parameters.mesh or specify xps"
    w, h = mesh['width'], mesh['height']
    ep_map = mesh['endpoint_map'] || {}
    xp_module = mesh['xp_module'] || Xp.default_module_name
    ModuleCatalog.assert_family!(xp_module, :xp)
    xps = (0...h).flat_map do |y|
      (0...w).map { |x| Xp.new("xp_#{x}_#{y}", x, y, ep_map["xp_#{x}_#{y}"] || [], {}, xp_module) }
    end
    conns = xps.flat_map do |xp|
      DIRS.filter_map do |(dx, dy), dir|
        nb = xps.find { |n| n.x == xp.x + dx && n.y == xp.y + dy }
        Connection.new(xp.id, nb.id, dir) if nb
      end
    end
    NocConfig.new(noc.name, noc.version, noc.parameters, xps, conns, noc.endpoints)
  end
end
