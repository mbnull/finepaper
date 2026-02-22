class Xp
  attr_reader :id, :x, :y, :endpoints

  def initialize(id, x, y, endpoints)
    @id = id
    @x = x
    @y = y
    @endpoints = endpoints
  end

  def node_id(noc)
    (noc.xps.map(&:x).max + 1) * @y + @x
  end

  def neighbors(noc)
    noc.connections
       .select { |c| c.from == @id || c.to == @id }
       .map { |c| c.from == @id ? c.to : c.from }
       .map { |xp_id| noc.xps.find { |xp| xp.id == xp_id } }
       .compact
  end
end
