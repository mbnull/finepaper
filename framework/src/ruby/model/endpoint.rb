class Endpoint
  attr_reader :id, :type, :protocol, :data_width

  def initialize(id, type, protocol, data_width)
    @id = id
    @type = type
    @protocol = protocol
    @data_width = data_width
  end
end
