$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'fileutils'
require 'json'
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

  def test_generates_interface_anchor_bundle_for_renamed_noc_modules
    Dir.mktmpdir do |dir|
      spec_path = write_file(dir, 'spec/noc.yaml', renamed_interface_anchor_spec_yaml)
      write_file(dir, 'spec/views/RouterTile.xml', router_tile_view_xml)
      write_file(dir, 'spec/views/NetworkPort.xml', network_port_view_xml)

      SpecGenerator.generate(
        spec_path: spec_path,
        views_dir: File.join(dir, 'spec/views'),
        qt_bundle_dir: File.join(dir, 'qt/bundles'),
        ruby_model_dir: File.join(dir, 'framework/src/ruby/model')
      )

      modules_xml = File.read(File.join(dir, 'qt/bundles/modules.xml'))
      assert_includes modules_xml, '<module name="RouterTile"'
      assert_includes modules_xml, '<interface id="east" label="East" bus="router_link" role="initiator" connects_to="target" match="">'
      assert_includes modules_xml, '<port id="east" direction="output" type="bus" bus_type="router_link" role="router" name="East" description="East router interface" interface="east" />'
      refute_includes modules_xml, 'east_in'
      refute_includes modules_xml, 'east_out'

      graphics_xml = File.read(File.join(dir, 'qt/bundles/graphics/RouterTile.xml'))
      assert_includes graphics_xml, '<anchors>'
      assert_includes graphics_xml, '<anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />'

      assert File.file?(File.join(dir, 'framework/src/ruby/model/xp.rb'))
      assert File.file?(File.join(dir, 'framework/src/ruby/model/endpoint.rb'))
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

  def test_generates_ravenoc_extension_runtime_bundle
    Dir.mktmpdir do |dir|
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', ravenoc_extension_yaml)
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      SpecGenerator.generate_extension(
        extension_path: extension_path,
        views_dir: File.join(dir, 'spec/noc/views'),
        bundle_dir: File.join(dir, 'plugins/ravenoc')
      )

      plugin_json = JSON.parse(File.read(File.join(dir, 'plugins/ravenoc/plugin.json')))
      assert_equal 'finepaper.ravenoc', plugin_json.fetch('id')
      assert_equal 'RaveNoC', plugin_json.fetch('name')
      assert_equal '1.0', plugin_json.fetch('version')
      assert_equal 'modules.xml', plugin_json.fetch('modules')
      assert_equal 'graphics', plugin_json.fetch('graphics')
      assert_equal 'ruby', plugin_json.fetch('generator').fetch('command')
      assert_equal 'generic_graph_v1', plugin_json.fetch('generator').fetch('input_format')
      assert_equal 'generator/bin/generate', plugin_json.fetch('generator').fetch('args').first
      assert_equal 'ruby', plugin_json.fetch('drc').fetch('command')
      assert_equal 'generic_graph_v1', plugin_json.fetch('drc').fetch('input_format')
      assert_equal 'generator/bin/drc', plugin_json.fetch('drc').fetch('args').first
      presets = plugin_json.fetch('topology_presets')
      assert_equal 1, presets.size
      assert_equal 'mesh', presets.first.fetch('id')
      assert_equal 'RaveTile', presets.first.fetch('router_module')
      assert_equal 'rave_{row}_{col}', presets.first.fetch('id_pattern')
      assert_equal 2, presets.first.fetch('parameters').fetch('rows').fetch('default')
      assert_equal({ 'enabled' => false, 'library' => '' }, plugin_json.fetch('native'))

      modules_xml = File.read(File.join(dir, 'plugins/ravenoc/modules.xml'))
      refute_includes modules_xml, '<buses>'
      assert_includes modules_xml, '<module name="RaveTile" palette_label="Rave Tile" graph_group="xps"'
      assert_includes modules_xml, '<module name="RaveEndpoint" palette_label="Rave Endpoint" graph_group="endpoints"'
      assert_includes modules_xml, '<port id="east" direction="inout" type="bus" bus_type="ravenoc_router_link" role="router" name="East" description="East RaveNoC router link" interface="east" />'
      assert_includes modules_xml, '<parameter name="bypass_cdc" type="bool" default="false" label="Bypass CDC in smoke" description="Drive generated bypass_cdc vector high in the smoke wrapper." />'
      assert_includes modules_xml, '<choice value="xy" label="XY" />'
      assert_includes modules_xml, '<choice value="zero_low" label="Zero Low" />'

      rave_tile_graphics = File.read(File.join(dir, 'plugins/ravenoc/graphics/RaveTile.xml'))
      assert_includes rave_tile_graphics, '<module-graphics type="RaveTile">'
      assert_includes rave_tile_graphics, '<anchor ref="east"'
      refute File.exist?(File.join(dir, 'plugins/ravenoc/pages.json'))
    end
  end

  def test_rejects_extension_kind_outside_noc
    Dir.mktmpdir do |dir|
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', ravenoc_extension_yaml.sub('kind: noc', 'kind: ip'))
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate_extension(
          extension_path: extension_path,
          views_dir: File.join(dir, 'spec/noc/views'),
          bundle_dir: File.join(dir, 'plugins/ravenoc')
        )
      end
      assert_match(/kind must be noc/, error.message)
    end
  end

  def test_rejects_extension_view_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', ravenoc_extension_yaml)
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml.sub('ref="east"', 'ref="debug"'))
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate_extension(
          extension_path: extension_path,
          views_dir: File.join(dir, 'spec/noc/views'),
          bundle_dir: File.join(dir, 'plugins/ravenoc')
        )
      end
      assert_match(/view RaveTile references unknown interface debug/, error.message)
    end
  end

  def test_cli_generates_extension_bundle
    Dir.mktmpdir do |dir|
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', ravenoc_extension_yaml)
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--extension', extension_path,
        '--views', File.join(dir, 'spec/noc/views'),
        '--bundle', File.join(dir, 'plugins/ravenoc')
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated extension bundle'
      assert File.file?(File.join(dir, 'plugins/ravenoc/plugin.json'))
      assert File.file?(File.join(dir, 'plugins/ravenoc/modules.xml'))
      assert File.file?(File.join(dir, 'plugins/ravenoc/graphics/RaveTile.xml'))
      assert File.file?(File.join(dir, 'plugins/ravenoc/graphics/RaveEndpoint.xml'))
      refute File.exist?(File.join(dir, 'plugins/ravenoc/pages.json'))
    end
  end

  private

  def write_file(root, relative_path, content)
    path = File.join(root, relative_path)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, content)
    path
  end

  def ravenoc_extension_yaml
    <<~YAML
      schema: finepaper.extension.v1
      kind: noc
      extension:
        id: finepaper.ravenoc
        name: RaveNoC
        version: '1.0'
      runtime:
        generator:
          command: ruby
          input_format: generic_graph_v1
          args:
            - generator/bin/generate
            - -i
            - "{input}"
            - -o
            - "{output}"
            - -t
            - generator/template
        drc:
          command: ruby
          input_format: generic_graph_v1
          args:
            - generator/bin/drc
            - -i
            - "{input}"
      topology_presets:
        - id: mesh
          label: Mesh
          kind: mesh
          router_module: RaveTile
          id_pattern: rave_{row}_{col}
          ports: { east: east, west: west, north: north, south: south }
          parameters:
            rows: { label: Rows, default: 2, min: 1, max: 16 }
            cols: { label: Columns, default: 2, min: 1, max: 16 }
      modules:
        RaveTile:
          palette_label: Rave Tile
          graph_group: xps
          description: Editable RaveNoC router tile backed by upstream RTL mesh.
          identity:
            external_id_prefix: rave
            display_prefix: Rave
            width: 2
            supports_mesh_coordinates: true
          parameters:
            display_name: { type: string, default: Rave Tile, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: rave_00, label: External ID, description: Stable generated artifact identifier. }
            x: { type: int, default: 0, configurable: false, description: Canvas X position. }
            y: { type: int, default: 0, configurable: false, description: Canvas Y position. }
            mesh_col: { type: int, default: 0, configurable: false, description: Logical RaveNoC mesh column. }
            mesh_row: { type: int, default: 0, configurable: false, description: Logical RaveNoC mesh row. }
            flit_data_width: { type: int, default: 32, min: 8, max: 512, label: Flit data width, description: FLIT_DATA_WIDTH macro value. }
            flit_type_width: { type: int, default: 2, min: 1, max: 8, label: Flit type width, description: FLIT_TP_WIDTH macro value. }
            flit_buffer_depth: { type: int, default: 2, min: 1, max: 1024, label: Flit buffer depth, description: FLIT_BUFF macro value; must be a power of two. }
            virtual_channels: { type: int, default: 3, min: 1, max: 16, label: Virtual channels, description: N_VIRT_CHN macro value. }
            routing_algorithm: { type: string, enum: [xy, yx], labels: { xy: XY, yx: YX }, default: xy, label: Routing algorithm, description: ROUTING_ALG macro value. }
            priority: { type: string, enum: [zero_high, zero_low], labels: { zero_high: Zero High, zero_low: Zero Low }, default: zero_high, label: Priority, description: H_PRIORITY macro value. }
            max_packet_flits: { type: int, default: 256, min: 1, max: 4096, label: Max packet flits, description: MAX_SZ_PKT macro value. }
            axi_addr_width: { type: int, default: 32, min: 8, max: 64, label: AXI address width, description: AXI_ADDR_WIDTH macro value. }
            axi_data_width: { type: int, default: 32, min: 8, max: 512, label: AXI data width, description: AXI_DATA_WIDTH macro value. }
            axi_cdc_required: { type: string, default: all, label: AXI CDC required, description: 'AXI_CDC_REQ policy: all, none, or bit mask.' }
            bypass_cdc: { type: bool, default: false, label: Bypass CDC in smoke, description: Drive generated bypass_cdc vector high in the smoke wrapper. }
          interfaces:
            north:
              label: North
              bus: ravenoc_router_link
              role: target
              connects_to: initiator
              match: []
              port: { id: north, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: North, description: North RaveNoC router link }
            east:
              label: East
              bus: ravenoc_router_link
              role: initiator
              connects_to: target
              match: []
              port: { id: east, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: East, description: East RaveNoC router link }
            south:
              label: South
              bus: ravenoc_router_link
              role: initiator
              connects_to: target
              match: []
              port: { id: south, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: South, description: South RaveNoC router link }
            west:
              label: West
              bus: ravenoc_router_link
              role: target
              connects_to: initiator
              match: []
              port: { id: west, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: West, description: West RaveNoC router link }
            local:
              label: Local
              bus: ravenoc_endpoint_link
              role: target
              connects_to: initiator
              match: []
              port: { id: local, direction: input, type: bus, bus_type: ravenoc_endpoint_link, role: attachment, name: Local, description: Local RaveNoC endpoint slot }
        RaveEndpoint:
          palette_label: Rave Endpoint
          graph_group: endpoints
          description: Editable RaveNoC endpoint attachment for a router tile.
          identity:
            external_id_prefix: rave_ep
            display_prefix: Rave EP
            width: 2
            supports_mesh_coordinates: false
          parameters:
            display_name: { type: string, default: Rave Endpoint, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: rave_ep_00, label: External ID, description: Stable generated artifact identifier. }
            x: { type: int, default: 0, configurable: false, description: Canvas X position. }
            y: { type: int, default: 0, configurable: false, description: Canvas Y position. }
            endpoint_index: { type: int, default: 0, min: 0, max: 255, label: Endpoint index, description: Stable endpoint index in the generated AXI arrays. }
          interfaces:
            noc:
              label: NoC
              bus: ravenoc_endpoint_link
              role: initiator
              connects_to: target
              match: []
              port: { id: noc, direction: output, type: bus, bus_type: ravenoc_endpoint_link, role: attachment, name: NoC, description: RaveNoC endpoint attachment }
    YAML
  end

  def rave_tile_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="RaveTile">
        <graphics layout="mesh_router" node_color="#8fb7e8" supports_collapse="true">
          <expanded min_width="136" height="116" caption_left="20" caption_top="6" port_inset="18" />
          <collapsed min_width="104" height="92" caption_left="20" caption_top="26" endpoint_inset="18" />
          <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
        </graphics>
        <anchors>
          <anchor ref="local" x="0" y="58" normal_x="-1" normal_y="0" label="Local" label_x="32" label_y="58" />
          <anchor ref="north" x="68" y="0" normal_x="0" normal_y="-1" label="North" label_x="68" label_y="34" />
          <anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />
          <anchor ref="south" x="68" y="116" normal_x="0" normal_y="1" label="South" label_x="68" label_y="98" />
          <anchor ref="west" x="0" y="92" normal_x="-1" normal_y="0" label="West" label_x="24" label_y="92" />
        </anchors>
      </module-view>
    XML
  end

  def rave_endpoint_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="RaveEndpoint">
        <graphics layout="endpoint" node_color="#d9e9a7">
          <expanded min_width="112" height="54" caption_left="8" caption_top="6" />
          <arrangement loose_endpoint_spacing_x="168"
                       loose_endpoint_spacing_y="84"
                       loose_endpoint_margin_y="116" />
        </graphics>
        <anchors>
          <anchor ref="noc" x="112" y="27" normal_x="1" normal_y="0" label="NoC" label_x="84" label_y="40" />
        </anchors>
      </module-view>
    XML
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

  def renamed_interface_anchor_spec_yaml
    <<~YAML
      schema: v1
      kind: noc-definition
      name: NoC
      version: '1.0'
      buses:
        router_link:
          description: Router-to-router NoC link.
          compatibility:
            roles:
              initiator: [target]
              target: [initiator]
            match: []
          config: {}
          signals:
            - name: flit
              direction: initiator_to_target
              width: FLIT_WIDTH
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
      modules:
        RouterTile:
          palette_label: Router Tile
          graph_group: xps
          description: Spec-named NoC router tile.
          identity:
            external_id_prefix: xp
            display_prefix: RT
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
            east:
              label: East
              bus: router_link
              role: initiator
              port: { id: east, direction: output, type: bus, bus_type: router_link, role: router, name: East, description: East router interface }
            west:
              label: West
              bus: router_link
              role: target
              port: { id: west, direction: input, type: bus, bus_type: router_link, role: router, name: West, description: West router interface }
            local0:
              label: Local 0
              bus: ni_link
              role: target
              accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }
              port: { id: local0, direction: input, type: bus, bus_type: ni_link, role: attachment, name: Local 0, description: Local endpoint slot 0 }
        NetworkPort:
          palette_label: Network Port
          graph_group: endpoints
          description: Spec-named NoC endpoint.
          identity:
            external_id_prefix: ep
            display_prefix: NP
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
              label: NoC
              bus: ni_link
              role: initiator
              config:
                protocol: { parameter: protocol }
                data_width: { parameter: data_width }
              port: { id: noc, direction: output, type: bus, bus_type: ni_link, role: attachment, name: NoC, description: NoC attachment interface }
    YAML
  end

  def router_tile_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="RouterTile">
        <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
          <expanded min_width="136" height="116" caption_left="30" caption_top="6" port_inset="16" />
          <collapsed min_width="104" height="92" caption_left="30" caption_top="26" endpoint_inset="18" />
          <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
        </graphics>
        <anchors>
          <anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />
          <anchor ref="west" x="0" y="58" normal_x="-1" normal_y="0" label="West" label_x="24" label_y="58" />
          <anchor ref="local0" x="0" y="30" normal_x="-1" normal_y="0" label="Local 0" label_x="30" label_y="30" />
        </anchors>
      </module-view>
    XML
  end

  def network_port_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="NetworkPort">
        <graphics layout="endpoint" node_color="#d6f4b6">
          <expanded min_width="104" height="54" caption_left="8" caption_top="6" />
          <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
        </graphics>
        <anchors>
          <anchor ref="noc" x="104" y="27" normal_x="1" normal_y="0" label="NoC" label_x="78" label_y="27" />
        </anchors>
      </module-view>
    XML
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
