require 'fileutils'
require 'date'
require 'json'
require 'pathname'
require 'tmpdir'
require 'yaml'

module SpecGenerator
  class SpecError < StandardError; end

  IPCORE_TOP_LEVEL_KEYS = %w[
    schema id name version kind buses runtime topology_presets instance_parameters modules
  ].freeze
  RUNTIME_KEYS = %w[generator drc].freeze
  COMMAND_KEYS = %w[command input_format args].freeze
  TOPOLOGY_PRESET_KEYS = %w[id label kind router_module id_pattern ports parameters].freeze
  TOPOLOGY_PRESET_PARAMETER_KEYS = %w[label default min max].freeze
  IPCORE_MODULE_KEYS = %w[
    palette_label graph_group description identity capabilities interface_limits parameters interfaces
  ].freeze
  INTERFACE_METADATA_KEYS = %w[cardinality autocomplete_group topology_rule].freeze
  INTERFACE_CARDINALITIES = %w[one many].freeze
  INTERFACE_TOPOLOGY_RULES = %w[opposite_side].freeze
  GENERATED_OUTPUT_ROOTS = [
    ['generated/ipcores/finepaper.noc/ipcore-runtime.json', :file],
    ['generated/ipcores/finepaper.noc/modules.xml', :file],
    ['generated/ipcores/finepaper.noc/graphics', :directory],
    ['ipcores/finepaper-noc/generator/src/ruby/model', :generated_files],
    ['generated/ipcores/finepaper.ravenoc/ipcore-runtime.json', :file],
    ['generated/ipcores/finepaper.ravenoc/modules.xml', :file],
    ['generated/ipcores/finepaper.ravenoc/graphics', :directory],
    ['generated/ipcores/finepaper.opennoc/ipcore-runtime.json', :file],
    ['generated/ipcores/finepaper.opennoc/modules.xml', :file],
    ['generated/ipcores/finepaper.opennoc/graphics', :directory]
  ].freeze
  HANDWRITTEN_MODEL_FILES = %w[connection.rb noc_config.rb].freeze
  STALE_RUNTIME_MANIFEST_FILE_NAME = %w[plugin json].join('.').freeze
  IPCORE_INTERFACE_KEYS = %w[
    label bus role connects_to match accepts config cardinality autocomplete_group topology_rule port ports
  ].freeze
  BUS_KEYS = %w[description ipxact compatibility config signals].freeze
  BUS_CONFIG_KEYS = %w[type enum default description].freeze
  COMPATIBILITY_KEYS = %w[roles match].freeze
  IDENTITY_KEYS = %w[external_id_prefix display_prefix width supports_mesh_coordinates].freeze
  CAPABILITY_KEYS = %w[supports_collapse].freeze
  PARAMETER_KEYS = %w[type default enum labels label description emit configurable min max].freeze
  PORT_KEYS = %w[id direction type bus_type role name description].freeze
  PARAMETER_TYPES = %w[string int bool].freeze
  EMIT_MODES = %w[attribute config editor editor_only].freeze
  PORT_DIRECTIONS = %w[input output inout].freeze

  View = Struct.new(:module_name, :graphics_xml, :anchors_xml, :interface_refs, keyword_init: true)
  ParsedSpec = Struct.new(:data, :views, keyword_init: true)

  def self.generate_ipcore(ipcore_path:, views_dir:, runtime_bundle_dir:, ruby_model_dir: nil)
    parsed = IpCoreParser.new(ipcore_path, views_dir).parse
    IpCoreRuntimeEmitter.new(parsed, source_root: File.dirname(ipcore_path)).write(runtime_bundle_dir)
    RubyModelEmitter.new(parsed.data).write(ruby_model_dir) if ruby_model_dir
  end

  def self.check_repository_generated_outputs(root: Dir.pwd)
    root = File.expand_path(root)
    mismatches = []
    Dir.mktmpdir('finepaper-spec-gen-check') do |dir|
      copy_ipcore_source(root, dir, 'finepaper-noc')
      copy_ipcore_source(root, dir, 'ravenoc')
      copy_ipcore_source(root, dir, 'opennoc')

      generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/finepaper-noc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/finepaper-noc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.noc'),
        ruby_model_dir: File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model')
      )

      generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/ravenoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      )

      generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/opennoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/opennoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.opennoc')
      )

      stale_generated_runtime_manifest_relpaths(root).each do |relpath|
        mismatches << "stale committed: #{relpath}"
      end

      GENERATED_OUTPUT_ROOTS.each do |relroot, type|
        relpaths = (generated_output_relpaths(root, relroot, type) +
                    generated_output_relpaths(dir, relroot, type)).uniq.sort
        relpaths.each do |relpath|
          committed_path = File.join(root, relpath)
          generated_path = File.join(dir, relpath)
          if !File.file?(committed_path)
            mismatches << "missing committed: #{relpath}"
          elsif !File.file?(generated_path)
            mismatches << "missing generated: #{relpath}"
          elsif File.binread(committed_path) != File.binread(generated_path)
            mismatches << "content mismatch: #{relpath}"
          end
        end
      end
    end

    return true if mismatches.empty?

    raise SpecError, "Generated artifacts are out of date:\n#{mismatches.map { |path| "  #{path}" }.join("\n")}"
  end

  def self.stale_runtime_manifest_file_name
    STALE_RUNTIME_MANIFEST_FILE_NAME
  end

  def self.copy_ipcore_source(source_root, target_root, package)
    source_package = File.join(source_root, 'ipcores', package)
    target_package = File.join(target_root, 'ipcores', package)
    FileUtils.mkdir_p(target_package)
    FileUtils.cp(File.join(source_package, 'ipcore.yml'), File.join(target_package, 'ipcore.yml'))
    FileUtils.cp_r(File.join(source_package, 'views'), File.join(target_package, 'views'))
  end

  def self.generated_output_relpaths(root, relroot, type)
    return [relroot] if type == :file

    base = File.join(root, relroot)
    return [] unless File.directory?(base)

    relpaths = Dir.glob(File.join(base, '**', '*'), File::FNM_DOTMATCH)
                  .select { |path| File.file?(path) }
                  .map { |path| path.sub(%r{\A#{Regexp.escape(root)}/}, '') }
    return relpaths unless type == :generated_files

    relpaths.reject { |relpath| HANDWRITTEN_MODEL_FILES.include?(File.basename(relpath)) }
  end

  def self.stale_generated_runtime_manifest_relpaths(root)
    Dir.glob(File.join(root, 'generated/ipcores/*', stale_runtime_manifest_file_name))
       .select { |path| File.file?(path) }
       .map { |path| path.sub(%r{\A#{Regexp.escape(root)}/}, '') }
       .sort
  end

  class ConstrainedYamlLoader
    ANCHOR_ALIAS_ERROR = 'YAML anchors and aliases are not allowed'.freeze
    CUSTOM_TAG_ERROR = 'YAML custom tags are not allowed'.freeze
    DOCUMENT_SEPARATOR_ERROR = 'YAML multi-document streams are not allowed'.freeze
    IMPLICIT_TIMESTAMP_ERROR = 'Implicit timestamp values are not allowed'.freeze

    class DocumentMarkerHandler < Psych::Handler
      def start_document(_version, _tag_directives, implicit)
        raise SpecError, DOCUMENT_SEPARATOR_ERROR unless implicit
      end

      def end_document(implicit)
        raise SpecError, DOCUMENT_SEPARATOR_ERROR unless implicit
      end
    end

    def self.load_file(path)
      new(File.read(path)).load
    end

    def initialize(text)
      @text = text
    end

    def load
      validate_parse_tree!
      data = safe_load
      reject_implicit_timestamps!(data)
      stringify_version_fields!(data)
      data
    end

    private

    def validate_parse_tree!
      Psych::Parser.new(DocumentMarkerHandler.new).parse(@text)
      stream = Psych.parse_stream(@text)
      raise SpecError, DOCUMENT_SEPARATOR_ERROR if stream.children.size > 1

      stream.children.each do |document|
        validate_node!(document.root) if document.root
      end
    rescue Psych::Exception => error
      raise SpecError, "Invalid YAML: #{error.message}"
    end

    def validate_node!(node)
      raise SpecError, ANCHOR_ALIAS_ERROR if node.is_a?(Psych::Nodes::Alias)
      raise SpecError, ANCHOR_ALIAS_ERROR if node.respond_to?(:anchor) && node.anchor
      raise SpecError, CUSTOM_TAG_ERROR if node.respond_to?(:tag) && node.tag

      case node
      when Psych::Nodes::Mapping
        validate_mapping_node!(node)
      when Psych::Nodes::Sequence, Psych::Nodes::Document, Psych::Nodes::Stream
        node.children.each { |child| validate_node!(child) } if node.children
      end
    end

    def validate_mapping_node!(node)
      seen_keys = {}

      node.children.each_slice(2) do |key_node, value_node|
        validate_node!(key_node)
        raise SpecError, ANCHOR_ALIAS_ERROR if key_node.is_a?(Psych::Nodes::Scalar) && key_node.value == '<<'

        key = duplicate_key_identity(key_node)
        raise SpecError, "Duplicate YAML key: #{describe_key(key_node)}" if seen_keys.key?(key)

        seen_keys[key] = true
        validate_node!(value_node)
      end
    end

    def duplicate_key_identity(node)
      value = Psych::Visitors::ToRuby.create.accept(node)
      reject_implicit_timestamps!(value)
      [value.class, value]
    rescue Psych::Exception => error
      raise SpecError, "Invalid YAML: #{error.message}"
    end

    def describe_key(node)
      return node.value.inspect if node.respond_to?(:value)

      node.class.name
    end

    def safe_load
      Psych.safe_load(
        @text,
        aliases: false,
        permitted_classes: [Date, DateTime, Time]
      )
    rescue Psych::Exception => error
      raise SpecError, "Invalid YAML: #{error.message}"
    end

    def reject_implicit_timestamps!(value)
      case value
      when Date, DateTime, Time
        raise SpecError, IMPLICIT_TIMESTAMP_ERROR
      when Hash
        value.each do |key, child|
          reject_implicit_timestamps!(key)
          reject_implicit_timestamps!(child)
        end
      when Array
        value.each { |child| reject_implicit_timestamps!(child) }
      end
    end

    def stringify_version_fields!(value)
      case value
      when Hash
        value.each do |key, child|
          if key == 'version' && !child.nil? && !child.is_a?(Hash) && !child.is_a?(Array)
            value[key] = child.to_s
          else
            stringify_version_fields!(child)
          end
        end
      when Array
        value.each { |child| stringify_version_fields!(child) }
      end
    end
  end

  class IpCoreParser
    def initialize(ipcore_path, views_dir)
      @ipcore_path = ipcore_path
      @views_dir = views_dir
    end

    def parse
      data = load_yaml
      validate_top_level(data)
      validate_runtime(data.fetch('runtime'))
      validate_topology_presets(data.fetch('topology_presets', []))
      validate_instance_parameters(data.fetch('instance_parameters', {}))
      validate_modules(data.fetch('modules'), data.fetch('buses', {}), has_buses: data.key?('buses'))
      views = ViewParser.new(@views_dir, data.fetch('modules')).parse
      ParsedSpec.new(data: data, views: views)
    end

    private

    def load_yaml
      ConstrainedYamlLoader.load_file(@ipcore_path).tap do |data|
        raise SpecError, 'IP core spec root must be a map' unless data.is_a?(Hash)
      end
    end

    def validate_top_level(data)
      validate_keys!(data, IPCORE_TOP_LEVEL_KEYS, 'IP core top-level')
      raise SpecError, 'schema must be finepaper.ipcore.v1' unless data['schema'] == 'finepaper.ipcore.v1'
      %w[id name version kind].each do |key|
        raise SpecError, "#{key} must be a string" unless data[key].is_a?(String)
      end
      raise SpecError, 'kind must be noc' unless data['kind'] == 'noc'
      raise SpecError, 'runtime must be a map' unless data['runtime'].is_a?(Hash)
      raise SpecError, 'modules must be a map' unless data['modules'].is_a?(Hash)
      raise SpecError, 'buses must be a map' if data.key?('buses') && !data['buses'].is_a?(Hash)
      validate_buses(data.fetch('buses', {})) if data.key?('buses')
    end

    def validate_runtime(runtime)
      validate_keys!(runtime, RUNTIME_KEYS, 'IP core runtime')
      validate_runtime_command(runtime, 'generator')
      validate_runtime_command(runtime, 'drc')
    end

    def validate_runtime_command(runtime, name)
      command = runtime[name]
      raise SpecError, "runtime.#{name} must be a map" unless command.is_a?(Hash)

      validate_keys!(command, COMMAND_KEYS, "IP core runtime.#{name}")
      %w[command input_format].each do |key|
        raise SpecError, "runtime.#{name}.#{key} must be a string" unless command[key].is_a?(String)
      end
      raise SpecError, "runtime.#{name}.args must be a list" unless command['args'].is_a?(Array)

      command['args'].each do |arg|
        raise SpecError, "runtime.#{name}.args entries must be strings" unless arg.is_a?(String)
      end
    end

    def validate_topology_presets(presets)
      raise SpecError, 'topology_presets must be a list' unless presets.is_a?(Array)

      presets.each do |preset|
        raise SpecError, 'topology preset must be a map' unless preset.is_a?(Hash)

        validate_keys!(preset, TOPOLOGY_PRESET_KEYS, "topology preset #{preset['id'] || '<unnamed>'}")
        %w[id label kind router_module id_pattern].each do |key|
          raise SpecError, "topology preset #{key} must be a string" unless preset[key].is_a?(String)
        end
        raise SpecError, "topology preset #{preset['id']} ports must be a map" unless preset['ports'].is_a?(Hash)
        preset['ports'].each do |name, value|
          raise SpecError, "topology preset #{preset['id']} port #{name} must be a string" unless value.is_a?(String)
        end
        raise SpecError, "topology preset #{preset['id']} parameters must be a map" unless preset['parameters'].is_a?(Hash)
        preset['parameters'].each do |name, parameter|
          raise SpecError, "topology preset #{preset['id']} parameter #{name} must be a map" unless parameter.is_a?(Hash)
          validate_keys!(parameter, TOPOLOGY_PRESET_PARAMETER_KEYS, "topology preset #{preset['id']} parameter #{name}")
          raise SpecError, "topology preset #{preset['id']} parameter #{name} label must be a string" unless parameter['label'].is_a?(String)
          %w[default min max].each do |key|
            unless parameter[key].is_a?(Integer)
              raise SpecError, "topology preset #{preset['id']} parameter #{name} #{key} must be an integer"
            end
          end
        end
      end
    end

    def validate_buses(buses)
      buses.each do |bus_name, bus|
        raise SpecError, "Bus #{bus_name} must be a map" unless bus.is_a?(Hash)

        validate_keys!(bus, BUS_KEYS, "bus #{bus_name}")
        validate_compatibility(bus_name, bus.fetch('compatibility'))
        validate_bus_config(bus_name, bus.fetch('config'))
        validate_signals(bus_name, bus.fetch('signals'))

        bus.fetch('compatibility').fetch('match').each do |field_name|
          field = bus.fetch('config')[field_name]
          raise SpecError, "Bus #{bus_name} compatibility field #{field_name} is missing config" unless field
          raise SpecError, "Bus #{bus_name} compatibility field #{field_name} must declare enum" unless field['enum'].is_a?(Array) && !field['enum'].empty?
        end
      end
    end

    def validate_compatibility(bus_name, compatibility)
      raise SpecError, "Bus #{bus_name} compatibility must be a map" unless compatibility.is_a?(Hash)

      validate_keys!(compatibility, COMPATIBILITY_KEYS, "bus #{bus_name} compatibility")
      raise SpecError, "Bus #{bus_name} compatibility.roles must be a map" unless compatibility['roles'].is_a?(Hash)
      raise SpecError, "Bus #{bus_name} compatibility.match must be a list" unless compatibility['match'].is_a?(Array)

      compatibility['roles'].each do |role, targets|
        raise SpecError, "Bus #{bus_name} role #{role} targets must be a list" unless targets.is_a?(Array)
      end
    end

    def validate_bus_config(bus_name, config)
      raise SpecError, "Bus #{bus_name} config must be a map" unless config.is_a?(Hash)

      config.each do |field_name, field|
        raise SpecError, "Bus #{bus_name} config #{field_name} must be a map" unless field.is_a?(Hash)

        validate_keys!(field, BUS_CONFIG_KEYS, "bus #{bus_name} config #{field_name}")
        validate_type_name!(field['type'], "Bus #{bus_name} config #{field_name}")
        validate_enum!(field, "Bus #{bus_name} config #{field_name}")
        validate_default!(field, "Bus #{bus_name} config #{field_name}", require_key: true)
      end
    end

    def validate_signals(bus_name, signals)
      raise SpecError, "Bus #{bus_name} signals must be a list" unless signals.is_a?(Array)

      signals.each do |signal|
        raise SpecError, "Bus #{bus_name} signal must be a map" unless signal.is_a?(Hash)

        validate_keys!(signal, %w[name direction width], "bus #{bus_name} signal")
        raise SpecError, "Bus #{bus_name} signal name must be a string" unless signal['name'].is_a?(String)
        raise SpecError, "Bus #{bus_name} signal direction must be a string" unless signal['direction'].is_a?(String)
      end
    end

    def validate_modules(modules, buses, has_buses:)
      raise SpecError, 'modules cannot be empty' if modules.empty?

      modules.each do |module_name, mod|
        raise SpecError, "Module #{module_name} must be a map" unless mod.is_a?(Hash)

        validate_keys!(mod, IPCORE_MODULE_KEYS, "module #{module_name}")
        %w[palette_label graph_group description].each do |key|
          raise SpecError, "Module #{module_name} #{key} must be a string" unless mod[key].is_a?(String)
        end
        validate_identity(module_name, mod.fetch('identity'))
        validate_capabilities(module_name, mod['capabilities']) if mod.key?('capabilities')
        validate_parameters(module_name, mod.fetch('parameters'))
        limits = mod.fetch('interface_limits', {})
        validate_interface_limits(module_name, limits, buses) if has_buses || mod.key?('interface_limits')
        validate_interfaces(module_name, mod.fetch('interfaces'), mod.fetch('parameters'), limits, buses, has_buses: has_buses)
      end
    end

    def validate_instance_parameters(parameters)
      raise SpecError, 'instance_parameters must be a map' unless parameters.is_a?(Hash)

      validate_parameter_map('instance', parameters, 'instance parameter')
    end

    def validate_identity(module_name, identity)
      raise SpecError, "Module #{module_name} identity must be a map" unless identity.is_a?(Hash)

      validate_keys!(identity, IDENTITY_KEYS, "module #{module_name} identity")
      %w[external_id_prefix display_prefix].each do |key|
        raise SpecError, "Module #{module_name} identity.#{key} must be a string" unless identity[key].is_a?(String)
      end
      raise SpecError, "Module #{module_name} identity.width must be an integer" unless identity['width'].is_a?(Integer)
      unless identity['supports_mesh_coordinates'] == true || identity['supports_mesh_coordinates'] == false
        raise SpecError, "Module #{module_name} identity.supports_mesh_coordinates must be a bool"
      end
    end

    def validate_capabilities(module_name, capabilities)
      raise SpecError, "Module #{module_name} capabilities must be a map" unless capabilities.is_a?(Hash)

      validate_keys!(capabilities, CAPABILITY_KEYS, "module #{module_name} capabilities")
      if capabilities.key?('supports_collapse') &&
         capabilities['supports_collapse'] != true &&
         capabilities['supports_collapse'] != false
        raise SpecError, "Module #{module_name} capabilities.supports_collapse must be a bool"
      end
    end

    def validate_parameters(module_name, parameters)
      raise SpecError, "Module #{module_name} parameters must be a map" unless parameters.is_a?(Hash)

      validate_parameter_map(module_name, parameters, "Module #{module_name} parameter")
    end

    def validate_parameter_map(owner_name, parameters, context_prefix)
      parameters.each do |param_name, param|
        context = "#{context_prefix} #{param_name}"
        raise SpecError, "#{context} must be a map" unless param.is_a?(Hash)

        validate_keys!(param, PARAMETER_KEYS, "#{owner_name} parameter #{param_name}")
        validate_type_name!(param['type'], context)
        raise SpecError, "#{context} default is required" unless param.key?('default')
        if param.key?('emit') && !EMIT_MODES.include?(param['emit'])
          raise SpecError, "#{context} emit is invalid"
        end
        validate_enum!(param, context)
        validate_parameter_labels!(param, context)
        validate_default!(param, context)
        validate_parameter_bounds!(param, context)
      end
    end

    def validate_parameter_bounds!(param, context)
      if param.key?('configurable') && param['configurable'] != true && param['configurable'] != false
        raise SpecError, "#{context} configurable must be a bool"
      end
      %w[min max].each do |key|
        raise SpecError, "#{context} #{key} must be an integer" if param.key?(key) && !param[key].is_a?(Integer)
      end
    end

    def validate_interface_limits(module_name, limits, buses)
      raise SpecError, "Module #{module_name} interface_limits must be a map" unless limits.is_a?(Hash)

      limits.each do |bus_name, limit|
        raise SpecError, "Module #{module_name} interface_limits references unknown bus #{bus_name}" unless buses.key?(bus_name)
        raise SpecError, "Module #{module_name} interface_limits #{bus_name} must be a map" unless limit.is_a?(Hash)
        validate_keys!(limit, %w[max], "module #{module_name} interface_limits #{bus_name}")
        raise SpecError, "Module #{module_name} interface_limits #{bus_name}.max must be an integer" unless limit['max'].is_a?(Integer)
      end
    end

    def validate_interfaces(module_name, interfaces, parameters, limits, buses, has_buses:)
      raise SpecError, "Module #{module_name} interfaces must be a map" unless interfaces.is_a?(Hash)

      interfaces.each do |interface_name, interface|
        raise SpecError, "Module #{module_name} interface #{interface_name} must be a map" unless interface.is_a?(Hash)

        validate_keys!(interface, IPCORE_INTERFACE_KEYS, "module #{module_name} interface #{interface_name}")
        %w[label bus role].each do |key|
          raise SpecError, "#{module_name}.#{interface_name} #{key} must be a string" unless interface[key].is_a?(String)
        end
        validate_interface_compatibility(module_name, interface_name, interface, parameters, buses, has_buses: has_buses)
        validate_interface_metadata(module_name, interface_name, interface)
        validate_port_projection(module_name, interface_name, interface)
      end

      limits.each do |bus_name, limit|
        count = interfaces.count { |_, interface| interface['bus'] == bus_name }
        if count > limit['max']
          raise SpecError, "Module #{module_name} has #{count} #{bus_name} interfaces, max is #{limit['max']}"
        end
      end
    end

    def validate_interface_compatibility(module_name, interface_name, interface, parameters, buses, has_buses:)
      if has_buses
        bus_name = interface.fetch('bus')
        bus = buses[bus_name]
        raise SpecError, "#{module_name}.#{interface_name} references unknown bus #{bus_name}" unless bus
        unless bus.fetch('compatibility').fetch('roles').key?(interface['role'])
          raise SpecError, "#{module_name}.#{interface_name} role #{interface['role']} is not defined by #{bus_name}"
        end
        validate_optional_connects_to(module_name, interface_name, interface)
        validate_optional_match(module_name, interface_name, interface)
        validate_accepts(module_name, interface_name, bus_name, interface['accepts'], bus) if interface.key?('accepts')
        validate_interface_config(module_name, interface_name, bus_name, interface['config'], parameters, bus) if interface.key?('config')
      else
        raise SpecError, "#{module_name}.#{interface_name} connects_to must be a string" unless interface['connects_to'].is_a?(String)
        raise SpecError, "#{module_name}.#{interface_name} match must be a list" unless interface['match'].is_a?(Array)
        interface['match'].each do |field|
          raise SpecError, "#{module_name}.#{interface_name} match entries must be strings" unless field.is_a?(String)
        end
      end
    end

    def validate_optional_connects_to(module_name, interface_name, interface)
      return unless interface.key?('connects_to')

      raise SpecError, "#{module_name}.#{interface_name} connects_to must be a string" unless interface['connects_to'].is_a?(String)
    end

    def validate_optional_match(module_name, interface_name, interface)
      return unless interface.key?('match')

      raise SpecError, "#{module_name}.#{interface_name} match must be a list" unless interface['match'].is_a?(Array)
      interface['match'].each do |field|
        raise SpecError, "#{module_name}.#{interface_name} match entries must be strings" unless field.is_a?(String)
      end
    end

    def validate_interface_metadata(module_name, interface_name, interface)
      INTERFACE_METADATA_KEYS.each do |key|
        next unless interface.key?(key)

        unless interface[key].is_a?(String)
          raise SpecError, "#{module_name}.#{interface_name} #{key} must be a string"
        end

        validate_interface_metadata_value(module_name, interface_name, key, interface[key])
      end
    end

    def validate_interface_metadata_value(module_name, interface_name, key, value)
      case key
      when 'cardinality'
        unless INTERFACE_CARDINALITIES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} cardinality is invalid"
        end
      when 'topology_rule'
        unless INTERFACE_TOPOLOGY_RULES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} topology_rule is invalid"
        end
      end
    end

    def validate_accepts(module_name, interface_name, bus_name, accepts, bus)
      raise SpecError, "#{module_name}.#{interface_name} accepts must be a map" unless accepts.is_a?(Hash)

      accepts.each do |field_name, values|
        field = bus.fetch('config')[field_name]
        raise SpecError, "#{module_name}.#{interface_name} accepts unknown #{bus_name} config #{field_name}" unless field
        raise SpecError, "#{module_name}.#{interface_name} accepts #{field_name} must be a list" unless values.is_a?(Array)
        raise SpecError, "#{module_name}.#{interface_name} accepts #{field_name} cannot be empty" if values.empty?
        raise SpecError, "#{module_name}.#{interface_name} accepts #{field_name} requires #{bus_name} enum" unless field['enum'].is_a?(Array)

        values.each do |value|
          unless field['enum'].include?(value)
            raise SpecError, "#{module_name}.#{interface_name} accepts #{field_name} value #{value} outside #{bus_name} enum"
          end
        end
      end
    end

    def validate_interface_config(module_name, interface_name, bus_name, config, parameters, bus)
      raise SpecError, "#{module_name}.#{interface_name} config must be a map" unless config.is_a?(Hash)

      config.each do |field_name, binding|
        bus_field = bus.fetch('config')[field_name]
        raise SpecError, "#{module_name}.#{interface_name} config references unknown #{bus_name} config #{field_name}" unless bus_field
        unless binding.is_a?(Hash) && binding.keys == ['parameter']
          raise SpecError, "#{module_name}.#{interface_name} config #{field_name} must be { parameter: name }"
        end

        parameter_name = binding['parameter']
        parameter = parameters[parameter_name]
        raise SpecError, "#{module_name}.#{interface_name} config #{field_name} references unknown parameter #{parameter_name}" unless parameter
        raise SpecError, "#{module_name}.#{interface_name} config #{field_name} type does not match #{parameter_name}" unless parameter['type'] == bus_field['type']

        next unless bus_field['enum']

        raise SpecError, "#{module_name}.#{interface_name} parameter #{parameter_name} must declare enum" unless parameter['enum'].is_a?(Array)
        parameter['enum'].each do |value|
          unless bus_field['enum'].include?(value)
            raise SpecError, "#{module_name}.#{interface_name} parameter #{parameter_name} enum value #{value} outside #{bus_name} enum"
          end
        end
      end
    end

    def validate_port_projection(module_name, interface_name, interface)
      raise SpecError, "#{module_name}.#{interface_name} must define port or ports" unless interface.key?('port') || interface.key?('ports')
      raise SpecError, "#{module_name}.#{interface_name} cannot define both port and ports" if interface.key?('port') && interface.key?('ports')

      projections = interface.key?('port') ? [interface['port']] : interface['ports']
      raise SpecError, "#{module_name}.#{interface_name} ports must be a list" unless projections.is_a?(Array)

      projections.each do |port|
        raise SpecError, "#{module_name}.#{interface_name} port must be a map" unless port.is_a?(Hash)

        validate_keys!(port, PORT_KEYS, "#{module_name}.#{interface_name} port")
        raise SpecError, "#{module_name}.#{interface_name} port direction is invalid" unless PORT_DIRECTIONS.include?(port['direction'])
      end
    end

    def validate_keys!(hash, allowed, context)
      hash.each_key do |key|
        next if allowed.include?(key)

        raise SpecError, "Unknown #{context} field: #{key}"
      end
    end

    def validate_type_name!(type, context)
      raise SpecError, "#{context} type is invalid" unless PARAMETER_TYPES.include?(type)
    end

    def validate_enum!(field, context)
      return unless field.key?('enum')

      raise SpecError, "#{context} enum must be a list" unless field['enum'].is_a?(Array)
      raise SpecError, "#{context} enum cannot be empty" if field['enum'].empty?
      field['enum'].each do |value|
        raise SpecError, "#{context} enum value #{value.inspect} does not match #{field['type']}" unless value_matches_type?(value, field['type'])
      end
    end

    def validate_parameter_labels!(field, context)
      return unless field.key?('labels')

      raise SpecError, "#{context} labels must be a map" unless field['labels'].is_a?(Hash)
      raise SpecError, "#{context} labels require enum" unless field['enum'].is_a?(Array)

      enum_values = field['enum'].map(&:to_s)
      field['labels'].each do |value, label|
        raise SpecError, "#{context} label key #{value} is outside enum" unless enum_values.include?(value.to_s)
        raise SpecError, "#{context} label for #{value} must be a string" unless label.is_a?(String)
      end
    end

    def validate_default!(field, context, require_key: false)
      return if !require_key && !field.key?('default')

      value = field['default']
      raise SpecError, "#{context} default #{value.inspect} does not match #{field['type']}" unless value_matches_type?(value, field['type'])
      return unless field['enum'] && !field['enum'].include?(value)

      raise SpecError, "#{context} default #{value.inspect} is outside enum"
    end

    def value_matches_type?(value, type)
      case type
      when 'string' then value.is_a?(String)
      when 'int' then value.is_a?(Integer)
      when 'bool' then value == true || value == false
      else false
      end
    end
  end

  class ViewParser
    def initialize(views_dir, modules)
      @views_dir = views_dir
      @modules = modules
    end

    def parse
      @modules.each_key.to_h do |module_name|
        view = parse_view(module_name)
        validate_view_refs!(view)
        [module_name, view]
      end
    end

    private

    def parse_view(module_name)
      path = File.join(@views_dir, "#{module_name}.xml")
      raise SpecError, "Missing view XML for #{module_name}: #{path}" unless File.file?(path)

      text = File.read(path)
      declared_module = text[/<module-view\b[^>]*\bmodule="([^"]+)"/, 1]
      if declared_module
        raise SpecError, "view #{module_name} declares module #{declared_module}" unless declared_module == module_name

        schema = text[/<module-view\b[^>]*\bschema="([^"]+)"/, 1]
        raise SpecError, "view #{module_name} schema must be v1" unless schema == 'v1'
      elsif text.match?(/<module-view\b/)
        raise SpecError, "view #{module_name} is missing module attribute"
      elsif !bare_graphics_view?(text)
        raise SpecError, "view #{module_name} is missing module attribute"
      end

      graphics_xml = normalize_indentation(text[%r{(<graphics\b.*?</graphics>)}m, 1])
      raise SpecError, "view #{module_name} must contain graphics element" unless graphics_xml

      anchors_xml = normalize_indentation(text[%r{(<anchors\b.*?</anchors>)}m, 1])
      graphics_xml = strip_nested_anchors(graphics_xml) if anchors_xml
      refs = text.scan(/<(?:interface|anchor)\b[^>]*\bref="([^"]+)"/).flatten
      View.new(
        module_name: module_name,
        graphics_xml: graphics_xml.strip,
        anchors_xml: anchors_xml&.strip,
        interface_refs: refs
      )
    end

    def normalize_indentation(text)
      return nil unless text

      lines = text.strip.lines.map(&:rstrip)
      rest_indent = lines.drop(1).reject(&:empty?).map { |line| line[/\A */].size }.min || 0
      ([lines.first.strip] + lines.drop(1).map { |line| line.sub(/\A {0,#{rest_indent}}/, '') }).join("\n")
    end

    def strip_nested_anchors(text)
      normalize_indentation(text.sub(%r{\n?\s*<anchors\b.*?</anchors>}m, ''))
    end

    def bare_graphics_view?(text)
      text.match?(/\A\s*(?:<\?xml[^>]*>\s*)?<graphics\b/m)
    end

    def validate_view_refs!(view)
      interfaces = @modules.fetch(view.module_name).fetch('interfaces')
      view.interface_refs.each do |ref|
        raise SpecError, "view #{view.module_name} references unknown interface #{ref}" unless interfaces.key?(ref)
      end
    end
  end

  class QtBundleEmitter
    def initialize(parsed)
      @spec = parsed.data
      @views = parsed.views
    end

    def write(qt_bundle_dir)
      FileUtils.mkdir_p(File.join(qt_bundle_dir, 'graphics'))
      File.write(File.join(qt_bundle_dir, 'modules.xml'), modules_xml)

      @views.each do |module_name, view|
        File.write(
          File.join(qt_bundle_dir, 'graphics', "#{module_name}.xml"),
          graphics_xml(module_name, view)
        )
      end
    end

    private

    def modules_xml
      lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<module-bundle>']
      lines.concat(bus_lines) if @spec.key?('buses')
      @spec.fetch('modules').each do |module_name, mod|
        lines.concat(module_lines(module_name, mod))
      end
      lines << '</module-bundle>'
      "#{lines.join("\n")}\n"
    end

    def bus_lines
      lines = ['  <buses>']
      @spec.fetch('buses').each do |bus_name, bus|
        lines << "    <bus#{attrs(name: bus_name, description: bus['description'])}>"
        lines << '      <compatibility>'
        bus.fetch('compatibility').fetch('roles').each do |role, targets|
          lines << "        <role#{attrs(name: role, connects_to: targets.join(','))} />"
        end
        bus.fetch('compatibility').fetch('match').each do |field|
          lines << "        <match#{attrs(field: field)} />"
        end
        lines << '      </compatibility>'
        lines << '      <config>'
        bus.fetch('config').each do |field_name, field|
          lines << "        <field#{attrs(name: field_name, type: field['type'], default: field['default'], enum: field['enum']&.join(','), description: field['description'])} />"
        end
        lines << '      </config>'
        lines << '      <signals>'
        bus.fetch('signals').each do |signal|
          lines << "        <signal#{attrs(name: signal['name'], direction: signal['direction'], width: signal['width'])} />"
        end
        lines << '      </signals>'
        lines << '    </bus>'
      end
      lines << '  </buses>'
      lines
    end

    def module_lines(module_name, mod)
      lines = [
        "  <module#{attrs(name: module_name, palette_label: mod['palette_label'], graph_group: mod['graph_group'], description: mod['description'])}>"
      ]
      lines << "    <identity#{attrs(mod.fetch('identity'))} />"
      if mod['capabilities']
        lines << "    <capabilities#{attrs(mod['capabilities'])} />"
      end
      lines.concat(interface_lines(mod.fetch('interfaces')))
      lines.concat(port_lines(mod.fetch('interfaces')))
      lines.concat(parameter_lines(mod.fetch('parameters')))
      lines << '  </module>'
      lines
    end

    def interface_lines(interfaces)
      lines = ['    <interfaces>']
      interfaces.each do |interface_name, interface|
        compatibility = @spec.fetch('buses', {}).fetch(interface.fetch('bus'), {}).fetch('compatibility', {})
        interface_attrs = {
          id: interface_name,
          label: interface['label'],
          bus: interface['bus'],
          role: interface['role'],
          connects_to: interface['connects_to'] || compatibility.fetch('roles').fetch(interface.fetch('role')).join(','),
          match: interface.fetch('match', compatibility.fetch('match', [])).join(','),
          cardinality: interface['cardinality'],
          autocomplete_group: interface['autocomplete_group'],
          topology_rule: interface['topology_rule']
        }
        lines << "      <interface#{attrs(interface_attrs)}>"
        interface.fetch('accepts', {}).each do |field, values|
          lines << "        <accept#{attrs(field: field, values: values.join(','))} />"
        end
        interface.fetch('config', {}).each do |field, binding|
          lines << "        <config#{attrs(field: field, parameter: binding['parameter'])} />"
        end
        lines << '      </interface>'
      end
      lines << '    </interfaces>'
      lines
    end

    def port_lines(interfaces)
      lines = ['    <ports>']
      interfaces.each do |interface_name, interface|
        projected_ports(interface).each do |port|
          lines << "      <port#{attrs(port.merge('interface' => interface_name))} />"
        end
      end
      lines << '    </ports>'
      lines
    end

    def parameter_lines(parameters)
      lines = ['    <parameters>']
      parameters.each do |name, parameter|
        param_attrs = {
          name: name,
          type: parameter.fetch('type'),
          default: parameter.fetch('default'),
          label: parameter['label'],
          description: parameter['description'],
          configurable: parameter.key?('configurable') ? parameter['configurable'] : nil,
          min: parameter['min'],
          max: parameter['max']
        }

        if parameter['enum']
          lines << "      <parameter#{attrs(param_attrs)}>"
          lines << '        <choices>'
          parameter['enum'].each do |choice|
          lines << "          <choice#{attrs(value: choice, label: choice_label(parameter, choice))} />"
          end
          lines << '        </choices>'
          lines << '      </parameter>'
        else
          lines << "      <parameter#{attrs(param_attrs)} />"
        end
      end
      lines << '    </parameters>'
      lines
    end

    def projected_ports(interface)
      return [interface.fetch('port')] if interface.key?('port')

      interface.fetch('ports')
    end

    def choice_label(parameter, choice)
      labels = parameter.fetch('labels', {})
      labels.fetch(choice, labels.fetch(choice.to_s, choice))
    end

    def graphics_xml(module_name, view)
      <<~XML
        <?xml version="1.0" encoding="UTF-8"?>
        <module-graphics type="#{escape(module_name)}">
        #{indent(view.graphics_xml, 2)}
        #{indent(view.anchors_xml, 2) if view.anchors_xml}
        </module-graphics>
      XML
    end

    def attrs(values)
      values.filter_map do |key, value|
        next if value.nil?

        %(#{key}="#{escape(value)}")
      end.then { |items| items.empty? ? '' : " #{items.join(' ')}" }
    end

    def indent(text, spaces)
      prefix = ' ' * spaces
      text.lines.map { |line| "#{prefix}#{line.rstrip}" }.join("\n")
    end

    def escape(value)
      value.to_s
           .gsub('&', '&amp;')
           .gsub('"', '&quot;')
           .gsub('<', '&lt;')
           .gsub('>', '&gt;')
    end
  end

  class IpCoreRuntimeEmitter < QtBundleEmitter
    def initialize(parsed, source_root:)
      super(parsed)
      @source_root = File.expand_path(source_root)
    end

    def write(bundle_dir)
      @bundle_dir = File.expand_path(bundle_dir)
      FileUtils.mkdir_p(File.join(bundle_dir, 'graphics'))
      FileUtils.rm_f(File.join(bundle_dir, SpecGenerator.stale_runtime_manifest_file_name))
      File.write(File.join(bundle_dir, 'ipcore-runtime.json'), runtime_json)
      File.write(File.join(bundle_dir, 'modules.xml'), modules_xml)

      @views.each do |module_name, view|
        File.write(
          File.join(bundle_dir, 'graphics', "#{module_name}.xml"),
          graphics_xml(module_name, view)
        )
      end
    end

    private

    def runtime_json
      runtime = @spec.fetch('runtime')
      generator = runtime.fetch('generator')
      drc = runtime.fetch('drc')
      JSON.pretty_generate(
        {
          id: @spec.fetch('id'),
          name: @spec.fetch('name'),
          version: @spec.fetch('version'),
          kind: @spec.fetch('kind'),
          source_root: source_root_relative_to_bundle,
          instance_parameters: @spec.fetch('instance_parameters', {}),
          modules: 'modules.xml',
          graphics: 'graphics',
          generator: {
            command: generator.fetch('command'),
            input_format: generator.fetch('input_format'),
            args: generator.fetch('args')
          },
          drc: {
            command: drc.fetch('command'),
            input_format: drc.fetch('input_format'),
            args: drc.fetch('args')
          },
          topology_presets: @spec.fetch('topology_presets', [])
        }
      ) + "\n"
    end

    def source_root_relative_to_bundle
      Pathname.new(@source_root).relative_path_from(Pathname.new(@bundle_dir)).to_s
    end
  end

  class RubyModelEmitter
    TYPE_MAP = {
      'string' => ':string',
      'int' => ':integer',
      'bool' => ':boolean'
    }.freeze

    def initialize(spec)
      @spec = spec
    end

    def write(ruby_model_dir)
      FileUtils.mkdir_p(ruby_model_dir)
      File.write(File.join(ruby_model_dir, 'xp.rb'), xp_model)
      File.write(File.join(ruby_model_dir, 'endpoint.rb'), endpoint_model)
    end

    private

    def xp_model
      xp = module_by_graph_group('xps')
      <<~RUBY
        class Xp
          attr_reader :id, :x, :y, :endpoints, :config

          def self.config_schema
        #{schema_literal(config_parameters(xp), 4)}
          end

          def initialize(id, x, y, endpoints, config = {})
            @id = id
            @x = x
            @y = y
            @endpoints = endpoints
            @config = self.class.config_schema.transform_values { |v| v[:default] }.merge(config)
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
      RUBY
    end

    def endpoint_model
      endpoint = module_by_graph_group('endpoints')
      attribute_names = attribute_parameters(endpoint).keys
      args = ['id', *attribute_names, 'config = {}']
      assignments = ['@id = id'] + attribute_names.map { |name| "@#{name} = #{name}" }

      <<~RUBY
        class Endpoint
          attr_reader :id, #{attribute_names.map { |name| ":#{name}" }.join(', ')}, :config
          attr_accessor :ports, :template

          def self.config_schema
        #{schema_literal(config_parameters(endpoint), 4)}
          end

          def initialize(#{args.join(', ')})
        #{assignments.map { |line| "    #{line}" }.join("\n")}
            @config = self.class.config_schema.transform_values { |v| v[:default] }.merge(config)
          end
        end
      RUBY
    end

    def attribute_parameters(mod)
      mod.fetch('parameters').select { |_, parameter| parameter['emit'] == 'attribute' }
    end

    def config_parameters(mod)
      mod.fetch('parameters').select { |_, parameter| parameter['emit'] == 'config' }
    end

    def module_by_graph_group(graph_group)
      found = @spec.fetch('modules').values.select { |mod| mod['graph_group'] == graph_group }
      raise SpecError, "Expected exactly one module with graph_group #{graph_group}" unless found.size == 1

      found.first
    end

    def schema_literal(parameters, indent)
      prefix = ' ' * indent
      inner_prefix = ' ' * (indent + 2)
      return "#{prefix}{}" if parameters.empty?

      lines = ["#{prefix}{"]
      parameters.each_with_index do |(name, parameter), index|
        suffix = index == parameters.size - 1 ? '' : ','
        lines << "#{inner_prefix}#{name}: #{schema_entry(parameter)}#{suffix}"
      end
      lines << "#{prefix}}"
      lines.join("\n")
    end

    def schema_entry(parameter)
      values = [
        "type: #{TYPE_MAP.fetch(parameter.fetch('type'))}",
        "default: #{ruby_literal(parameter.fetch('default'))}"
      ]
      values << "enum: #{ruby_literal(parameter['enum'])}" if parameter['enum']
      "{ #{values.join(', ')} }"
    end

    def ruby_literal(value)
      case value
      when String
        "'#{value.gsub('\\', '\\\\').gsub("'", "\\\\'")}'"
      when Array
        "[#{value.map { |item| ruby_literal(item) }.join(', ')}]"
      else
        value.inspect
      end
    end
  end
end
