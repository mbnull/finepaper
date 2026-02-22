require 'erb'
require 'fileutils'

class RtlGenerator
  def initialize(noc, template_dir)
    @noc = noc
    @template_dir = template_dir
  end

  def render(template, output_path)
    tmpl = File.read(File.join(@template_dir, template))
    FileUtils.mkdir_p(File.dirname(output_path))
    File.write(output_path, ERB.new(tmpl).result(@noc.expose))
  end
end
