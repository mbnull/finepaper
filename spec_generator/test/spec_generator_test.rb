$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'spec_generator'

class SpecGeneratorTest < Minitest::Test
  def test_generates_qt_bundle_graphics_and_ruby_models
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', valid_spec_yaml)
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      SpecGenerator.generate(
        spec_path: spec_path,
        views_dir: File.join(dir, 'spec/views'),
        qt_bundle_dir: File.join(dir, 'qt/bundles'),
        ruby_model_dir: File.join(dir, 'framework/src/ruby/model')
      )

      modules_xml = File.read(File.join(dir, 'qt/bundles/modules.xml'))
      assert_includes modules_xml, '<bus name="ni_link"'
      assert_includes modules_xml, '<match field="protocol" />'
      assert_includes modules_xml, '<interface id="local3" bus="ni_link" role="target" connects_to="initiator" match="protocol,data_width">'
      assert_includes modules_xml, '<accept field="data_width" values="32,64,128" />'
      assert_includes modules_xml, '<port id="ep3" direction="output" type="bus" bus_type="ni_link" role="attachment" name="EP3" description="Local endpoint slot 3" interface="local3" />'
      assert_includes modules_xml, '<choice value="chi" label="chi" />'

      xp_graphics = File.read(File.join(dir, 'qt/bundles/graphics/XP.xml'))
      assert_includes xp_graphics, '<module-graphics type="XP">'
      assert_includes xp_graphics, '<graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">'

      xp_model = File.read(File.join(dir, 'framework/src/ruby/model/xp.rb'))
      assert_includes xp_model, 'attr_reader :id, :x, :y, :endpoints, :config'
      assert_includes xp_model, "routing_algorithm: { type: :string, default: 'xy', enum: ['xy', 'yx'] }"

      endpoint_model = File.read(File.join(dir, 'framework/src/ruby/model/endpoint.rb'))
      assert_includes endpoint_model, 'attr_reader :id, :type, :protocol, :data_width, :config'
      assert_includes endpoint_model, 'buffer_depth: { type: :integer, default: 16 }'
    end
  end

  def test_rejects_unknown_top_level_fields
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', valid_spec_yaml.sub("version: '1.0'\n", "version: '1.0'\nextra: nope\n"))
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate(
          spec_path: spec_path,
          views_dir: File.join(dir, 'spec/views'),
          qt_bundle_dir: File.join(dir, 'out/qt'),
          ruby_model_dir: File.join(dir, 'out/ruby')
        )
      end
      assert_match(/Unknown top-level field: extra/, error.message)
    end
  end

  def test_rejects_interface_accepts_values_outside_bus_enum
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', valid_spec_yaml.sub('protocol: [axi4, chi]', 'protocol: [axi4, wishbone]'))
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate(
          spec_path: spec_path,
          views_dir: File.join(dir, 'spec/views'),
          qt_bundle_dir: File.join(dir, 'out/qt'),
          ruby_model_dir: File.join(dir, 'out/ruby')
        )
      end
      assert_match(/XP.local0 accepts protocol value wishbone outside ni_link enum/, error.message)
    end
  end

  def test_rejects_view_interface_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', valid_spec_yaml)
      write_file(dir, 'spec/views/XP.xml', xp_view_xml.sub('ref="local3"', 'ref="local4"'))
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate(
          spec_path: spec_path,
          views_dir: File.join(dir, 'spec/views'),
          qt_bundle_dir: File.join(dir, 'out/qt'),
          ruby_model_dir: File.join(dir, 'out/ruby')
        )
      end
      assert_match(/view XP references unknown interface local4/, error.message)
    end
  end

  def test_cli_generates_outputs
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', valid_spec_yaml)
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--spec', spec_path,
        '--views', File.join(dir, 'spec/views'),
        '--qt-bundle', File.join(dir, 'qt/bundles'),
        '--ruby-model', File.join(dir, 'framework/src/ruby/model')
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated'
      assert File.file?(File.join(dir, 'qt/bundles/modules.xml'))
      assert File.file?(File.join(dir, 'framework/src/ruby/model/xp.rb'))
    end
  end

  private

  def write_file(root, relative_path, content)
    path = File.join(root, relative_path)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, content)
    path
  end

  def valid_spec_yaml
    <<~YAML
      schema: v1
      kind: noc-definition
      name: NoC
      version: '1.0'
      buses:
        ni_link:
          description: Endpoint-to-router NoC interface.
          compatibility:
            roles:
              initiator: [target]
              target: [initiator]
            match: [protocol, data_width]
          config:
            protocol:
              type: string
              enum: [axi4, chi]
              default: axi4
              description: Interface protocol.
            data_width:
              type: int
              enum: [32, 64, 128]
              default: 64
              description: Data width in bits.
          signals:
            - name: flit
              direction: initiator_to_target
              width: FLIT_WIDTH
        router_link:
          description: Router-to-router NoC link.
          compatibility:
            roles:
              peer: [peer]
            match: []
          config: {}
          signals:
            - name: flit
              direction: peer_to_peer
              width: FLIT_WIDTH
      modules:
        XP:
          palette_label: XP
          graph_group: xps
          description: Mesh router with four directional links and four local endpoint attachment slots.
          identity:
            external_id_prefix: xp
            display_prefix: XP
            width: 2
            supports_mesh_coordinates: true
          capabilities:
            supports_collapse: true
          interface_limits:
            ni_link:
              max: 4
          parameters:
            x: { type: int, default: 0, configurable: false, emit: attribute, description: Canvas X position. }
            y: { type: int, default: 0, configurable: false, emit: attribute, description: Canvas Y position. }
            display_name: { type: string, default: '', emit: editor, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: '', emit: editor, label: External ID, description: Framework-facing identifier. }
            collapsed: { type: bool, default: true, configurable: false, emit: editor_only, description: Collapsed editor state. }
            routing_algorithm: { type: string, enum: [xy, yx], default: xy, emit: config, label: Routing algorithm, description: Router path selection strategy. }
            vc_count: { type: int, default: 2, emit: config, label: VC count, description: Virtual channel count. }
            buffer_depth: { type: int, default: 8, emit: config, label: Buffer depth, description: Depth of each input buffer. }
          interfaces:
            local0: { bus: ni_link, role: target, accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }, port: { id: ep0, direction: output, type: bus, bus_type: ni_link, role: attachment, name: EP0, description: Local endpoint slot 0 } }
            local1: { bus: ni_link, role: target, accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }, port: { id: ep1, direction: output, type: bus, bus_type: ni_link, role: attachment, name: EP1, description: Local endpoint slot 1 } }
            local2: { bus: ni_link, role: target, accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }, port: { id: ep2, direction: output, type: bus, bus_type: ni_link, role: attachment, name: EP2, description: Local endpoint slot 2 } }
            local3: { bus: ni_link, role: target, accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }, port: { id: ep3, direction: output, type: bus, bus_type: ni_link, role: attachment, name: EP3, description: Local endpoint slot 3 } }
            north: { bus: router_link, role: peer, ports: [{ id: north_in, direction: input, type: bus, bus_type: router_link, role: router, name: N, description: North router ingress }, { id: north_out, direction: output, type: bus, bus_type: router_link, role: router, name: N, description: North router egress }] }
            east: { bus: router_link, role: peer, ports: [{ id: east_in, direction: input, type: bus, bus_type: router_link, role: router, name: E, description: East router ingress }, { id: east_out, direction: output, type: bus, bus_type: router_link, role: router, name: E, description: East router egress }] }
            south: { bus: router_link, role: peer, ports: [{ id: south_in, direction: input, type: bus, bus_type: router_link, role: router, name: S, description: South router ingress }, { id: south_out, direction: output, type: bus, bus_type: router_link, role: router, name: S, description: South router egress }] }
            west: { bus: router_link, role: peer, ports: [{ id: west_in, direction: input, type: bus, bus_type: router_link, role: router, name: W, description: West router ingress }, { id: west_out, direction: output, type: bus, bus_type: router_link, role: router, name: W, description: West router egress }] }
        Endpoint:
          palette_label: Endpoint
          graph_group: endpoints
          description: Endpoint interface block that terminates a local NoC connection.
          identity:
            external_id_prefix: ep
            display_prefix: EP
            width: 2
            supports_mesh_coordinates: false
          parameters:
            display_name: { type: string, default: '', emit: editor, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: '', emit: editor, label: External ID, description: Framework-facing identifier. }
            type: { type: string, enum: [master, slave], default: master, emit: attribute, label: Type, description: Endpoint traffic role. }
            protocol: { type: string, enum: [axi4, chi], default: axi4, emit: attribute, label: Protocol, description: Interface protocol. }
            data_width: { type: int, enum: [32, 64, 128], default: 64, emit: attribute, label: Data width, description: Bus width in bits. }
            qos_enabled: { type: bool, default: false, emit: config, label: QoS enabled, description: Enable QoS tagging support. }
            buffer_depth: { type: int, default: 16, emit: config, label: Buffer depth, description: Ingress buffer depth. }
          interfaces:
            noc:
              bus: ni_link
              role: initiator
              config:
                protocol: { parameter: protocol }
                data_width: { parameter: data_width }
              port: { id: noc, direction: input, type: bus, bus_type: ni_link, role: attachment, name: NoC, description: NoC attachment input }
    YAML
  end

  def xp_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="XP">
        <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
          <expanded min_width="136" height="116" caption_left="30" caption_top="6" port_inset="16" />
          <collapsed min_width="104" height="92" caption_left="30" caption_top="26" endpoint_inset="18" />
          <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
        </graphics>
        <interfaces>
          <interface ref="local0" side="west" slot="0" />
          <interface ref="local1" side="west" slot="1" />
          <interface ref="local2" side="west" slot="2" />
          <interface ref="local3" side="west" slot="3" />
          <interface ref="north" side="north" />
          <interface ref="east" side="east" />
          <interface ref="south" side="south" />
          <interface ref="west" side="west" />
        </interfaces>
      </module-view>
    XML
  end

  def endpoint_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="Endpoint">
        <graphics layout="endpoint" node_color="#d6f4b6">
          <expanded min_width="104" height="54" caption_left="8" caption_top="6" />
          <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
        </graphics>
        <interfaces>
          <interface ref="noc" side="east" />
        </interfaces>
      </module-view>
    XML
  end
end
