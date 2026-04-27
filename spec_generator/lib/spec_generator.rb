require 'fileutils'
require 'yaml'

module SpecGenerator
  class SpecError < StandardError; end

  TOP_LEVEL_KEYS = %w[schema kind name version buses modules].freeze
  BUS_KEYS = %w[description ipxact compatibility config signals].freeze
  BUS_CONFIG_KEYS = %w[type enum default description].freeze
  COMPATIBILITY_KEYS = %w[roles match].freeze
  MODULE_KEYS = %w[
    palette_label graph_group description identity capabilities interface_limits parameters interfaces
  ].freeze
  IDENTITY_KEYS = %w[external_id_prefix display_prefix width supports_mesh_coordinates].freeze
  CAPABILITY_KEYS = %w[supports_collapse].freeze
  PARAMETER_KEYS = %w[type default enum label description emit configurable min max].freeze
  INTERFACE_KEYS = %w[label bus role accepts config port ports].freeze
  PORT_KEYS = %w[id direction type bus_type role name description].freeze
  PARAMETER_TYPES = %w[string int bool].freeze
  EMIT_MODES = %w[attribute config editor editor_only].freeze
  PORT_DIRECTIONS = %w[input output inout].freeze

  View = Struct.new(:module_name, :graphics_xml, :anchors_xml, :interface_refs, keyword_init: true)
  ParsedSpec = Struct.new(:data, :views, keyword_init: true)

  def self.generate(spec_path:, views_dir:, qt_bundle_dir:, ruby_model_dir:)
    parsed = Parser.new(spec_path, views_dir).parse
    QtBundleEmitter.new(parsed).write(qt_bundle_dir)
    RubyModelEmitter.new(parsed.data).write(ruby_model_dir)
  end

  class Parser
    def initialize(spec_path, views_dir)
      @spec_path = spec_path
      @views_dir = views_dir
    end

    def parse
      data = load_yaml
      validate_top_level(data)
      validate_buses(data.fetch('buses'))
      validate_modules(data.fetch('modules'), data.fetch('buses'))
      views = ViewParser.new(@views_dir, data.fetch('modules')).parse
      ParsedSpec.new(data: data, views: views)
    end

    private

    def load_yaml
      YAML.safe_load(File.read(@spec_path), aliases: false).tap do |data|
        raise SpecError, 'Spec root must be a map' unless data.is_a?(Hash)
      end
    rescue Psych::Exception => error
      raise SpecError, "Invalid YAML: #{error.message}"
    end

    def validate_top_level(data)
      validate_keys!(data, TOP_LEVEL_KEYS, 'top-level')
      raise SpecError, 'schema must be v1' unless data['schema'] == 'v1'
      raise SpecError, 'kind must be noc-definition' unless data['kind'] == 'noc-definition'
      raise SpecError, 'name must be a string' unless data['name'].is_a?(String)
      raise SpecError, 'version must be a string' unless data['version'].is_a?(String)
      raise SpecError, 'buses must be a map' unless data['buses'].is_a?(Hash)
      raise SpecError, 'modules must be a map' unless data['modules'].is_a?(Hash)
    end

    def validate_buses(buses)
      buses.each do |bus_name, bus|
        raise SpecError, "Bus #{bus_name} must be a map" unless bus.is_a?(Hash)

        validate_keys!(bus, BUS_KEYS, "bus #{bus_name}")
        validate_compatibility(bus_name, bus.fetch('compatibility'))
        validate_bus_config(bus_name, bus.fetch('config'))
        validate_signals(bus_name, bus.fetch('signals'))

        match_fields = bus.fetch('compatibility').fetch('match')
        match_fields.each do |field_name|
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
        validate_default!(field, "Bus #{bus_name} config #{field_name}")
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

    def validate_modules(modules, buses)
      raise SpecError, 'modules cannot be empty' if modules.empty?

      modules.each do |module_name, mod|
        raise SpecError, "Module #{module_name} must be a map" unless mod.is_a?(Hash)

        validate_keys!(mod, MODULE_KEYS, "module #{module_name}")
        validate_identity(module_name, mod.fetch('identity'))
        validate_capabilities(module_name, mod['capabilities']) if mod.key?('capabilities')
        validate_parameters(module_name, mod.fetch('parameters'))
        validate_interface_limits(module_name, mod.fetch('interface_limits', {}), buses)
        validate_interfaces(module_name, mod.fetch('interfaces'), mod.fetch('parameters'), mod.fetch('interface_limits', {}), buses)
      end
    end

    def validate_identity(module_name, identity)
      raise SpecError, "Module #{module_name} identity must be a map" unless identity.is_a?(Hash)

      validate_keys!(identity, IDENTITY_KEYS, "module #{module_name} identity")
    end

    def validate_capabilities(module_name, capabilities)
      raise SpecError, "Module #{module_name} capabilities must be a map" unless capabilities.is_a?(Hash)

      validate_keys!(capabilities, CAPABILITY_KEYS, "module #{module_name} capabilities")
    end

    def validate_parameters(module_name, parameters)
      raise SpecError, "Module #{module_name} parameters must be a map" unless parameters.is_a?(Hash)

      parameters.each do |param_name, param|
        raise SpecError, "Module #{module_name} parameter #{param_name} must be a map" unless param.is_a?(Hash)

        validate_keys!(param, PARAMETER_KEYS, "module #{module_name} parameter #{param_name}")
        validate_type_name!(param['type'], "Module #{module_name} parameter #{param_name}")
        raise SpecError, "Module #{module_name} parameter #{param_name} emit is invalid" unless EMIT_MODES.include?(param['emit'])
        validate_enum!(param, "Module #{module_name} parameter #{param_name}")
        validate_default!(param, "Module #{module_name} parameter #{param_name}")
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

    def validate_interfaces(module_name, interfaces, parameters, limits, buses)
      raise SpecError, "Module #{module_name} interfaces must be a map" unless interfaces.is_a?(Hash)

      interfaces.each do |interface_name, interface|
        raise SpecError, "Module #{module_name} interface #{interface_name} must be a map" unless interface.is_a?(Hash)

        validate_keys!(interface, INTERFACE_KEYS, "module #{module_name} interface #{interface_name}")
        bus_name = interface.fetch('bus')
        bus = buses[bus_name]
        raise SpecError, "#{module_name}.#{interface_name} references unknown bus #{bus_name}" unless bus
        raise SpecError, "#{module_name}.#{interface_name} role #{interface['role']} is not defined by #{bus_name}" unless bus.fetch('compatibility').fetch('roles').key?(interface['role'])

        validate_accepts(module_name, interface_name, bus_name, interface['accepts'], bus) if interface.key?('accepts')
        validate_interface_config(module_name, interface_name, bus_name, interface['config'], parameters, bus) if interface.key?('config')
        validate_port_projection(module_name, interface_name, interface)
      end

      limits.each do |bus_name, limit|
        count = interfaces.count { |_, interface| interface['bus'] == bus_name }
        if count > limit['max']
          raise SpecError, "Module #{module_name} has #{count} #{bus_name} interfaces, max is #{limit['max']}"
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
        raise SpecError, "#{module_name}.#{interface_name} config #{field_name} must be { parameter: name }" unless binding.is_a?(Hash) && binding.keys == ['parameter']

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

        if context == 'top-level'
          raise SpecError, "Unknown top-level field: #{key}"
        end
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

    def validate_default!(field, context)
      return unless field.key?('default')

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
      raise SpecError, "view #{module_name} is missing module attribute" unless declared_module
      raise SpecError, "view #{module_name} declares module #{declared_module}" unless declared_module == module_name

      schema = text[/<module-view\b[^>]*\bschema="([^"]+)"/, 1]
      raise SpecError, "view #{module_name} schema must be v1" unless schema == 'v1'

      graphics_xml = normalize_indentation(text[%r{(<graphics\b.*?</graphics>)}m, 1])
      raise SpecError, "view #{module_name} must contain graphics element" unless graphics_xml

      anchors_xml = normalize_indentation(text[%r{(<anchors\b.*?</anchors>)}m, 1])
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
      lines.concat(bus_lines)
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
        bus = @spec.fetch('buses').fetch(interface.fetch('bus'))
        compatibility = bus.fetch('compatibility')
        lines << "      <interface#{attrs(id: interface_name, label: interface['label'], bus: interface['bus'], role: interface['role'], connects_to: compatibility.fetch('roles').fetch(interface.fetch('role')).join(','), match: compatibility.fetch('match').join(','))}>"
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
            lines << "          <choice#{attrs(value: choice, label: choice)} />"
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
