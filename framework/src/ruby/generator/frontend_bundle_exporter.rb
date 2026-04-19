require 'cgi'
require 'erb'
require 'fileutils'
require_relative '../model/module_catalog'

class FrontendBundleExporter
  TEMPLATE_DIR = File.expand_path(File.join(__dir__, '..', '..', '..', 'template'))

  def initialize(descriptors = ModuleCatalog.descriptors)
    @descriptors = descriptors
  end

  def write(output_dir)
    FileUtils.mkdir_p(output_dir)
    FileUtils.mkdir_p(File.join(output_dir, 'graphics'))

    File.write(File.join(output_dir, 'modules.xml'),
               render('module_bundle.xml.erb', descriptors: @descriptors))

    @descriptors.each do |descriptor|
      path = File.join(output_dir, 'graphics', "#{descriptor[:name]}.xml")
      File.write(path, render('module_graphics.xml.erb', descriptor: descriptor))
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
end
