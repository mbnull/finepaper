require 'fileutils'
require 'json'
require 'optparse'

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
      'finepaper.opennoc' => :generate_opennoc
    }.freeze
    OPENNOC_AGENT_TYPES = %w[RNF RNI HNF HNI SNF].freeze

    def initialize(manifest:, input:, output:)
      @manifest_path = manifest
      @input_path = input
      @output_dir = output
    end

    def generate
      manifest = JSON.parse(File.read(@manifest_path))
      input = JSON.parse(File.read(@input_path))

      validate!(manifest, input)

      FileUtils.mkdir_p(@output_dir)
      send(PACKAGE_HANDLERS.fetch(manifest.fetch('id'), :generate_generic), manifest, input)
    end

    private

    def validate!(manifest, input)
      raise Error, 'manifest schema must be ipcraft.manifest.v1' unless manifest['schema'] == 'ipcraft.manifest.v1'
      raise Error, 'input schema must be ipcraft.noc.project.v1' unless input['schema'] == 'ipcraft.noc.project.v1'
      raise Error, 'input package does not match manifest id' unless input['package'] == manifest['id']
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

    def generate_opennoc(manifest, input)
      projection = opennoc_projection(manifest, input)

      write_json(File.join(@output_dir, 'opennoc_mesh.json'), projection.fetch(:mesh))
      write_json(File.join(@output_dir, 'manifest.json'), opennoc_output_manifest(manifest, input, projection))
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
      xps.to_h do |xp|
        parameters = xp.fetch('parameters', {})
        x = parameters.fetch('mesh_col', nil)
        y = parameters.fetch('mesh_row', nil)
        unless x.is_a?(Integer) && y.is_a?(Integer)
          raise Error, "OpenNoCXP #{artifact_id(xp)} mesh_col/mesh_row must be integers"
        end

        [xp.fetch('id'), [x, y]]
      end
    end

    def validate_rectangular_opennoc_mesh!(coordinates, xp_count)
      if coordinates.size != xp_count
        raise Error, 'OpenNoCXP graph has duplicate instance ids'
      end

      negative = coordinates.find { |_id, (x, y)| x.negative? || y.negative? }
      if negative
        _id, coordinate = negative
        raise Error, "OpenNoCXP graph has negative coordinate #{coordinate.join(',')}"
      end

      duplicate = coordinates.group_by { |_id, coordinate| coordinate }.find { |_coordinate, entries| entries.size > 1 }
      if duplicate
        coordinate, entries = duplicate
        ids = entries.map(&:first).join(', ')
        raise Error, "OpenNoCXP graph has duplicate coordinate #{coordinate.join(',')} for #{ids}"
      end

      cols = coordinates.values.map(&:first).max + 1
      rows = coordinates.values.map(&:last).max + 1
      occupied = coordinates.values
      (0...rows).each do |row|
        (0...cols).each do |col|
          next if occupied.include?([col, row])

          raise Error, "OpenNoCXP graph has missing coordinate #{col},#{row}"
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
          raise Error, "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} must have exactly two interfaces"
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
        raise Error, "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      instance_id = ref['instance'] || ref['module']
      interface_id = ref['interface'] || ref['port']
      unless instance_id && interface_id
        raise Error, "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      { instance: instance_id, port: port_for_interface!(instance_id, interface_id, interface_ports, connection) }
    end

    def endpoint_from_direct_ref(ref, interface_ports, connection, endpoint_name)
      unless ref.is_a?(Hash)
        raise Error,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      instance_id = ref['instance'] || ref['module']
      unless instance_id
        raise Error,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      port = ref['port']
      port ||= port_for_interface!(instance_id, ref['interface'], interface_ports, connection) if ref.key?('interface')
      unless port
        raise Error,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid #{endpoint_name} endpoint reference"
      end

      { instance: instance_id, port: port }
    end

    def interface_ports_by_instance(instances)
      instances.to_h do |instance|
        interfaces = instance.fetch('interfaces', [])
        unless interfaces.is_a?(Array)
          raise Error, "ipcraft.noc.project.v1 instance #{instance.fetch('id', '<unnamed>')} interfaces must be an array"
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
        raise Error, "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} has invalid interface reference"
      end

      ports = interface_ports[instance_id]
      unless ports
        raise Error,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} references unknown instance #{instance_id}"
      end

      port = ports[interface_id]
      unless port
        raise Error,
              "ipcraft.noc.project.v1 connection #{connection.fetch('id', '<unnamed>')} references unknown interface #{instance_id}.#{interface_id}"
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
