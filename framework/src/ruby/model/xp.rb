require_relative 'module_catalog'

class Xp
  DEFAULT_MODULE_NAME = 'XP'.freeze

  attr_reader :id, :x, :y, :endpoints, :config, :module_name

  def self.default_module_name
    DEFAULT_MODULE_NAME
  end

  def self.config_schema(module_name = default_module_name)
    ModuleCatalog.config_schema(module_name)
  end

  def self.module_descriptor(module_name = default_module_name)
    ModuleCatalog.descriptor(module_name)
  end

  def initialize(id, x, y, endpoints, config = {}, module_name = self.class.default_module_name)
    ModuleCatalog.assert_family!(module_name, :xp)
    @id = id
    @x = x
    @y = y
    @endpoints = endpoints
    @module_name = module_name
    @config = self.class.config_schema(@module_name).transform_values { |value| value[:default] }.merge(config)
  end

  def module_descriptor
    self.class.module_descriptor(@module_name)
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
