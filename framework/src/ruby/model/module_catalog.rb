require 'json'

module ModuleCatalog
  DEFAULT_CATALOG_DIR = File.expand_path(File.join(__dir__, 'modules')).freeze
  ENV_CATALOG_DIR = 'FINEPAPER_MODEL_DIR'.freeze

  module_function

  def catalog_dir
    @catalog_dir_override || File.expand_path(ENV.fetch(ENV_CATALOG_DIR, DEFAULT_CATALOG_DIR))
  end

  def with_catalog_dir(path)
    previous = @catalog_dir_override
    @catalog_dir_override = File.expand_path(path)
    yield
  ensure
    @catalog_dir_override = previous
  end

  def descriptors(catalog_dir: self.catalog_dir, include_abstract: false)
    resolved = load_descriptors(File.expand_path(catalog_dir))
    include_abstract ? resolved : resolved.reject { |entry| entry[:abstract] }
  end

  def descriptor(name, catalog_dir: self.catalog_dir, include_abstract: true)
    entries = descriptors(catalog_dir: catalog_dir, include_abstract: include_abstract)
    entries.find { |entry| entry[:name] == name.to_s }
  end

  def config_schema(name, catalog_dir: self.catalog_dir)
    (descriptor(name, catalog_dir: catalog_dir)&.fetch(:config_parameters, []) || []).each_with_object({}) do |parameter, schema|
      schema[parameter[:name].to_sym] = {
        type: ruby_parameter_type(parameter[:type]),
        default: parameter[:default]
      }
    end
  end

  def config_parameter(name, parameter_name, catalog_dir: self.catalog_dir)
    descriptor(name, catalog_dir: catalog_dir)&.fetch(:config_parameters, [])&.find do |parameter|
      parameter[:name] == parameter_name.to_s
    end
  end

  def port(name, port_id, catalog_dir: self.catalog_dir)
    descriptor(name, catalog_dir: catalog_dir)&.fetch(:ports, [])&.find do |entry|
      entry[:id] == port_id.to_s
    end
  end

  def assert_family!(name, family, catalog_dir: self.catalog_dir)
    descriptor = descriptor(name, catalog_dir: catalog_dir)
    raise "Unknown module descriptor: #{name}" unless descriptor

    actual_family = descriptor[:family].to_s
    expected_family = family.to_s
    return descriptor if actual_family == expected_family

    raise "Module #{name} belongs to family #{actual_family}, expected #{expected_family}"
  end

  def compatible_module_names(name, relation:, catalog_dir: self.catalog_dir)
    source = descriptor(name, catalog_dir: catalog_dir)
    return [] unless source

    case relation.to_s
    when 'attachment'
      compatible_attachment_modules(source, catalog_dir)
    when 'router'
      compatible_router_modules(source, catalog_dir)
    else
      []
    end
  end

  def validate_parameter_value(parameter, value)
    parameter = deep_symbolize(parameter)
    name = parameter[:name]
    expected_type = ruby_parameter_type(parameter[:type])

    return ["invalid type for #{name}, expected #{expected_type}"] unless value_matches_type?(value, expected_type)

    errors = []
    choices = (parameter[:choices] || []).map { |choice| deep_symbolize(choice)[:value] }
    unless choices.empty? || choices.include?(value)
      errors << "#{name} must be one of #{choices.join(', ')}, got #{value.inspect}"
    end

    if [:integer, :double].include?(expected_type)
      if parameter.key?(:min) && value < parameter[:min]
        errors << "#{name} must be >= #{parameter[:min]}, got #{value}"
      end
      if parameter.key?(:max) && value > parameter[:max]
        errors << "#{name} must be <= #{parameter[:max]}, got #{value}"
      end
    end

    errors
  end

  def ruby_parameter_type(frontend_type)
    case frontend_type.to_s
    when 'int' then :integer
    when 'bool' then :boolean
    when 'double' then :double
    else :string
    end
  end

  def value_matches_type?(value, expected_type)
    case expected_type
    when :integer then value.is_a?(Integer)
    when :string then value.is_a?(String)
    when :boolean then [true, false].include?(value)
    when :double then value.is_a?(Numeric)
    else true
    end
  end

  def load_descriptors(catalog_dir)
    @descriptor_cache ||= {}
    @descriptor_cache[catalog_dir] ||= begin
      shared = load_shared_catalog(catalog_dir)
      templates = normalize_parameter_templates(shared.fetch('parameter_templates', {}))
      raw_descriptors = Dir[File.join(catalog_dir, '*.json')].sort.each_with_object({}) do |path, result|
        next if File.basename(path).start_with?('_')

        entry = JSON.parse(File.read(path))
        result[entry.fetch('name')] = deep_symbolize(entry)
      end

      resolved = {}
      raw_descriptors.keys.map do |name|
        resolve_descriptor(name, raw_descriptors, templates, resolved, [])
      end
    end
  end

  def load_shared_catalog(catalog_dir)
    path = File.join(catalog_dir, '_shared.json')
    return {} unless File.exist?(path)

    JSON.parse(File.read(path))
  end

  def resolve_descriptor(name, raw_descriptors, templates, resolved, stack)
    return resolved[name] if resolved.key?(name)
    raise "Unknown module descriptor: #{name}" unless raw_descriptors.key?(name)
    raise "Circular module inheritance: #{(stack + [name]).join(' -> ')}" if stack.include?(name)

    entry = deep_dup(raw_descriptors.fetch(name))
    parent_name = entry.delete(:extends)&.to_s
    if parent_name
      parent = resolve_descriptor(parent_name, raw_descriptors, templates, resolved, stack + [name])
      entry = deep_merge(parent, entry)
      entry[:name] = name
    end

    resolved[name] = normalize_descriptor(entry, templates)
  end

  def normalize_descriptor(entry, parameter_templates)
    descriptor = deep_symbolize(entry)
    descriptor[:family] = descriptor[:family].to_s
    descriptor[:abstract] = !!descriptor[:abstract]
    descriptor[:ports] = normalize_ports(descriptor[:ports])
    parameter_groups = descriptor.delete(:parameters) || {}
    base_parameters = normalize_parameter_group(parameter_groups[:base], parameter_templates)
    config_parameters = normalize_parameter_group(parameter_groups[:config], parameter_templates)

    descriptor[:config_parameters] = config_parameters
    descriptor[:parameters] = base_parameters + config_parameters
    descriptor
  end

  def normalize_parameter_group(parameters, templates)
    (parameters || []).map { |parameter| normalize_parameter(parameter, templates) }
  end

  def normalize_parameter(parameter, templates)
    parameter = deep_symbolize(parameter)
    if parameter.key?(:use)
      template_name = parameter.delete(:use).to_s
      template = templates.fetch(template_name) do
        raise "Unknown parameter template: #{template_name}"
      end
      parameter = deep_merge(template, parameter)
    end

    parameter[:name] = parameter[:name].to_s
    parameter
  end

  def normalize_parameter_templates(templates)
    templates.each_with_object({}) do |(name, parameter), normalized|
      normalized[name.to_s] = deep_symbolize(parameter)
    end
  end

  def normalize_ports(ports_definition)
    case ports_definition
    when Array
      ports_definition.map { |port| normalize_port(port) }
    when Hash
      ports = (ports_definition[:explicit] || []).map { |port| normalize_port(port) }
      ports.concat((ports_definition[:patterns] || []).flat_map { |pattern| expand_port_pattern(pattern) })
      ports
    else
      []
    end
  end

  def normalize_port(port)
    deep_symbolize(port)
  end

  def expand_port_pattern(pattern)
    pattern = deep_symbolize(pattern)

    case pattern[:generator]
    when 'attachment_slots'
      expand_attachment_slots(pattern)
    when 'router_sides'
      expand_router_sides(pattern)
    else
      raise "Unsupported port generator: #{pattern[:generator]}"
    end
  end

  def expand_attachment_slots(pattern)
    count = Integer(pattern.fetch(:count))
    id_prefix = pattern.fetch(:id_prefix, 'ep')
    name_prefix = pattern.fetch(:name_prefix, 'EP')
    description_template = pattern.fetch(:description_template, 'Attachment slot %{index}')

    count.times.map do |index|
      {
        id: "#{id_prefix}#{index}",
        direction: pattern.fetch(:direction),
        type: pattern.fetch(:type),
        bus_type: pattern[:bus_type],
        role: pattern[:role],
        name: "#{name_prefix}#{index}",
        description: format(description_template, index: index)
      }
    end
  end

  def expand_router_sides(pattern)
    sides = pattern.fetch(:sides, %w[north east south west])

    sides.flat_map do |side|
      label = side[0].upcase
      side_name = side.capitalize

      [
        {
          id: "#{side}_in",
          direction: 'input',
          type: pattern.fetch(:type),
          bus_type: pattern[:bus_type],
          role: pattern[:role],
          name: label,
          description: "#{side_name} router ingress"
        },
        {
          id: "#{side}_out",
          direction: 'output',
          type: pattern.fetch(:type),
          bus_type: pattern[:bus_type],
          role: pattern[:role],
          name: label,
          description: "#{side_name} router egress"
        }
      ]
    end
  end

  def deep_symbolize(value)
    case value
    when Array
      value.map { |item| deep_symbolize(item) }
    when Hash
      value.each_with_object({}) do |(key, item), result|
        result[key.to_sym] = deep_symbolize(item)
      end
    else
      value
    end
  end

  def deep_dup(value)
    case value
    when Array
      value.map { |item| deep_dup(item) }
    when Hash
      value.each_with_object({}) do |(key, item), result|
        result[key] = deep_dup(item)
      end
    else
      value
    end
  end

  def deep_merge(base, overrides)
    base.merge(overrides) do |_key, base_value, override_value|
      if base_value.is_a?(Hash) && override_value.is_a?(Hash)
        deep_merge(base_value, override_value)
      else
        override_value
      end
    end
  end

  def compatible_attachment_modules(source_descriptor, catalog_dir)
    attachment_bus = source_descriptor.dig(:connectivity, :attachment_bus_type)
    return [] unless attachment_bus

    target_family = case source_descriptor[:family]
                    when 'xp' then 'endpoint'
                    when 'endpoint' then 'xp'
                    end
    return [] unless target_family

    descriptors(catalog_dir: catalog_dir).select do |entry|
      entry[:family] == target_family &&
        entry.dig(:connectivity, :attachment_bus_type) == attachment_bus
    end.map { |entry| entry[:name] }
  end

  def compatible_router_modules(source_descriptor, catalog_dir)
    router_bus = source_descriptor.dig(:connectivity, :router_bus_type)
    return [] unless router_bus

    descriptors(catalog_dir: catalog_dir).select do |entry|
      entry[:family] == 'xp' &&
        entry.dig(:connectivity, :router_bus_type) == router_bus
    end.map { |entry| entry[:name] }
  end
end
