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
  IPCRAFT_PACKAGE_TOP_LEVEL_KEYS = %w[
    schema id name version plugin extensions ipxact parameters connection_classes modules views topologies commands
  ].freeze
  IPCRAFT_PLUGIN_KEYS = %w[library entry].freeze
  IPCRAFT_IPXACT_KEYS = %w[root generated].freeze
  IPCRAFT_CONNECTION_CLASS_KEYS = %w[id roles symmetric ipxact].freeze
  IPCRAFT_MODULE_KEYS = %w[id name description graph_role attach parameters interfaces].freeze
  IPCRAFT_INTERFACE_KEYS = %w[id name label modes accepts multi_connection ipxact parameters].freeze
  IPCRAFT_ACCEPT_KEYS = %w[class role].freeze
  IPCRAFT_VIEW_KEYS = %w[module file].freeze
  IPCRAFT_COMMAND_KEYS = %w[executable input_schema args].freeze
  IPCRAFT_EXTENSION_MODE_KEYS = %w[ipxact].freeze
  IPCRAFT_EXTENSION_MODE_IPXACT_KEYS = %w[mode].freeze
  IPCRAFT_INTERFACE_IPXACT_KEYS = %w[bus_interface mode modes].freeze
  IPCRAFT_INTERFACE_IPXACT_MODE_KEYS = %w[mode].freeze
  IPXACT_NATIVE_MODES = %w[
    initiator target system mirroredInitiator mirroredTarget mirroredSystem monitor
  ].freeze
  NOC_EXTENSION_ID = 'noc.v1'.freeze
  NOC_CHI_CONNECTION_ROLES = %w[node interconnect peer].freeze
  NOC_CHI_MODE_MAPPINGS = {
    'chi_interconnect' => { 'ipxact' => { 'mode' => 'target' } },
    'chi_requester_node' => { 'ipxact' => { 'mode' => 'initiator' } },
    'chi_home_node' => { 'ipxact' => { 'mode' => 'initiator' } },
    'chi_subordinate_node' => { 'ipxact' => { 'mode' => 'initiator' } },
    'chi_peer' => { 'ipxact' => { 'mode' => 'system' } }
  }.freeze

  View = Struct.new(:module_name, :graphics_xml, :anchors_xml, :interface_refs, keyword_init: true)
  ParsedSpec = Struct.new(:data, :views, keyword_init: true)

  def self.generate_ipcore(ipcore_path:, views_dir:, runtime_bundle_dir:, ruby_model_dir: nil)
    parsed = IpCoreParser.new(ipcore_path, views_dir).parse
    IpCoreRuntimeEmitter.new(parsed, source_root: File.dirname(ipcore_path)).write(runtime_bundle_dir)
    RubyModelEmitter.new(parsed.data).write(ruby_model_dir) if ruby_model_dir
  end

  def self.check_ipcraft_package_source(ipcore_path:, package_root: File.dirname(ipcore_path))
    IpcraftPackageParser.new(ipcore_path, package_root).parse
    true
  end

  def self.build_ipcraft_manifest(ipcore_path:, package_root:)
    manifest = IpcraftPackageParser.new(ipcore_path, package_root).parse
    IpcraftManifestEmitter.new(manifest, package_root: package_root).write
  end

  def self.build_repository_ipcraft_manifests(root: Dir.pwd)
    root = File.expand_path(root)
    repository_ipcraft_source_package_dirs(root).map do |package|
      package_root = File.join(root, 'ipcores', package)
      build_ipcraft_manifest(
        ipcore_path: File.join(package_root, 'ipcore.yml'),
        package_root: package_root
      )
    end
  end

  def self.check_repository_ipcraft_manifests(root: Dir.pwd)
    root = File.expand_path(root)
    mismatches = []

    Dir.mktmpdir('finepaper-ipcraft-manifest-check') do |dir|
      repository_ipcraft_source_package_dirs(root).each do |package|
        copy_ipcraft_package_source(root, dir, package)
        package_root = File.join(dir, 'ipcores', package)
        build_ipcraft_manifest(
          ipcore_path: File.join(package_root, 'ipcore.yml'),
          package_root: package_root
        )
      end

      repository_ipcraft_package_dirs(root).each do |package|
        source_relpath = File.join('ipcores', package, 'ipcore.yml')
        manifest_relpath = File.join('ipcores', package, 'ipcraft.json')
        if !File.file?(File.join(root, source_relpath))
          mismatches << "missing source: #{source_relpath}"
        elsif !File.file?(File.join(root, manifest_relpath))
          mismatches << "missing committed: #{manifest_relpath}"
        elsif !File.file?(File.join(dir, manifest_relpath))
          mismatches << "missing generated: #{manifest_relpath}"
        elsif File.binread(File.join(root, manifest_relpath)) != File.binread(File.join(dir, manifest_relpath))
          mismatches << "content mismatch: #{manifest_relpath}"
        end
      end
    end

    return true if mismatches.empty?

    raise SpecError, "Ipcraft manifests are out of date:\n#{mismatches.map { |path| "  #{path}" }.join("\n")}"
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

  def self.copy_ipcraft_package_source(source_root, target_root, package)
    source_package = File.join(source_root, 'ipcores', package)
    target_package = File.join(target_root, 'ipcores', package)
    FileUtils.mkdir_p(target_package)
    FileUtils.cp(File.join(source_package, 'ipcore.yml'), File.join(target_package, 'ipcore.yml'))
    FileUtils.cp_r(File.join(source_package, 'views'), File.join(target_package, 'views')) if File.directory?(File.join(source_package, 'views'))
  end

  def self.repository_ipcraft_source_package_dirs(root)
    Dir.glob(File.join(root, 'ipcores', '*', 'ipcore.yml'))
       .map { |path| File.basename(File.dirname(path)) }
       .sort
  end

  def self.repository_ipcraft_package_dirs(root)
    (Dir.glob(File.join(root, 'ipcores', '*', 'ipcore.yml')) +
     Dir.glob(File.join(root, 'ipcores', '*', 'ipcraft.json')))
      .map { |path| File.basename(File.dirname(path)) }
      .uniq
      .sort
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

  class IpcraftPackageParser
    def initialize(ipcore_path, package_root)
      @ipcore_path = ipcore_path
      @package_root = File.expand_path(package_root)
    end

    def parse
      data = load_yaml
      case data['schema']
      when 'ipcraft.package.v1'
        IpcraftPackageNormalizer.new(data, package_root: @package_root).manifest
      when 'finepaper.ipcore.v1'
        parsed = IpCoreParser.new(@ipcore_path, File.join(@package_root, 'views')).parse
        LegacyIpcraftManifestAdapter.new(parsed, package_root: @package_root).manifest
      else
        raise SpecError, 'schema must be ipcraft.package.v1'
      end
    end

    private

    def load_yaml
      ConstrainedYamlLoader.load_file(@ipcore_path).tap do |data|
        raise SpecError, 'ipcraft package source root must be a map' unless data.is_a?(Hash)
      end
    end
  end

  class IpcraftPackageNormalizer
    def initialize(data, package_root:)
      @data = data
      @package_root = package_root
      @extensions = {}
      @connection_classes = []
      @connection_class_index = {}
      @modules = []
      @module_interfaces = {}
    end

    def manifest
      validate_top_level
      @extensions = normalize_extensions
      @connection_classes = normalize_connection_classes
      @connection_class_index = @connection_classes.to_h { |klass| [klass.fetch('id'), klass] }
      @modules = normalize_modules
      validate_accept_rules!

      compact_hash(
        'schema' => 'ipcraft.manifest.v1',
        'id' => required_string(@data, 'id', 'id'),
        'name' => required_string(@data, 'name', 'name'),
        'version' => required_string(@data, 'version', 'version'),
        'plugin' => normalize_plugin,
        'extensions' => @extensions,
        'ipxact' => normalize_ipxact,
        'parameters' => normalize_parameters,
        'connection_classes' => @connection_classes,
        'modules' => @modules,
        'views' => normalize_views,
        'topologies' => normalize_topologies,
        'commands' => normalize_commands
      )
    end

    private

    def validate_top_level
      validate_keys!(@data, IPCRAFT_PACKAGE_TOP_LEVEL_KEYS, 'ipcraft package top-level')
      raise SpecError, 'schema must be ipcraft.package.v1' unless @data['schema'] == 'ipcraft.package.v1'
      %w[id name version].each { |key| required_string(@data, key, key) }
      raise SpecError, 'connection_classes must be a list' unless @data['connection_classes'].is_a?(Array)
      raise SpecError, 'modules must be a list' unless @data['modules'].is_a?(Array)
      raise SpecError, 'commands must be a map' unless @data['commands'].is_a?(Hash)
    end

    def normalize_plugin
      plugin = @data['plugin']
      return nil unless plugin
      raise SpecError, 'plugin must be a map' unless plugin.is_a?(Hash)

      validate_keys!(plugin, IPCRAFT_PLUGIN_KEYS, 'plugin')
      normalized = {}
      if plugin.key?('library')
        library = required_string(plugin, 'library', 'plugin.library')
        validate_package_local_path!(library, 'plugin.library')
        normalized['library'] = library
      end
      normalized['entry'] = required_string(plugin, 'entry', 'plugin.entry') if plugin.key?('entry')
      normalized
    end

    def normalize_extensions
      raw = @data.fetch('extensions', {})
      raise SpecError, 'extensions must be a map' unless raw.is_a?(Hash)

      raw.to_h do |id, extension|
        raise SpecError, "extension #{id} must be a map" unless extension.is_a?(Hash)

        normalized = deep_copy(extension)
        enabled = normalized.fetch('enabled', false)
        unless enabled == true || enabled == false
          raise SpecError, "extension #{id}.enabled must be a bool"
        end
        normalized['enabled'] = enabled
        if id == NOC_EXTENSION_ID && enabled
          validate_extension_modes!(id, normalized) if normalized.key?('modes')
          normalized['modes'] = deep_copy(NOC_CHI_MODE_MAPPINGS).merge(deep_copy(normalized.fetch('modes', {})))
        end
        validate_extension_modes!(id, normalized)
        [id, normalized]
      end
    end

    def validate_extension_modes!(extension_id, extension)
      return unless extension.key?('modes')
      raise SpecError, "extension #{extension_id}.modes must be a map" unless extension['modes'].is_a?(Hash)

      extension['modes'] = extension.fetch('modes').to_h do |mode, mapping|
        raise SpecError, "extension #{extension_id}.modes keys must be strings" unless mode.is_a?(String)

        [mode, normalize_extension_mode_mapping(extension_id, mode, mapping)]
      end
    end

    def normalize_extension_mode_mapping(extension_id, mode, mapping)
      context = "extension #{extension_id} mode #{mode}"
      unless mapping.is_a?(Hash) && mapping['ipxact'].is_a?(Hash) && mapping['ipxact']['mode'].is_a?(String)
        raise SpecError, "#{context} has no IP-XACT mapping"
      end

      validate_keys!(mapping, IPCRAFT_EXTENSION_MODE_KEYS, context)
      ipxact = mapping.fetch('ipxact')
      validate_keys!(ipxact, IPCRAFT_EXTENSION_MODE_IPXACT_KEYS, "#{context} ipxact")
      native_mode = required_string(ipxact, 'mode', "#{context} ipxact.mode")
      validate_ipxact_mode!(native_mode, "#{context} ipxact.mode")
      { 'ipxact' => { 'mode' => native_mode } }
    end

    def normalize_ipxact
      ipxact = @data['ipxact']
      return nil unless ipxact
      raise SpecError, 'ipxact must be a map' unless ipxact.is_a?(Hash)

      validate_keys!(ipxact, IPCRAFT_IPXACT_KEYS, 'ipxact')
      normalized = {}
      if ipxact.key?('root')
        root = required_string(ipxact, 'root', 'ipxact.root')
        validate_package_local_path!(root, 'ipxact.root')
        normalized['root'] = root
      end
      if ipxact.key?('generated')
        unless ipxact['generated'] == true || ipxact['generated'] == false
          raise SpecError, 'ipxact.generated must be a bool'
        end
        normalized['generated'] = ipxact['generated']
      end
      normalized
    end

    def normalize_parameters
      parameters = @data.fetch('parameters', {})
      raise SpecError, 'parameters must be a map' unless parameters.is_a?(Hash)

      deep_copy(parameters)
    end

    def normalize_connection_classes
      seen_ids = {}
      @data.fetch('connection_classes').map do |klass|
        raise SpecError, 'connection class must be a map' unless klass.is_a?(Hash)

        validate_keys!(klass, IPCRAFT_CONNECTION_CLASS_KEYS, "connection class #{klass['id'] || '<unnamed>'}")
        klass_id = required_string(klass, 'id', 'connection class id')
        roles = required_string_list(klass, 'roles', "connection class #{klass_id} roles")
        remember_unique!(seen_ids, klass_id, 'connection class id')
        normalized = {
          'id' => klass_id,
          'roles' => roles,
          'symmetric' => required_bool(klass, 'symmetric', "connection class #{klass_id}.symmetric")
        }
        if klass.key?('ipxact')
          normalized['ipxact'] = normalize_connection_class_ipxact(klass_id, roles, klass['ipxact'])
        end
        validate_connection_class_mapping!(normalized)
        normalized
      end
    end

    def normalize_connection_class_ipxact(klass_id, roles, ipxact)
      raise SpecError, "connection class #{klass_id} ipxact must be a map" unless ipxact.is_a?(Hash)
      raise SpecError, "connection class #{klass_id} ipxact cannot be empty" if ipxact.empty?

      expected_roles = roles.uniq
      ipxact.each do |role, mode|
        unless expected_roles.include?(role)
          raise SpecError, "connection class #{klass_id} ipxact field #{role} is not recognized"
        end
        unless mode.is_a?(String)
          raise SpecError, "connection class #{klass_id} ipxact.#{role} must be a string"
        end

        validate_ipxact_mode!(mode, "connection class #{klass_id} ipxact.#{role}")
      end

      expected_roles.each do |role|
        raise SpecError, "connection class #{klass_id} ipxact.#{role} is required" unless ipxact.key?(role)
      end

      deep_copy(ipxact)
    end

    def normalize_modules
      seen_ids = {}
      @data.fetch('modules').map do |mod|
        raise SpecError, 'module must be a map' unless mod.is_a?(Hash)

        module_id = required_string(mod, 'id', 'module id')
        remember_unique!(seen_ids, module_id, 'module id')
        validate_keys!(mod, IPCRAFT_MODULE_KEYS, "module #{module_id}")
        interfaces = normalize_interfaces(module_id, mod.fetch('interfaces', nil))
        @module_interfaces[module_id] = interfaces.map { |interface| interface.fetch('id') }
        compact_hash(
          'id' => module_id,
          'name' => optional_string(mod, 'name', "module #{module_id}.name"),
          'description' => optional_string(mod, 'description', "module #{module_id}.description"),
          'graph_role' => optional_string(mod, 'graph_role', "module #{module_id}.graph_role"),
          'attach' => mod.key?('attach') ? deep_copy(mod['attach']) : nil,
          'parameters' => mod.key?('parameters') ? normalize_module_parameters(module_id, mod['parameters']) : nil,
          'interfaces' => interfaces
        )
      end
    end

    def normalize_module_parameters(module_id, parameters)
      raise SpecError, "module #{module_id}.parameters must be a map" unless parameters.is_a?(Hash)

      deep_copy(parameters)
    end

    def normalize_interfaces(module_id, interfaces)
      raise SpecError, "module #{module_id}.interfaces must be a list" unless interfaces.is_a?(Array)

      seen_ids = {}
      interfaces.map do |interface|
        raise SpecError, "module #{module_id} interface must be a map" unless interface.is_a?(Hash)

        interface_id = required_string(interface, 'id', "module #{module_id} interface id")
        remember_unique!(seen_ids, interface_id, "module #{module_id} interface id")
        validate_keys!(interface, IPCRAFT_INTERFACE_KEYS, "#{module_id}.#{interface_id}")
        modes = required_string_list(interface, 'modes', "#{module_id}.#{interface_id} modes")
        ipxact = interface.key?('ipxact') ? normalize_interface_ipxact(module_id, interface_id, interface['ipxact']) : nil
        modes.each { |mode| validate_interface_mode_mapping!(module_id, interface_id, ipxact, mode) }

        compact_hash(
          'id' => interface_id,
          'name' => optional_string(interface, 'name', "#{module_id}.#{interface_id}.name"),
          'label' => optional_string(interface, 'label', "#{module_id}.#{interface_id}.label"),
          'modes' => modes,
          'accepts' => normalize_accepts(module_id, interface_id, interface.fetch('accepts', nil)),
          'multi_connection' => interface.key?('multi_connection') ? required_bool(interface, 'multi_connection', "#{module_id}.#{interface_id}.multi_connection") : nil,
          'ipxact' => ipxact,
          'parameters' => interface.key?('parameters') ? deep_copy(interface['parameters']) : nil
        )
      end
    end

    def normalize_accepts(module_id, interface_id, accepts)
      raise SpecError, "#{module_id}.#{interface_id}.accepts must be a list" unless accepts.is_a?(Array)

      accepts.map do |accept|
        raise SpecError, "#{module_id}.#{interface_id}.accepts entry must be a map" unless accept.is_a?(Hash)

        validate_keys!(accept, IPCRAFT_ACCEPT_KEYS, "#{module_id}.#{interface_id}.accepts")
        {
          'class' => required_string(accept, 'class', "#{module_id}.#{interface_id}.accepts.class"),
          'role' => required_string(accept, 'role', "#{module_id}.#{interface_id}.accepts.role")
        }
      end
    end

    def normalize_interface_ipxact(module_id, interface_id, ipxact)
      raise SpecError, "#{module_id}.#{interface_id}.ipxact must be a map" unless ipxact.is_a?(Hash)

      validate_keys!(ipxact, IPCRAFT_INTERFACE_IPXACT_KEYS, "#{module_id}.#{interface_id}.ipxact")
      normalized = {}
      if ipxact.key?('bus_interface')
        normalized['bus_interface'] = required_string(ipxact, 'bus_interface', "#{module_id}.#{interface_id} ipxact.bus_interface")
      end
      if ipxact.key?('mode')
        mode = required_string(ipxact, 'mode', "#{module_id}.#{interface_id} ipxact.mode")
        validate_ipxact_mode!(mode, "#{module_id}.#{interface_id} ipxact.mode")
        normalized['mode'] = mode
      end
      if ipxact.key?('modes')
        raise SpecError, "#{module_id}.#{interface_id} ipxact.modes must be a map" unless ipxact['modes'].is_a?(Hash)

        normalized['modes'] = ipxact['modes'].to_h do |mode, mapping|
          raise SpecError, "#{module_id}.#{interface_id} ipxact.modes keys must be strings" unless mode.is_a?(String)

          [mode, normalize_interface_ipxact_mode_mapping(module_id, interface_id, mode, mapping)]
        end
      end
      normalized
    end

    def normalize_interface_ipxact_mode_mapping(module_id, interface_id, mode, mapping)
      context = "#{module_id}.#{interface_id} ipxact.modes.#{mode}"
      unless mapping.is_a?(Hash) && mapping['mode'].is_a?(String)
        raise SpecError, "#{context} has no IP-XACT mapping"
      end

      validate_keys!(mapping, IPCRAFT_INTERFACE_IPXACT_MODE_KEYS, context)
      native_mode = required_string(mapping, 'mode', "#{context}.mode")
      validate_ipxact_mode!(native_mode, "#{context}.mode")
      { 'mode' => native_mode }
    end

    def normalize_views
      views = @data.fetch('views', [])
      raise SpecError, 'views must be a list' unless views.is_a?(Array)

      seen_modules = {}
      views.map do |view|
        raise SpecError, 'view must be a map' unless view.is_a?(Hash)

        validate_keys!(view, IPCRAFT_VIEW_KEYS, "view #{view['module'] || '<unnamed>'}")
        module_id = required_string(view, 'module', 'view.module')
        remember_unique!(seen_modules, module_id, 'view module')
        file = required_string(view, 'file', 'view.file')
        validate_package_local_path!(file, "view #{module_id} file")
        normalized = {
          'module' => module_id,
          'file' => file
        }
        validate_view!(normalized)
        normalized
      end
    end

    def normalize_topologies
      topologies = @data.fetch('topologies', [])
      raise SpecError, 'topologies must be a list' unless topologies.is_a?(Array)

      seen_ids = {}
      topologies.each do |topology|
        raise SpecError, 'topology must be a map' unless topology.is_a?(Hash)

        topology_id = required_string(topology, 'id', 'topology id')
        remember_unique!(seen_ids, topology_id, 'topology id')
      end
      deep_copy(topologies)
    end

    def normalize_commands
      @data.fetch('commands').to_h do |name, command|
        raise SpecError, "commands.#{name} must be a map" unless command.is_a?(Hash)

        validate_keys!(command, IPCRAFT_COMMAND_KEYS, "commands.#{name}")
        executable = required_string(command, 'executable', "commands.#{name}.executable")
        validate_package_local_path!(executable, "commands.#{name}.executable")
        normalized = {
          'executable' => executable,
          'input_schema' => required_string(command, 'input_schema', "commands.#{name}.input_schema")
        }
        if command.key?('args')
          raise SpecError, "commands.#{name}.args must be a list" unless command['args'].is_a?(Array)
          command['args'].each do |arg|
            raise SpecError, "commands.#{name}.args entries must be strings" unless arg.is_a?(String)
          end
          normalized['args'] = command['args']
        end
        [name, normalized]
      end
    end

    def validate_accept_rules!
      @modules.each do |mod|
        mod.fetch('interfaces').each do |interface|
          interface.fetch('accepts').each do |accept|
            klass = @connection_class_index[accept.fetch('class')]
            unless klass
              raise SpecError, "#{mod.fetch('id')}.#{interface.fetch('id')} accepts unknown connection class #{accept.fetch('class')}"
            end
            next if klass.fetch('roles').include?(accept.fetch('role'))

            raise SpecError, "#{mod.fetch('id')}.#{interface.fetch('id')} role #{accept.fetch('role')} is not in connection class #{klass.fetch('id')}"
          end
        end
      end
    end

    def validate_interface_mode_mapping!(module_id, interface_id, ipxact, mode)
      return if IPXACT_NATIVE_MODES.include?(mode)
      return if extension_mode_mapping(mode)
      return if explicit_interface_mode_mapping?(ipxact, mode)

      raise SpecError, "#{module_id}.#{interface_id} mode #{mode} has no IP-XACT mapping"
    end

    def explicit_interface_mode_mapping?(ipxact, mode)
      return false unless ipxact.is_a?(Hash)

      ipxact['mode'].is_a?(String) ||
        (ipxact['modes'].is_a?(Hash) && ipxact['modes'].key?(mode))
    end

    def extension_mode_mapping(mode)
      @extensions.each_value do |extension|
        next unless extension['enabled'] == true && extension['modes'].is_a?(Hash)

        mapping = extension['modes'][mode]
        return mapping if mapping
      end
      nil
    end

    def validate_connection_class_mapping!(klass)
      return if klass['ipxact'].is_a?(Hash)
      return if klass.fetch('roles').all? { |role| IPXACT_NATIVE_MODES.include?(role) }
      return if noc_chi_connection_class?(klass)

      raise SpecError, "connection class #{klass.fetch('id')} has no IP-XACT mapping"
    end

    def noc_chi_connection_class?(klass)
      extension = @extensions[NOC_EXTENSION_ID]
      return false unless extension && extension['enabled'] == true
      return false unless klass.fetch('id').start_with?('chi_')

      klass.fetch('roles').all? { |role| NOC_CHI_CONNECTION_ROLES.include?(role) }
    end

    def validate_view!(view)
      module_id = view.fetch('module')
      interfaces = @module_interfaces[module_id]
      raise SpecError, "view references unknown module #{module_id}" unless interfaces

      path = File.expand_path(view.fetch('file'), @package_root)
      raise SpecError, "Missing view XML for #{module_id}: #{path}" unless File.file?(path)

      text = File.read(path)
      declared_module = text[/<module-view\b[^>]*\bmodule="([^"]+)"/, 1]
      raise SpecError, "view #{module_id} declares module #{declared_module}" if declared_module && declared_module != module_id

      text.scan(/<(?:interface|anchor)\b[^>]*\bref="([^"]+)"/).flatten.each do |ref|
        raise SpecError, "view #{module_id} references unknown interface #{ref}" unless interfaces.include?(ref)
      end
    end

    def validate_keys!(hash, allowed, context)
      hash.each_key do |key|
        next if allowed.include?(key)

        raise SpecError, "Unknown #{context} field: #{key}"
      end
    end

    def remember_unique!(seen, id, context)
      raise SpecError, "Duplicate #{context}: #{id}" if seen.key?(id)

      seen[id] = true
    end

    def validate_package_local_path!(path, context)
      raise SpecError, "#{context} cannot be empty" if path.empty?
      raise SpecError, "#{context} must be package-local" if absolute_path?(path)

      root = File.expand_path(@package_root)
      expanded = File.expand_path(path, root)
      return if expanded == root || expanded.start_with?("#{root}#{File::SEPARATOR}")

      raise SpecError, "#{context} escapes package root"
    end

    def absolute_path?(path)
      Pathname.new(path).absolute? || path.match?(/\A(?:[A-Za-z]:[\\\/]|\\\\|\/\/)/)
    end

    def validate_ipxact_mode!(mode, context)
      return if IPXACT_NATIVE_MODES.include?(mode)

      raise SpecError, "#{context} is invalid"
    end

    def required_string(hash, key, context)
      raise SpecError, "#{context} is required" unless hash.key?(key)
      raise SpecError, "#{context} must be a string" unless hash[key].is_a?(String)

      hash[key]
    end

    def optional_string(hash, key, context)
      return nil unless hash.key?(key)

      required_string(hash, key, context)
    end

    def required_bool(hash, key, context)
      raise SpecError, "#{context} is required" unless hash.key?(key)
      return hash[key] if hash[key] == true || hash[key] == false

      raise SpecError, "#{context} must be a bool"
    end

    def required_string_list(hash, key, context)
      raise SpecError, "#{context} is required" unless hash.key?(key)
      raise SpecError, "#{context} must be a list" unless hash[key].is_a?(Array)
      raise SpecError, "#{context} cannot be empty" if hash[key].empty?

      hash[key].each do |value|
        raise SpecError, "#{context} entries must be strings" unless value.is_a?(String)
      end
      hash[key]
    end

    def compact_hash(hash)
      hash.reject { |_, value| value.nil? }
    end

    def deep_copy(value)
      case value
      when Hash
        value.to_h { |key, child| [key, deep_copy(child)] }
      when Array
        value.map { |child| deep_copy(child) }
      else
        value
      end
    end
  end

  class LegacyIpcraftManifestAdapter
    def initialize(parsed, package_root:)
      @spec = parsed.data
      @views = parsed.views
      @package_root = package_root
    end

    def manifest
      {
        'schema' => 'ipcraft.manifest.v1',
        'id' => @spec.fetch('id'),
        'name' => @spec.fetch('name'),
        'version' => @spec.fetch('version'),
        'extensions' => {},
        'parameters' => @spec.fetch('instance_parameters', {}),
        'connection_classes' => connection_classes,
        'modules' => modules,
        'views' => views,
        'topologies' => @spec.fetch('topology_presets', []),
        'commands' => commands
      }
    end

    private

    def connection_classes
      @spec.fetch('buses', {}).map do |bus_name, bus|
        roles = bus.fetch('compatibility').fetch('roles').keys
        {
          'id' => bus_name,
          'roles' => roles,
          'symmetric' => roles.size == 1
        }
      end
    end

    def modules
      @spec.fetch('modules').map do |module_id, mod|
        {
          'id' => module_id,
          'name' => mod.fetch('palette_label'),
          'description' => mod.fetch('description'),
          'graph_role' => graph_role(mod.fetch('graph_group')),
          'parameters' => mod.fetch('parameters'),
          'interfaces' => interfaces(mod.fetch('interfaces'))
        }
      end
    end

    def interfaces(interfaces)
      interfaces.map do |interface_id, interface|
        {
          'id' => interface_id,
          'label' => interface['label'],
          'modes' => [interface.fetch('role')],
          'accepts' => [{ 'class' => interface.fetch('bus'), 'role' => interface.fetch('role') }],
          'multi_connection' => interface['cardinality'] == 'many',
          'ipxact' => { 'bus_interface' => interface_id }
        }
      end
    end

    def views
      @views.keys.map do |module_id|
        { 'module' => module_id, 'file' => "views/#{module_id}.xml" }
      end
    end

    def commands
      runtime = @spec.fetch('runtime')
      {
        'validate' => legacy_command(runtime.fetch('drc')),
        'generate' => legacy_command(runtime.fetch('generator'))
      }
    end

    def legacy_command(command)
      executable = command.fetch('args').first || command.fetch('command')
      {
        'executable' => executable,
        'input_schema' => 'ipcraft.noc.project.v1'
      }
    end

    def graph_role(group)
      case group
      when 'xps' then 'host'
      when 'endpoints' then 'attached'
      else group
      end
    end
  end

  class IpcraftManifestEmitter
    def initialize(manifest, package_root:)
      @manifest = manifest
      @package_root = package_root
    end

    def write
      path = File.join(@package_root, 'ipcraft.json')
      FileUtils.mkdir_p(@package_root)
      File.write(path, "#{JSON.pretty_generate(@manifest)}\n")
      path
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
