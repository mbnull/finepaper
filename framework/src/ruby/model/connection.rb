class Connection
  VALID_DIRECTIONS = %w[north east south west].freeze

  attr_reader :from, :to, :dir, :from_port, :to_port, :kind

  def initialize(from, to, dir = nil, from_port: nil, to_port: nil, kind: 'router')
    @from = from
    @to = to
    @dir = dir
    @from_port = from_port
    @to_port = to_port
    @kind = kind
  end

  def router_direction
    return @dir unless @dir.nil? || @dir.empty?

    self.class.direction_from_ports(@from_port, @to_port)
  end

  def router_link?
    @kind == 'router'
  end

  def self.direction_from_ports(from_port, to_port)
    source_side = side_from_port(from_port)
    target_side = side_from_port(to_port)
    return nil if source_side && target_side && source_side != opposite_direction(target_side)
    return source_side if source_side
    return opposite_direction(target_side) if target_side

    nil
  end

  def self.side_from_port(port_name)
    return nil if port_name.nil? || port_name.empty?

    match = /\A(north|east|south|west)_(?:in|out)\z/.match(port_name)
    match && match[1]
  end

  def self.opposite_direction(direction)
    {
      'north' => 'south',
      'south' => 'north',
      'east' => 'west',
      'west' => 'east'
    }[direction]
  end
end
