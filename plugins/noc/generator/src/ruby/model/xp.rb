class Xp
  attr_reader :id, :x, :y, :endpoints, :config

  def self.config_schema
    {
      routing_algorithm: { type: :string, default: 'xy' },
      vc_count: { type: :integer, default: 2 },
      buffer_depth: { type: :integer, default: 8 }
    }
  end

  def initialize(id, x, y, endpoints, config = {})
    @id = id
    @x = x
    @y = y
    @endpoints = endpoints
    @config = self.class.config_schema.transform_values { |v| v[:default] }.merge(config)
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
