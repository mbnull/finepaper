require_relative 'module_catalog'

class Endpoint
  DEFAULT_MODULE_NAME = 'Endpoint'.freeze

  attr_reader :id, :type, :protocol, :data_width, :config, :module_name
  attr_accessor :ports, :template

  def self.default_module_name
    DEFAULT_MODULE_NAME
  end

  def self.config_schema(module_name = default_module_name)
    ModuleCatalog.config_schema(module_name)
  end

  def self.module_descriptor(module_name = default_module_name)
    ModuleCatalog.descriptor(module_name)
  end

  def initialize(id, type, protocol, data_width, config = {}, module_name = self.class.default_module_name)
    ModuleCatalog.assert_family!(module_name, :endpoint)
    @id = id
    @type = type
    @protocol = protocol
    @data_width = data_width
    @module_name = module_name
    @config = self.class.config_schema(@module_name).transform_values { |value| value[:default] }.merge(config)
  end

  def module_descriptor
    self.class.module_descriptor(@module_name)
  end
end
