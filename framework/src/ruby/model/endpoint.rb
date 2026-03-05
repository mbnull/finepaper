class Endpoint
  attr_reader :id, :type, :protocol, :data_width, :config
  attr_accessor :ports, :template

  def self.config_schema
    {
      buffer_depth: { type: :integer, default: 16 },
      qos_enabled: { type: :boolean, default: false }
    }
  end

  def initialize(id, type, protocol, data_width, config = {})
    @id = id
    @type = type
    @protocol = protocol
    @data_width = data_width
    @config = self.class.config_schema.transform_values { |v| v[:default] }.merge(config)
  end
end
