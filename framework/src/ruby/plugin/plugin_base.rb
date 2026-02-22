class PluginBase
  def process(noc, context) = raise NotImplementedError
end

class PluginRunner
  def initialize = @plugins = []
  def register(plugin) = @plugins << plugin
  def run(noc, context) = @plugins.each { |p| p.process(noc, context) }
end
