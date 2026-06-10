require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'tmpdir'

module IpcraftGenerator
  class Error < StandardError; end

  class CLI
    def self.run(argv)
      options = {}

      OptionParser.new do |parser|
        parser.on('--manifest PATH', 'Ipcraft manifest JSON path') { |value| options[:manifest] = value }
        parser.on('--input PATH', 'Ipcraft project input JSON path') { |value| options[:input] = value }
        parser.on('--output DIR', 'Generated output directory') { |value| options[:output] = value }
      end.parse!(argv)
    rescue OptionParser::ParseError => error
      raise Error, error.message
    else
      raise Error, "unexpected argument: #{argv.first}" unless argv.empty?

      raise Error, '--manifest is required' unless options[:manifest]
      raise Error, '--input is required' unless options[:input]
      raise Error, '--output is required' unless options[:output]

      Generator.new(
        manifest: options.fetch(:manifest),
        input: options.fetch(:input),
        output: options.fetch(:output)
      ).generate

      puts "Generated ipcraft output in #{options.fetch(:output)}"
    end
  end

  class Generator
    PACKAGE_HANDLERS = {
      'finepaper.noc' => :generate_finepaper_noc,
      'finepaper.ravenoc' => :generate_ravenoc,
      'finepaper.opennoc' => :generate_opennoc
    }.freeze
    OPENNOC_AGENT_TYPES = %w[RNF RNI HNF HNI SNF].freeze
    RAVENOC_VENDOR_FILES = [
      'bus_arch_sv_pkg/amba_axi_pkg.sv',
      'src/include/ravenoc_axi_fnc.svh',
      'src/include/ravenoc_defines.svh',
      'src/include/ravenoc_structs.svh',
      'src/include/ravenoc_pkg.sv',
      'src/ni/axi_csr.sv',
      'src/ni/axi_slave_if.sv',
      'src/ni/router_wrapper.sv',
      'src/ni/async_gp_fifo.sv',
      'src/ni/cdc_pkt.sv',
      'src/ni/pkt_proc.sv',
      'src/router/fifo.sv',
      'src/router/output_module.sv',
      'src/router/router_if.sv',
      'src/router/router_ravenoc.sv',
      'src/router/rr_arbiter.sv',
      'src/router/vc_buffer.sv',
      'src/router/input_router.sv',
      'src/router/input_module.sv',
      'src/router/input_datapath.sv',
      'src/ravenoc.sv'
    ].freeze
    RAVENOC_VENDOR_SOURCE_FILES = [
      'bus_arch_sv_pkg/amba_axi_pkg.sv',
      'src/include/ravenoc_pkg.sv',
      'src/ni/axi_csr.sv',
      'src/ni/axi_slave_if.sv',
      'src/ni/router_wrapper.sv',
      'src/ni/async_gp_fifo.sv',
      'src/ni/cdc_pkt.sv',
      'src/ni/pkt_proc.sv',
      'src/router/fifo.sv',
      'src/router/output_module.sv',
      'src/router/router_if.sv',
      'src/router/router_ravenoc.sv',
      'src/router/rr_arbiter.sv',
      'src/router/vc_buffer.sv',
      'src/router/input_router.sv',
      'src/router/input_module.sv',
      'src/router/input_datapath.sv',
      'src/ravenoc.sv'
    ].freeze
    OPENNOC_VENDOR_FILES = [
      'LICENSE',
      'tools/mesh_generator/mesh_gen.py',
      'tools/mesh_generator/template/mesh_wrapper.j2',
      'tools/mesh_generator/chi_xp_node.sv',
      'rtl/misc/chi_xp_channel.v',
      'rtl/misc/sync_fifo.v',
      'rtl/include/chie_defines.v',
      'rtl/include/rni_param.v',
      'rtl/include/hnf_param.v',
      'rtl/include/hni_param.v',
      'rtl/include/snf_param.v',
      'rtl/src/rni/rni.v',
      'rtl/src/hnf/hnf.v',
      'rtl/src/hni/hni.v',
      'rtl/src/snf/snf.v'
    ].freeze
    RAVENOC_DEFAULTS = {
      'rows' => 2,
      'cols' => 2,
      'flit_data_width' => 32,
      'flit_type_width' => 2,
      'flit_buffer_depth' => 2,
      'virtual_channels' => 3,
      'routing_algorithm' => 'xy',
      'priority' => 'zero_high',
      'max_packet_flits' => 256,
      'axi_addr_width' => 32,
      'axi_data_width' => 32,
      'axi_cdc_required' => 'all',
      'bypass_cdc' => false
    }.freeze
    RAVENOC_ROUTING_MAP = {
      'xy' => 'XYAlg',
      'yx' => 'YXAlg'
    }.freeze
    RAVENOC_PRIORITY_MAP = {
      'zero_high' => 'ZeroHighPrior',
      'zero_low' => 'ZeroLowPrior'
    }.freeze
    RAVENOC_DEFINE_NAMES = {
      'rows' => 'NOC_CFG_SZ_ROWS',
      'cols' => 'NOC_CFG_SZ_COLS',
      'flit_data_width' => 'FLIT_DATA_WIDTH',
      'flit_type_width' => 'FLIT_TP_WIDTH',
      'flit_buffer_depth' => 'FLIT_BUFF',
      'virtual_channels' => 'N_VIRT_CHN',
      'routing_algorithm' => 'ROUTING_ALG',
      'priority' => 'H_PRIORITY',
      'max_packet_flits' => 'MAX_SZ_PKT',
      'axi_addr_width' => 'AXI_ADDR_WIDTH',
      'axi_data_width' => 'AXI_DATA_WIDTH'
    }.freeze
    COMPATIBLE_INPUT_MODULES = {
      'finepaper.ravenoc' => ['RaveNoC']
    }.freeze

    def initialize(manifest:, input:, output:)
      @manifest_path = manifest
      @input_path = input
      @output_dir = output
    end

    def generate
      requested_output_dir = @output_dir
      stage_output_dir = nil
      remove_success_manifest(requested_output_dir)

      manifest = JSON.parse(File.read(@manifest_path))
      manifest = normalize_package_manifest(manifest)
      input = normalize_emitted_inputs(manifest, JSON.parse(File.read(@input_path)))

      validate!(manifest, input)

      stage_output_dir = create_stage_output_dir(requested_output_dir)
      @output_dir = stage_output_dir
      send(PACKAGE_HANDLERS.fetch(manifest.fetch('id'), :generate_generic), manifest, input)
      @output_dir = requested_output_dir
      replace_output_dir(stage_output_dir, requested_output_dir)
      stage_output_dir = nil
    rescue
      @output_dir = requested_output_dir if requested_output_dir
      remove_success_manifest(requested_output_dir) if requested_output_dir
      raise
    ensure
      @output_dir = requested_output_dir if requested_output_dir
      FileUtils.rm_rf(stage_output_dir) if stage_output_dir && Dir.exist?(stage_output_dir)
    end

    private

    def remove_success_manifest(output_dir)
      return unless output_dir

      FileUtils.rm_f(File.join(output_dir, 'manifest.json'))
    end

    def create_stage_output_dir(output_dir)
      expanded_output = File.expand_path(output_dir)
      parent = File.dirname(expanded_output)
      FileUtils.mkdir_p(parent)
      Dir.mktmpdir(".#{File.basename(expanded_output)}.", parent)
    end

    def replace_output_dir(stage_output_dir, output_dir)
      expanded_output = File.expand_path(output_dir)
      FileUtils.rm_rf(expanded_output)
      FileUtils.mv(stage_output_dir, expanded_output)
    end

    def validate!(manifest, input)
      raise Error, 'manifest schema must be ipcraft.package.v1' unless manifest['schema'] == 'ipcraft.package.v1'
      raise Error, 'input schema must be ipcraft.emitted-inputs.v1' unless input['schema'] == 'ipcraft.emitted-inputs.v1'
      raise Error, 'input package does not match manifest id' unless input['package'] == manifest['id']
      if input.key?('package_id') && input['package_id'] != manifest['id']
        raise Error, 'input package_id does not match manifest id'
      end

      validate_command_input_graph!(manifest, input)
    end

    def normalize_emitted_inputs(manifest, emitted)
      unless emitted['schema'] == 'ipcraft.emitted-inputs.v1'
        raise Error, 'input schema must be ipcraft.emitted-inputs.v1'
      end

      package = emitted['package']
      unless package.is_a?(Hash) && package['id'].is_a?(String)
        raise Error, 'emitted inputs package.id is required'
      end
      unless package['id'] == manifest.fetch('id')
        raise Error, 'input package does not match manifest id'
      end

      graph_config = emitted_file_json(emitted, 'graph_config') || empty_graph_config
      parameters = emitted_file_json(emitted, 'parameters') || {}
      unless parameters.is_a?(Hash)
        raise Error, 'emitted parameters must be an object'
      end

      {
        'schema' => emitted.fetch('schema'),
        'project' => emitted_project_state(emitted, package, parameters),
        'package' => package.fetch('id'),
        'package_id' => package.fetch('id'),
        'package_version' => package['version'],
        'parameters' => parameters,
        'instances' => graph_config_instances(manifest, graph_config),
        'connections' => graph_config_connections(graph_config)
      }
    end

    def empty_graph_config
      {
        'schema' => 'ipcraft.graph-config.v1',
        'objects' => [],
        'relationships' => []
      }
    end

    def emitted_project_state(emitted, package, parameters)
      instance_id = emitted['instance']
      project_id = emitted['project']
      {
        'id' => project_id,
        'name' => project_id,
        'global_parameters' => parameters,
        'instance' => {
          'id' => instance_id,
          'package' => package.fetch('id'),
          'state' => {
            'global_parameters' => parameters
          }
        }
      }
    end

    def emitted_file_json(emitted, kind)
      entry = emitted_files(emitted).find { |file| file['kind'] == kind }
      return nil unless entry

      path = entry['path']
      raise Error, "emitted #{kind} file path must be a string" unless path.is_a?(String)

      JSON.parse(File.read(confined_emitted_file_path(path)))
    end

    def emitted_files(emitted)
      files = emitted['files']
      raise Error, 'emitted inputs files must be an array' unless files.is_a?(Array)

      files.each do |file|
        raise Error, 'emitted inputs file entries must be objects' unless file.is_a?(Hash)
      end
      files
    end

    def confined_emitted_file_path(relative_path)
      raw = relative_path.to_s.tr('\\', '/')
      expanded_root = File.expand_path(File.dirname(@input_path))
      expanded_path = File.expand_path(raw, expanded_root)
      unless expanded_path == expanded_root || expanded_path.start_with?("#{expanded_root}#{File::SEPARATOR}")
        raise Error, "emitted input file escapes input root: #{relative_path}"
      end
      expanded_path
    end

    def graph_config_instances(manifest, graph_config)
      objects = graph_config.fetch('objects', [])
      raise Error, 'graph_config objects must be an array' unless objects.is_a?(Array)
      module_interfaces = manifest_module_interfaces(manifest)

      objects.map do |object|
        raise Error, 'graph_config object must be an object' unless object.is_a?(Hash)

        object_id = required_input_string(object, 'id', 'graph object id')
        module_type = unscoped_module_type(required_input_string(object, 'type', "graph object #{object_id} type"))
        properties = object.fetch('properties', {})
        raise Error, "graph_config object #{object_id} properties must be an object" unless properties.is_a?(Hash)

        {
          'id' => object_id,
          'module' => module_type,
          'type' => module_type,
          'parameters' => properties,
          'interfaces' => module_interfaces.fetch(module_type, []).map do |interface_id|
            { 'id' => interface_id, 'port' => interface_id }
          end
        }
      end
    end

    def graph_config_connections(graph_config)
      relationships = graph_config.fetch('relationships', [])
      raise Error, 'graph_config relationships must be an array' unless relationships.is_a?(Array)

      relationships.map do |relationship|
        raise Error, 'graph_config relationship must be an object' unless relationship.is_a?(Hash)

        relationship_id = required_input_string(relationship, 'id', 'graph relationship id')
        endpoints = relationship.fetch('endpoints', [])
        unless endpoints.is_a?(Array)
          raise Error, "graph_config relationship #{relationship_id} endpoints must be an array"
        end

        {
          'id' => relationship_id,
          'class' => relationship['type'],
          'interfaces' => endpoints.map { |endpoint| graph_config_endpoint(endpoint, relationship_id) }
        }
      end
    end

    def graph_config_endpoint(endpoint, relationship_id)
      unless endpoint.is_a?(Hash)
        raise Error, "graph_config relationship #{relationship_id} endpoint must be an object"
      end

      object = required_input_string(endpoint, 'object', "graph relationship #{relationship_id} endpoint object")
      role = required_input_string(endpoint, 'role', "graph relationship #{relationship_id} endpoint role")
      {
        'instance' => object,
        'interface' => role
      }
    end

    def unscoped_module_type(type)
      type.to_s.split('::').last
    end

    def normalize_package_manifest(manifest)
      return manifest unless manifest['schema'] == 'ipcraft.package.v1'

      editor = manifest.dig('native', 'ipcraft', 'editor')
      raise Error, 'manifest native.ipcraft.editor must be an object' unless editor.is_a?(Hash)

      editor.merge(
        'schema' => manifest.fetch('schema'),
        'id' => manifest.fetch('id'),
        'name' => manifest['name'],
        'version' => manifest['version']
      )
    end

    def validate_command_input_graph!(manifest, input)
      instances = input.fetch('instances', [])
      raise Error, 'generator input instances must be an array' unless instances.is_a?(Array)

      module_interfaces = manifest_module_interfaces(manifest)
      known_module_ids = module_interfaces.keys + COMPATIBLE_INPUT_MODULES.fetch(manifest.fetch('id'), [])
      connection_class_ids = manifest_connection_class_ids(manifest)
      instance_interfaces = {}
      instances.each do |instance|
        raise Error, 'generator input instance must be an object' unless instance.is_a?(Hash)

        instance_id = required_input_string(instance, 'id', 'instance id')
        raise Error, "duplicate generator input instance #{instance_id}" if instance_interfaces.key?(instance_id)

        module_id = instance['module'] || instance['module_id'] || instance['type']
        if module_id && !module_id.is_a?(String)
          raise Error, "generator input instance #{instance_id} module id must be a string"
        end
        if module_id && !known_module_ids.empty? && !known_module_ids.include?(module_id)
          raise Error, "generator input instance #{instance_id} references unknown module #{module_id}"
        end

        instance_interfaces[instance_id] =
          declared_instance_interfaces(instance, module_interfaces.fetch(module_id, []), instance_id)
      end

      connections = input.fetch('connections', [])
      raise Error, 'generator input connections must be an array' unless connections.is_a?(Array)

      connections.each do |connection|
        validate_input_connection!(connection, instance_interfaces, connection_class_ids)
      end
    end

    def manifest_module_interfaces(manifest)
      manifest.fetch('modules', []).each_with_object({}) do |mod, modules|
        next unless mod.is_a?(Hash)

        module_id = mod['id']
        next unless module_id.is_a?(String)

        modules[module_id] = mod.fetch('interfaces', []).filter_map do |interface|
          interface['id'] if interface.is_a?(Hash) && interface['id'].is_a?(String)
        end
      end
    end

    def manifest_connection_class_ids(manifest)
      manifest.fetch('connection_classes', []).filter_map do |connection_class|
        connection_class['id'] if connection_class.is_a?(Hash) && connection_class['id'].is_a?(String)
      end
    end

    def declared_instance_interfaces(instance, module_interface_ids, instance_id)
      raw_interfaces = instance['interfaces']
      if raw_interfaces.nil?
        return module_interface_ids.to_h { |interface_id| [interface_id, interface_id] }
      end

      unless raw_interfaces.is_a?(Array)
        raise Error, "generator input instance #{instance_id} interfaces must be an array"
      end

      raw_interfaces.each_with_object({}) do |interface, interfaces|
        interface_id, port_id = normalize_instance_interface(interface, instance_id)
        unless module_interface_ids.empty? ||
               module_interface_ids.include?(interface_id) ||
               module_interface_ids.include?(port_id)
          raise Error, "generator input instance #{instance_id} references unknown interface #{interface_id}"
        end

        interfaces[interface_id] = port_id
      end
    end

    def normalize_instance_interface(interface, instance_id)
      if interface.is_a?(String)
        return [interface, interface]
      end
      unless interface.is_a?(Hash)
        raise Error, "generator input instance #{instance_id} interface entry must be a string or object"
      end

      interface_id = interface['id'] || interface['interface'] || interface['port']
      port_id = interface['port'] || interface_id
      unless interface_id.is_a?(String) && port_id.is_a?(String)
        raise Error, "generator input instance #{instance_id} interface entry has invalid id or port"
      end

      [interface_id, port_id]
    end

    def validate_input_connection!(connection, instance_interfaces, connection_class_ids)
      raise Error, 'generator input connection must be an object' unless connection.is_a?(Hash)

      connection_id = connection.fetch('id', '<unnamed>')
      validate_connection_class!(connection, connection_class_ids)
      interface_refs = if connection.key?('interfaces')
                         validate_connection_interfaces!(connection, instance_interfaces)
                       else
                         []
                       end

      if connection.key?('source') || connection.key?('target')
        validate_connection_endpoint_pair!(
          connection,
          instance_interfaces,
          %w[source target],
          canonical_refs: interface_refs
        )
      elsif connection.key?('from') || connection.key?('to')
        validate_connection_endpoint_pair!(
          connection,
          instance_interfaces,
          %w[from to],
          canonical_refs: interface_refs
        )
      elsif interface_refs.empty?
        raise Error, "generator input connection #{connection_id} must specify interfaces or endpoints"
      end
    end

    def validate_connection_class!(connection, connection_class_ids)
      return unless connection.key?('class')

      connection_id = connection.fetch('id', '<unnamed>')
      connection_class = connection['class']
      unless connection_class.is_a?(String)
        raise Error, "generator input connection #{connection_id} class must be a string"
      end
      return if connection_class_ids.empty? || connection_class_ids.include?(connection_class)

      raise Error, "generator input connection #{connection_id} references unknown connection class #{connection_class}"
    end

    def validate_connection_interfaces!(connection, instance_interfaces)
      refs = connection.fetch('interfaces')
      unless refs.is_a?(Array) && refs.size == 2
        raise Error, "generator input connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
      end

      refs.map do |ref|
        validate_canonical_endpoint_ref!(connection, ref, instance_interfaces, 'interface')
      end
    end

    def validate_connection_endpoint_pair!(connection, instance_interfaces, keys, canonical_refs:)
      connection_id = connection.fetch('id', '<unnamed>')
      unless keys.all? { |key| connection.key?(key) }
        raise Error, "generator input connection #{connection_id} must include both #{keys.join(' and ')}"
      end

      keys.each do |key|
        endpoint = if canonical_refs.empty?
                     validate_legacy_endpoint_ref!(connection, connection.fetch(key), instance_interfaces, key)
                   else
                     validate_canonical_endpoint_ref!(connection, connection.fetch(key), instance_interfaces, key)
                   end
        if !canonical_refs.empty? && !canonical_refs.include?(endpoint)
          raise Error, "generator input connection #{connection_id} #{key} endpoint does not match interfaces"
        end
      end
    end

    def validate_canonical_endpoint_ref!(connection, ref, instance_interfaces, endpoint_name)
      unless ref.is_a?(Hash) && ref['instance'].is_a?(String) && ref['interface'].is_a?(String)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      validate_known_interface_ref!(connection, instance_interfaces, ref.fetch('instance'), ref.fetch('interface'))
    end

    def validate_legacy_endpoint_ref!(connection, ref, instance_interfaces, endpoint_name)
      unless ref.is_a?(Hash)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      instance_id = ref['instance'] || ref['module']
      unless instance_id.is_a?(String)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      interface_id = ref['interface']
      return validate_known_interface_ref!(connection, instance_interfaces, instance_id, interface_id) if interface_id.is_a?(String)

      port_id = ref['port']
      unless port_id.is_a?(String)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      validate_known_port_ref!(connection, instance_interfaces, instance_id, port_id)
    end

    def validate_known_interface_ref!(connection, instance_interfaces, instance_id, interface_id)
      interfaces = instance_interfaces[instance_id]
      unless interfaces
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown instance #{instance_id}"
      end
      unless interfaces.key?(interface_id)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{interface_id}"
      end

      [instance_id, interface_id]
    end

    def validate_known_port_ref!(connection, instance_interfaces, instance_id, port_id)
      interfaces = instance_interfaces[instance_id]
      unless interfaces
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown instance #{instance_id}"
      end
      return [instance_id, interfaces.key(port_id)] if interfaces.value?(port_id)
      return [instance_id, port_id] if interfaces.key?(port_id)

      raise Error,
            "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{port_id}"
    end

    def required_input_string(hash, key, context)
      value = hash[key]
      raise Error, "generator input #{context} is required" unless value
      raise Error, "generator input #{context} must be a string" unless value.is_a?(String)

      value
    end

    def output_manifest(manifest, input)
      {
        ipcore: manifest.fetch('id'),
        schema: input.fetch('schema'),
        instance_count: input.fetch('instances', []).size,
        connection_count: input.fetch('connections', []).size
      }
    end

    def generate_generic(manifest, input)
      write_json(File.join(@output_dir, 'manifest.json'), output_manifest(manifest, input))
    end

    def generate_finepaper_noc(manifest, input)
      router_module = module_id_for_role(manifest, 'router', 'XP')
      endpoint_module = module_id_for_role(manifest, 'endpoint', 'Endpoint')
      routers = instances_for_module(input, router_module)
      endpoints = instances_for_module(input, endpoint_module)

      rtl_dir = File.join(@output_dir, 'rtl')
      FileUtils.mkdir_p(rtl_dir)
      File.write(File.join(rtl_dir, 'top.v'), finepaper_top(routers, endpoints))
      File.write(File.join(@output_dir, 'filelist.f'), "rtl/top.v\n")
      write_json(File.join(@output_dir, 'manifest.json'), finepaper_output_manifest(manifest, input, routers, endpoints))
    end

    def generate_ravenoc(manifest, input)
      projection = ravenoc_projection(manifest, input)
      vendor_sources = copy_ravenoc_vendor_files

      File.write(File.join(@output_dir, 'ravenoc_config.svh'), ravenoc_config_header(projection.fetch(:defines)))
      File.write(File.join(@output_dir, 'ravenoc_top.sv'), ravenoc_top)
      File.write(File.join(@output_dir, 'ravenoc_filelist.f'), ravenoc_filelist(projection.fetch(:defines), vendor_sources))
      write_json(File.join(@output_dir, 'manifest.json'), ravenoc_output_manifest(manifest, input, projection))
    end

    def generate_opennoc(manifest, input)
      projection = opennoc_projection(manifest, input)

      write_json(File.join(@output_dir, 'opennoc_mesh.json'), projection.fetch(:mesh))
      wrapper = run_opennoc_mesh_generator(projection)
      vendor_sources = copy_opennoc_vendor_files
      File.write(File.join(@output_dir, 'opennoc_filelist.f'), opennoc_filelist(wrapper, vendor_sources))
      write_json(File.join(@output_dir, 'manifest.json'), opennoc_output_manifest(manifest, input, projection))
    end

    def ravenoc_projection(manifest, input)
      parameters = ravenoc_parameters(manifest, input)
      dimensions = ravenoc_dimensions(manifest, input, parameters)
      parameters = parameters.merge(
        'rows' => dimensions.fetch(:rows),
        'cols' => dimensions.fetch(:cols)
      )
      validate_ravenoc_parameters!(parameters)

      {
        parameters: parameters,
        rows: dimensions.fetch(:rows),
        cols: dimensions.fetch(:cols),
        tiles: dimensions.fetch(:tiles),
        defines: ravenoc_define_values(parameters)
      }
    end

    def ravenoc_parameters(manifest, input)
      parameters = RAVENOC_DEFAULTS.merge(ravenoc_manifest_parameter_defaults(manifest))
      ravenoc_parameter_sources(manifest, input).each do |source|
        parameters.merge!(source)
      end
      parameters
    end

    def ravenoc_manifest_parameter_defaults(manifest)
      manifest.fetch('parameters', {}).each_with_object({}) do |(name, spec), defaults|
        defaults[name] = spec['default'] if spec.is_a?(Hash) && spec.key?('default')
      end
    end

    def ravenoc_parameter_sources(manifest, input)
      [
        input.dig('project', 'global_parameters'),
        input.dig('project', 'instance', 'state', 'global_parameters'),
        input.dig('project', 'instance', 'parameters'),
        input['parameters'],
        ravenoc_single_wrapper_parameters(manifest, input)
      ].select { |source| source.is_a?(Hash) }
    end

    def ravenoc_dimensions(manifest, input, parameters)
      tiles = instances_for_module(input, module_id_for_role(manifest, 'tile', 'RaveTile'))
      unless tiles.empty?
        coordinates = mesh_coordinates(tiles, col_key: 'mesh_col', row_key: 'mesh_row', item_label: 'RaveTile')
        rows, cols = validate_rectangular_mesh!(coordinates, tiles.size, 'RaveTile graph')
        validate_ravenoc_mesh_size!(rows, cols)
        validate_ravenoc_mesh_links!(input.fetch('connections', []), coordinates)
        return { rows: rows, cols: cols, tiles: tiles.size }
      end

      wrappers = ravenoc_wrapper_instances(manifest, input)
      if wrappers.empty?
        raise Error, 'expected RaveTile instances or one RaveNoC wrapper instance'
      end
      if wrappers.size > 1
        raise Error, "expected at most one RaveNoC wrapper instance, found #{wrappers.size}"
      end

      wrapper_parameters = wrappers.first&.fetch('parameters', {}) || {}
      rows = positive_integer_value!(wrapper_parameters.fetch('rows', parameters.fetch('rows')), 'RaveNoC rows')
      cols = positive_integer_value!(wrapper_parameters.fetch('cols', parameters.fetch('cols')), 'RaveNoC cols')
      validate_ravenoc_mesh_size!(rows, cols)
      { rows: rows, cols: cols, tiles: rows * cols }
    end

    def ravenoc_single_wrapper_parameters(manifest, input)
      wrappers = ravenoc_wrapper_instances(manifest, input)
      return nil unless wrappers.size == 1

      wrappers.first.fetch('parameters', {})
    end

    def ravenoc_wrapper_instances(manifest, input)
      wrapper_modules = [
        module_id_for_role(manifest, 'wrapper', 'RaveNoC'),
        module_id_for_role(manifest, 'noc', 'RaveNoC'),
        'RaveNoC'
      ].uniq
      input.fetch('instances', []).select { |instance| wrapper_modules.include?(instance['module']) }
    end

    def positive_integer_value!(value, name)
      raise Error, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

      value
    end

    def validate_ravenoc_mesh_size!(rows, cols)
      raise Error, '1x1 is not a legal RaveNoC mesh' if rows == 1 && cols == 1
    end

    def validate_ravenoc_parameters!(parameters)
      rows = positive_integer_value!(parameters.fetch('rows'), 'RaveNoC rows')
      cols = positive_integer_value!(parameters.fetch('cols'), 'RaveNoC cols')
      validate_ravenoc_mesh_size!(rows, cols)

      buffer_depth = positive_integer_value!(parameters.fetch('flit_buffer_depth'), 'flit_buffer_depth')
      unless (buffer_depth & (buffer_depth - 1)).zero?
        raise Error, 'flit_buffer_depth must be a power of two'
      end

      %w[flit_data_width flit_type_width virtual_channels max_packet_flits axi_addr_width axi_data_width].each do |name|
        positive_integer_value!(parameters.fetch(name), name)
      end
      unless [32, 64].include?(parameters.fetch('flit_data_width'))
        raise Error, 'flit_data_width must be 32 or 64'
      end
      raise Error, 'flit_type_width must be 2' unless parameters.fetch('flit_type_width') == 2
      unless (1..32).include?(parameters.fetch('virtual_channels'))
        raise Error, 'virtual_channels must be 1-32'
      end
      unless parameters.fetch('axi_data_width') == parameters.fetch('flit_data_width')
        raise Error, 'axi_data_width must equal flit_data_width'
      end
      unless RAVENOC_ROUTING_MAP.key?(parameters.fetch('routing_algorithm'))
        raise Error, 'routing_algorithm must be xy or yx'
      end
      unless RAVENOC_PRIORITY_MAP.key?(parameters.fetch('priority'))
        raise Error, 'priority must be zero_high or zero_low'
      end

      validate_ravenoc_axi_cdc_required!(parameters)
    end

    def validate_ravenoc_axi_cdc_required!(parameters)
      noc_size = parameters.fetch('rows') * parameters.fetch('cols')
      value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase.delete('_')
      return if %w[all none].include?(value)
      return if value.match?(/\A[01]+\z/) && value.length == noc_size

      raise Error, "axi_cdc_required must be all, none, or a #{noc_size}-bit binary mask"
    end

    def validate_ravenoc_mesh_links!(connections, coordinates)
      actual_links = ravenoc_actual_mesh_links(connections, coordinates)
      missing = ravenoc_expected_mesh_links(coordinates).find { |link| !actual_links.include?(link) }
      return unless missing

      raise Error, "missing mesh link #{ravenoc_mesh_link_description(missing)}"
    end

    def ravenoc_expected_mesh_links(coordinates)
      id_by_coordinate = coordinates.to_h { |id, coordinate| [coordinate, id] }
      coordinates.flat_map do |id, (col, row)|
        links = []
        east_id = id_by_coordinate[[col + 1, row]]
        links << [id, 'east', east_id, 'west'] if east_id
        south_id = id_by_coordinate[[col, row + 1]]
        links << [id, 'south', south_id, 'north'] if south_id
        links
      end
    end

    def ravenoc_actual_mesh_links(connections, coordinates)
      connections.filter_map do |connection|
        endpoints = ravenoc_connection_endpoints(connection)
        next unless endpoints.size == 2
        next unless endpoints.all? { |endpoint| coordinates.key?(endpoint.fetch(:instance)) }

        ravenoc_canonical_mesh_link(endpoints.fetch(0), endpoints.fetch(1), coordinates)
      end
    end

    def ravenoc_connection_endpoints(connection)
      refs = connection.fetch('interfaces', nil)
      return [] unless refs.is_a?(Array) && refs.size == 2

      refs.map do |ref|
        next nil unless ref.is_a?(Hash)

        instance = ref['instance'] || ref['module']
        interface = ref['interface'] || ref['port']
        next nil unless instance && interface

        { instance: instance, port: interface.to_s.downcase.delete_prefix('if_') }
      end.compact
    end

    def ravenoc_canonical_mesh_link(left, right, coordinates)
      left_id = left.fetch(:instance)
      right_id = right.fetch(:instance)
      left_col, left_row = coordinates.fetch(left_id)
      right_col, right_row = coordinates.fetch(right_id)
      left_port = left.fetch(:port)
      right_port = right.fetch(:port)

      case [right_col - left_col, right_row - left_row, left_port, right_port]
      when [1, 0, 'east', 'west']
        [left_id, 'east', right_id, 'west']
      when [-1, 0, 'west', 'east']
        [right_id, 'east', left_id, 'west']
      when [0, 1, 'south', 'north']
        [left_id, 'south', right_id, 'north']
      when [0, -1, 'north', 'south']
        [right_id, 'south', left_id, 'north']
      end
    end

    def ravenoc_mesh_link_description(link)
      from_id, from_port, to_id, to_port = link
      "#{from_id}.#{from_port} -> #{to_id}.#{to_port}"
    end

    def ravenoc_define_values(parameters)
      RAVENOC_DEFINE_NAMES.to_h do |parameter_name, define_name|
        [define_name, ravenoc_define_value(parameter_name, parameters.fetch(parameter_name))]
      end
    end

    def ravenoc_define_value(parameter_name, value)
      case parameter_name
      when 'routing_algorithm'
        RAVENOC_ROUTING_MAP.fetch(value, value)
      when 'priority'
        RAVENOC_PRIORITY_MAP.fetch(value, value)
      else
        value
      end
    end

    def ravenoc_config_header(defines)
      lines = [
        '`ifndef FINEPAPER_RAVENOC_CONFIG_SVH',
        '`define FINEPAPER_RAVENOC_CONFIG_SVH'
      ]
      lines.concat(defines.map { |name, value| "`define #{name} #{value}" })
      lines << '`endif'
      "#{lines.join("\n")}\n"
    end

    def ravenoc_filelist(defines, vendor_sources)
      lines = ['+incdir+.']
      lines << '+incdir+vendor/ravenoc/src/include'
      lines.concat(defines.map { |name, value| "+define+#{name}=#{value}" })
      lines.concat(vendor_sources)
      lines << 'ravenoc_top.sv'
      "#{lines.join("\n")}\n"
    end

    def ravenoc_top
      <<~SV
        // Generated by ipcraft_generator for finepaper.ravenoc
        module ravenoc_top (
          input logic clk_noc,
          input logic arst_noc
        );
        endmodule
      SV
    end

    def opennoc_projection(manifest, input)
      mappings = opennoc_module_mappings(manifest)
      xp_modules = modules_for_upstream_type(mappings, 'XP')
      if xp_modules.empty?
        raise Error, 'OpenNoC generation.module_mappings must include an XP mapping'
      end

      agent_type_by_module = mappings.select { |_module_id, upstream_type| OPENNOC_AGENT_TYPES.include?(upstream_type) }
      instances = input.fetch('instances', [])
      xps = instances.select { |instance| xp_modules.include?(instance['module']) }
      raise Error, 'expected at least one OpenNoC XP instance' if xps.empty?

      coordinates = opennoc_xp_coordinates(xps)
      rows, cols = validate_rectangular_opennoc_mesh!(coordinates, xps.size)
      agent_slots = opennoc_agent_slots(input, coordinates, agent_type_by_module)

      {
        mesh: opennoc_mesh_json(xps, coordinates, agent_slots),
        rows: rows,
        cols: cols
      }
    end

    def opennoc_module_mappings(manifest)
      mappings = manifest.dig('generation', 'module_mappings')
      raise Error, 'OpenNoC generation.module_mappings must be an object' unless mappings.is_a?(Hash)

      mappings
    end

    def modules_for_upstream_type(mappings, upstream_type)
      mappings.select { |_module_id, mapped_type| mapped_type == upstream_type }.keys
    end

    def opennoc_xp_coordinates(xps)
      mesh_coordinates(xps, col_key: 'mesh_col', row_key: 'mesh_row', item_label: 'OpenNoCXP')
    end

    def validate_rectangular_opennoc_mesh!(coordinates, xp_count)
      validate_rectangular_mesh!(coordinates, xp_count, 'OpenNoCXP graph')
    end

    def mesh_coordinates(instances, col_key:, row_key:, item_label:)
      instances.to_h do |instance|
        parameters = instance.fetch('parameters', {})
        x = parameters.fetch(col_key, nil)
        y = parameters.fetch(row_key, nil)
        unless x.is_a?(Integer) && y.is_a?(Integer)
          raise Error, "#{item_label} #{artifact_id(instance)} #{col_key}/#{row_key} must be integers"
        end

        [instance.fetch('id'), [x, y]]
      end
    end

    def validate_rectangular_mesh!(coordinates, instance_count, graph_label)
      if coordinates.size != instance_count
        raise Error, "#{graph_label} has duplicate instance ids"
      end

      negative = coordinates.find { |_id, (x, y)| x.negative? || y.negative? }
      if negative
        _id, coordinate = negative
        raise Error, "#{graph_label} has negative coordinate #{coordinate.join(',')}"
      end

      duplicate = coordinates.group_by { |_id, coordinate| coordinate }.find { |_coordinate, entries| entries.size > 1 }
      if duplicate
        coordinate, entries = duplicate
        ids = entries.map(&:first).join(', ')
        raise Error, "#{graph_label} has duplicate coordinate #{coordinate.join(',')} for #{ids}"
      end

      cols = coordinates.values.map(&:first).max + 1
      rows = coordinates.values.map(&:last).max + 1
      occupied = coordinates.values
      (0...rows).each do |row|
        (0...cols).each do |col|
          next if occupied.include?([col, row])

          raise Error, "#{graph_label} has missing coordinate #{col},#{row}"
        end
      end

      [rows, cols]
    end

    def opennoc_agent_slots(input, coordinates, agent_type_by_module)
      instances = input.fetch('instances', [])
      module_by_id = instances.to_h { |instance| [instance.fetch('id'), instance] }
      interface_ports = interface_ports_by_instance(instances)
      slots = {}

      input.fetch('connections', []).each do |connection|
        binding = opennoc_agent_connection(connection, interface_ports, module_by_id, coordinates, agent_type_by_module)
        next unless binding
        next if binding == :mesh

        slot_key = [binding.fetch(:xp_id), binding.fetch(:slot)]
        if slots.key?(slot_key)
          xp = module_by_id.fetch(binding.fetch(:xp_id))
          raise Error, "multiple OpenNoC agents connect to #{artifact_id(xp)}.#{binding.fetch(:slot)}"
        end

        slots[slot_key] = binding.fetch(:agent_type)
      end

      slots
    end

    def opennoc_agent_connection(connection, interface_ports, module_by_id, coordinates, agent_type_by_module)
      endpoints = connection_endpoints(connection, interface_ports)
      return nil unless endpoints.size == 2

      left, right = endpoints
      left_type = upstream_type_for_endpoint(left, module_by_id, agent_type_by_module)
      right_type = upstream_type_for_endpoint(right, module_by_id, agent_type_by_module)
      xp_ids = coordinates.keys
      left_is_xp = xp_ids.include?(left.fetch(:instance))
      right_is_xp = xp_ids.include?(right.fetch(:instance))
      left_is_agent = OPENNOC_AGENT_TYPES.include?(left_type)
      right_is_agent = OPENNOC_AGENT_TYPES.include?(right_type)

      if left_is_xp && right_is_xp
        validate_opennoc_mesh_connection!(connection, left, right, coordinates)
        return :mesh
      end

      return nil unless left_is_xp || right_is_xp || left_is_agent || right_is_agent

      if left_is_agent && right_is_xp
        return opennoc_agent_binding(connection, left, left_type, right)
      end

      if right_is_agent && left_is_xp
        return opennoc_agent_binding(connection, right, right_type, left)
      end

      raise Error, "invalid OpenNoC agent connection #{connection.fetch('id', '<unnamed>')}"
    end

    def upstream_type_for_endpoint(endpoint, module_by_id, agent_type_by_module)
      instance = module_by_id[endpoint.fetch(:instance)]
      return nil unless instance

      agent_type_by_module[instance['module']]
    end

    def opennoc_agent_binding(connection, agent_endpoint, agent_type, xp_endpoint)
      agent_port = agent_endpoint.fetch(:port).to_s.downcase
      xp_slot = xp_endpoint.fetch(:port).to_s.downcase
      if agent_port == 'chi' && %w[p0 p1].include?(xp_slot)
        return {
          agent_id: agent_endpoint.fetch(:instance),
          xp_id: xp_endpoint.fetch(:instance),
          slot: xp_slot,
          agent_type: agent_type
        }
      end

      raise Error, "invalid OpenNoC agent connection #{connection.fetch('id', '<unnamed>')}"
    end

    def validate_opennoc_mesh_connection!(connection, left, right, coordinates)
      return if valid_opennoc_mesh_connection?(left, right, coordinates)

      raise Error, "invalid OpenNoC XP mesh connection #{connection.fetch('id', '<unnamed>')}"
    end

    def valid_opennoc_mesh_connection?(left, right, coordinates)
      left_x, left_y = coordinates.fetch(left.fetch(:instance))
      right_x, right_y = coordinates.fetch(right.fetch(:instance))

      case [left.fetch(:port).to_s.downcase, right.fetch(:port).to_s.downcase]
      when %w[east west]
        right_x == left_x + 1 && right_y == left_y
      when %w[west east]
        right_x == left_x - 1 && right_y == left_y
      when %w[south north]
        right_x == left_x && right_y == left_y + 1
      when %w[north south]
        right_x == left_x && right_y == left_y - 1
      else
        false
      end
    end

    def connection_endpoints(connection, interface_ports)
      if connection.key?('interfaces')
        refs = connection.fetch('interfaces')
        unless refs.is_a?(Array) && refs.size == 2
          raise Error, "generator input connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
        end

        return refs.map { |ref| endpoint_from_interface_ref(ref, interface_ports, connection) }
      end

      if connection.key?('source') && connection.key?('target')
        return %w[source target].map do |key|
          endpoint_from_direct_ref(connection.fetch(key), interface_ports, connection, key)
        end
      end

      if connection.key?('from') && connection.key?('to')
        return %w[from to].map do |key|
          endpoint_from_direct_ref(connection.fetch(key), interface_ports, connection, key)
        end
      end

      []
    end

    def endpoint_from_interface_ref(ref, interface_ports, connection)
      unless ref.is_a?(Hash)
        raise Error, "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      instance_id = ref['instance'] || ref['module']
      interface_id = ref['interface'] || ref['port']
      unless instance_id && interface_id
        raise Error, "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      { instance: instance_id, port: port_for_interface!(instance_id, interface_id, interface_ports, connection) }
    end

    def endpoint_from_direct_ref(ref, interface_ports, connection, endpoint_name)
      unless ref.is_a?(Hash)
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      instance_id = ref['instance'] || ref['module']
      unless instance_id
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      port = ref['port']
      port ||= port_for_interface!(instance_id, ref['interface'], interface_ports, connection) if ref.key?('interface')
      unless port
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      { instance: instance_id, port: port }
    end

    def interface_ports_by_instance(instances)
      instances.to_h do |instance|
        interfaces = instance.fetch('interfaces', [])
        unless interfaces.is_a?(Array)
          raise Error, "generator input instance #{instance.fetch('id', '<unnamed>')} interfaces must be an array"
        end

        ports = interfaces.to_h do |interface|
          id = interface.fetch('id', interface['port'])
          [id, interface.fetch('port', id)]
        end
        [instance.fetch('id'), ports]
      end
    end

    def port_for_interface!(instance_id, interface_id, interface_ports, connection)
      unless instance_id && interface_id
        raise Error, "generator input connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      ports = interface_ports[instance_id]
      unless ports
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown instance #{instance_id}"
      end

      port = ports[interface_id]
      unless port
        raise Error,
              "generator input connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{interface_id}"
      end

      port
    end

    def opennoc_mesh_json(xps, coordinates, agent_slots)
      xp_by_id = xps.to_h { |xp| [xp.fetch('id'), xp] }

      coordinates.keys.sort_by { |xp_id| coordinates.fetch(xp_id).reverse }.to_h do |xp_id|
        x, y = coordinates.fetch(xp_id)
        [
          artifact_id(xp_by_id.fetch(xp_id)),
          {
            'X' => x,
            'Y' => y,
            'P0' => agent_slots.fetch([xp_id, 'p0'], 'NONE'),
            'P1' => agent_slots.fetch([xp_id, 'p1'], 'NONE')
          }
        ]
      end
    end

    def opennoc_output_manifest(manifest, input, projection)
      output_manifest(manifest, input).merge(
        topology: 'mesh',
        rows: projection.fetch(:rows),
        cols: projection.fetch(:cols)
      )
    end

    def ravenoc_output_manifest(manifest, input, projection)
      output_manifest(manifest, input).merge(
        topology: 'mesh',
        rows: projection.fetch(:rows),
        cols: projection.fetch(:cols),
        tiles: projection.fetch(:tiles),
        parameters: projection.fetch(:parameters)
      )
    end

    def module_id_for_role(manifest, role, fallback)
      mappings = manifest.dig('generation', 'module_mappings') || {}
      mappings.find { |_module_id, mapped_role| mapped_role == role }&.first || fallback
    end

    def instances_for_module(input, module_id)
      input.fetch('instances', []).select { |instance| instance['module'] == module_id }
    end

    def finepaper_output_manifest(manifest, input, routers, endpoints)
      output_manifest(manifest, input).merge(
        routers: routers.size,
        endpoints: endpoints.size
      )
    end

    def finepaper_top(routers, endpoints)
      lines = [
        '// Generated by ipcraft_generator for finepaper.noc',
        'module top;',
        '  // Routers'
      ]
      lines.concat(routers.map { |router| "  // XP #{instance_summary(router)}" })
      lines << '  // Endpoints'
      lines.concat(endpoints.map { |endpoint| "  // Endpoint #{instance_summary(endpoint)}" })
      lines << 'endmodule'
      "#{lines.join("\n")}\n"
    end

    def instance_summary(instance)
      parameters = instance.fetch('parameters', {})
      parameter_text = parameters.map { |key, value| "#{key}=#{value}" }.join(', ')
      [instance.fetch('id'), parameter_text].reject(&:empty?).join(' ')
    end

    def copy_ravenoc_vendor_files
      RAVENOC_VENDOR_FILES.each do |relative|
        copy_package_file("vendor/ravenoc/#{relative}", "vendor/ravenoc/#{relative}")
      end
      RAVENOC_VENDOR_SOURCE_FILES.map { |relative| "vendor/ravenoc/#{relative}" }
    end

    def run_opennoc_mesh_generator(projection)
      wrapper = "mesh_wrapper_#{projection.fetch(:cols)}x#{projection.fetch(:rows)}.sv"
      mesh_generator_root = package_path('vendor/OpenNoC/tools/mesh_generator')
      raise Error, 'OpenNoC vendor mesh generator is missing.' unless File.directory?(mesh_generator_root)

      Dir.mktmpdir('ipcraft-opennoc-mesh') do |dir|
        FileUtils.cp_r(mesh_generator_root, dir)
        working_dir = File.join(dir, 'mesh_generator')
        stdout, stderr, status = Open3.capture3(
          'python3',
          'mesh_gen.py',
          '-f',
          File.expand_path(File.join(@output_dir, 'opennoc_mesh.json')),
          chdir: working_dir
        )
        unless status.success?
          message = stderr.empty? ? stdout : stderr
          raise Error, "OpenNoC mesh generator failed: #{message.strip}"
        end

        generated_wrapper = File.join(working_dir, wrapper)
        raise Error, "OpenNoC mesh generator did not create #{wrapper}" unless File.file?(generated_wrapper)

        output_relative = "rtl/#{wrapper}"
        output_path = File.join(@output_dir, output_relative)
        FileUtils.mkdir_p(File.dirname(output_path))
        FileUtils.cp(generated_wrapper, output_path)
        output_relative
      end
    end

    def copy_opennoc_vendor_files
      copied = OPENNOC_VENDOR_FILES.map do |relative|
        copy_package_file("vendor/OpenNoC/#{relative}", "vendor/OpenNoC/#{relative}")
      end
      copied.select { |relative| relative.end_with?('.v', '.sv') }
    end

    def opennoc_filelist(wrapper, vendor_sources)
      "#{([wrapper] + vendor_sources).uniq.join("\n")}\n"
    end

    def package_root
      File.dirname(File.expand_path(@manifest_path))
    end

    def package_path(relative)
      root = package_root
      expanded = File.expand_path(relative, root)
      unless expanded == root || expanded.start_with?("#{root}#{File::SEPARATOR}")
        raise Error, "package path escapes package root: #{relative}"
      end
      expanded
    end

    def copy_package_file(source_relative, output_relative)
      source = package_path(source_relative)
      raise Error, "package file is missing: #{source_relative}" unless File.file?(source)

      destination = File.join(@output_dir, output_relative)
      FileUtils.mkdir_p(File.dirname(destination))
      FileUtils.cp(source, destination)
      output_relative
    end

    def artifact_id(instance)
      parameters = instance.fetch('parameters', {})
      safe_identifier(parameters.fetch('external_id', instance.fetch('id')))
    end

    def safe_identifier(value)
      identifier = value.to_s.gsub(/[^a-zA-Z0-9_$]/, '_')
      identifier.match?(/\A[a-zA-Z_]/) ? identifier : "ipcraft_#{identifier}"
    end

    def write_json(path, value)
      File.write(path, "#{JSON.pretty_generate(value)}\n")
    end
  end
end
