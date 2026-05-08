Port = Struct.new(:dir, :width, :name)
ModuleIface = Struct.new(:name, :ports)

class VerilogParser
  PORT_RE = /\b(input|output|inout)\b\s+(?:(?:wire|reg|logic|tri|signed|unsigned)\s+)*((?:\[[^\]]*\]\s*)*)(\w+)\s*[;,)]/

  def self.parse(path)
    src = File.read(path).gsub(%r{//[^\n]*}, '').gsub(%r{/\*.*?\*/}m, '')
    mod = src.match(/\bmodule\s+(\w+)/) or return nil
    ports = src.scan(PORT_RE).map { |dir, w, name| Port.new(dir, w.strip.empty? ? nil : w.strip, name) }
    ModuleIface.new(mod[1], ports)
  end
end
