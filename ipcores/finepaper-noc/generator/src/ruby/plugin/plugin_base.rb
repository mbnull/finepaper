class PluginBase
  def process(noc, context) = raise NotImplementedError
end

class PluginRunner
  def initialize = @plugins = []
  def register(plugin) = @plugins << plugin
  def run(noc, context) = @plugins.each { |p| p.process(noc, context) }
end

class NiPortPlugin < PluginBase
  def initialize(ipcore_dir) = @ipcore_dir = ipcore_dir

  def process(noc, _context)
    noc.endpoints.each do |ep|
      path = Dir[File.join(@ipcore_dir, "#{ep.id}.{sv,v}")].first
      next unless path
      ep.ports = VerilogParser.parse(path).ports
      tmpl = path.sub(/\.(sv|v)$/, '.sv.erb')
      ep.template = tmpl if File.exist?(tmpl)
    end
  end
end
