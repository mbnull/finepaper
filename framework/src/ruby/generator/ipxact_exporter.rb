require 'cgi'
require 'erb'
require 'fileutils'
require_relative '../model/module_catalog'

class IpXactExporter
  TEMPLATE_DIR = File.expand_path(File.join(__dir__, '..', '..', '..', 'template'))
  IPXACT_NS = 'http://www.accellera.org/XMLSchema/IPXACT/1685-2021/'.freeze
  FINEPAPER_NS = 'urn:finepaper:frontend:module:1.0'.freeze

  def initialize(descriptors = ModuleCatalog.descriptors)
    @descriptors = descriptors
  end

  def write(output_dir)
    FileUtils.mkdir_p(output_dir)

    @descriptors.each do |descriptor|
      path = File.join(output_dir, "#{descriptor[:name]}.component.xml")
      File.write(path,
                 render('ipxact_component.xml.erb',
                        descriptor: descriptor,
                        ipxact_ns: IPXACT_NS,
                        finepaper_ns: FINEPAPER_NS))
    end
  end

  private

  def render(template_name, locals)
    locals.each do |name, value|
      instance_variable_set("@#{name}", value)
    end

    template = File.read(File.join(TEMPLATE_DIR, template_name))
    ERB.new(template, trim_mode: '-').result(binding)
  end

  def xml_escape(value)
    CGI.escapeHTML(xml_scalar(value))
  end

  def xml_scalar(value)
    case value
    when true then 'true'
    when false then 'false'
    else value.to_s
    end
  end

  def xml_attributes(attributes)
    attributes.each_with_object([]) do |(key, value), result|
      next if value.nil? || (value.respond_to?(:empty?) && value.empty?)

      result << %(#{key}="#{xml_escape(value)}")
    end.then do |pairs|
      pairs.empty? ? '' : ' ' + pairs.join(' ')
    end
  end

  def ipxact_direction(direction)
    case direction
    when 'input' then 'in'
    when 'output' then 'out'
    else direction.to_s
    end
  end
end
