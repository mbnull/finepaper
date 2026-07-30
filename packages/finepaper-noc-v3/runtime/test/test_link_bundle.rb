# V3-only regression for the legacy backend's executable ready/valid shell.
$LOAD_PATH.unshift File.expand_path('../legacy-generator/src/ruby', __dir__)

require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'
require 'generator/rtl_generator'
require 'model/connection'
require 'model/endpoint'
require 'model/noc_config'
require 'model/xp'

class TestV3LinkBundle < Minitest::Test
  LEGACY_ROOT = File.expand_path('../legacy-generator', __dir__)
  TEMPLATE_DIR = File.join(LEGACY_ROOT, 'template')

  def setup
    parameters = {
      'data_width' => 64,
      'flit_width' => 128,
      'addr_width' => 32
    }
    endpoints = [
      Endpoint.new('ep_left', 'master', 'axi4', 64),
      Endpoint.new('ep_right', 'slave', 'axi4', 64)
    ]
    xps = [
      Xp.new('xp_left', 0, 0, ['ep_left']),
      Xp.new('xp_right', 1, 0, ['ep_right'])
    ]
    noc = NocConfig.new(
      'link_bundle',
      '3.0',
      parameters,
      xps,
      [Connection.new('xp_left', 'xp_right', 'east')],
      endpoints,
      single_domain_plan
    )

    @output_dir = Dir.mktmpdir('finepaper-v3-link-bundle-')
    RtlGenerator.new(noc, TEMPLATE_DIR).generate_partitioned(@output_dir)
    @top = File.read(File.join(@output_dir, 'link_bundle_top.v'))
    @left_xp = File.read(File.join(@output_dir, 'xp_e000', 'link_bundle_xp_e000.v'))
    @ni = File.read(Dir[File.join(@output_dir, 'ni_*', '*.v')].first)
  end

  def teardown
    FileUtils.rm_rf(@output_dir)
  end

  def test_router_links_have_two_complete_ready_valid_directions
    refute_includes @top, 'DOMAIN_IMPLEMENTATION'
    %w[xp_left_to_xp_right xp_right_to_xp_left].each do |direction|
      assert_match(/logic \[FLIT_WIDTH-1:0\] link_#{direction}_flit;/, @top)
      assert_match(/logic\s+link_#{direction}_valid;/, @top)
      assert_match(/logic\s+link_#{direction}_ready;/, @top)
    end

    assert_includes @top, '.flit_out_e(link_xp_left_to_xp_right_flit)'
    assert_includes @top, '.flit_out_e_valid(link_xp_left_to_xp_right_valid)'
    assert_includes @top, '.flit_out_e_ready(link_xp_left_to_xp_right_ready)'
    assert_includes @top, '.flit_in_e(link_xp_right_to_xp_left_flit)'
    assert_includes @top, '.flit_in_e_valid(link_xp_right_to_xp_left_valid)'
    assert_includes @top, '.flit_in_e_ready(link_xp_right_to_xp_left_ready)'
  end

  def test_single_timing_domain_preserves_legacy_clock_port_and_syncs_reset
    assert_includes @top, '//  - port_mode=legacy-single-clock'
    assert_match(/\n  input  logic clk,\n  input  logic rst_n,/, @top)
    refute_match(/\n  input  logic clk_[A-Za-z0-9_]+,/, @top)
    assert_equal 1, @top.scan(/fp_reset_synchronizer #\(/).size
    assert_equal 0, @top.scan(/\.rst_n\(rst_n\)/).size
    assert_match(/\.clk\(clk\),\n    \.rst_n\(rst_n_clock_test_/, @top)
  end

  def test_endpoint_attachment_has_complete_ready_valid_in_both_directions
    assert_includes @top, 'input  logic                  ep_left_flit_in_valid'
    assert_includes @top, 'output logic                  ep_left_flit_in_ready'
    assert_includes @top, 'output logic                  ep_left_flit_out_valid'
    assert_includes @top, 'input  logic                  ep_left_flit_out_ready'

    %w[ni_ep_left_to_router router_to_ni_ep_left].each do |direction|
      assert_match(/logic \[FLIT_WIDTH-1:0\] #{direction}_flit;/, @top)
      assert_match(/logic\s+#{direction}_valid;/, @top)
      assert_match(/logic\s+#{direction}_ready;/, @top)
    end

    assert_includes @top, '.local0_flit_in_valid(ni_ep_left_to_router_valid)'
    assert_includes @top, '.local0_flit_in_ready(ni_ep_left_to_router_ready)'
    assert_includes @top, '.local0_flit_out_valid(router_to_ni_ep_left_valid)'
    assert_includes @top, '.local0_flit_out_ready(router_to_ni_ep_left_ready)'
  end

  def test_xp_uses_one_to_one_registered_mapping_with_backpressure
    assert_includes @left_xp, 'This is deliberately only a forwarding foundation.'
    assert_includes @left_xp, 'It is not a complete'
    assert_includes @left_xp, 'flit_in_e -> local0_flit_out'
    assert_includes @left_xp, 'local0_flit_in -> flit_out_e'

    # Each ingress owns one ready output and each egress has one payload/valid
    # driver. The fixed two-port permutation therefore cannot broadcast a flit.
    assert_equal 1, @left_xp.scan(/assign flit_in_e_ready\s+=/).size
    assert_equal 1, @left_xp.scan(/assign local0_flit_in_ready\s+=/).size
    assert_equal 1, @left_xp.scan(/assign flit_out_e\s+=/).size
    assert_equal 1, @left_xp.scan(/assign local0_flit_out\s+=/).size
    assert_equal 1, @left_xp.scan(/assign flit_out_e_valid\s+=/).size
    assert_equal 1, @left_xp.scan(/assign local0_flit_out_valid\s+=/).size

    assert_match(/assign flit_in_e_ready\s+= rst_n && !forward_0_valid_q;/, @left_xp)
    assert_match(/forward_0_valid_q && local0_flit_out_ready/, @left_xp)
    assert_match(/flit_in_e_valid && flit_in_e_ready/, @left_xp)
    assert_match(/forward_0_payload_q <= flit_in_e;/, @left_xp)
  end

  def test_ni_propagates_valid_and_ready_instead_of_forcing_acceptance
    assert_includes @ni, '.valid_i(ep0_flit_in_valid)'
    assert_includes @ni, '.ready_i(ep0_router_flit_in_ready)'
    assert_includes @ni, '.valid_i(ep0_router_flit_out_valid)'
    assert_includes @ni, '.ready_i(ep0_flit_out_ready)'
    assert_includes @ni, 'assign ep0_router_flit_in_valid = ep0_req_valid;'
    assert_includes @ni, 'assign ep0_flit_in_ready        = ep0_req_ready;'
    assert_includes @ni, 'assign ep0_router_flit_out_ready = ep0_rsp_ready;'
    assert_includes @ni, 'assign ep0_flit_out_valid        = ep0_rsp_valid;'
    refute_includes @ni, '.valid_i(1\'b1)'
    refute_includes @ni, '.ready_i(1\'b1)'
  end

  def test_generated_rtl_passes_verilator_lint
    skip 'verilator not found' unless system('verilator --version > /dev/null 2>&1')

    stdout, stderr, status = Open3.capture3(
      'verilator', '--lint-only', '--sv', '-f', File.join(@output_dir, 'filelist.f')
    )
    assert status.success?, "Verilator lint failed:\n#{stdout}#{stderr}"
  end

  private

  def single_domain_plan
    routers = %w[r-0-0 r-1-0].map { |id| reference('router', id) }
    endpoints = %w[ep_left ep_right].map { |id| reference('endpoint', id) }
    members = routers + endpoints
    timing = binding('timing-domain', 'clock', 'clock-test')
    supply = binding('supply-domain', 'power', 'power-test')
    bindings = [supply, timing]
    entity_bindings = members.map do |element|
      {'element' => element, 'bindings' => deep_copy(bindings)}
    end
    edge_bindings = [
      edge(
        reference('router-link', 'link-r-0-0--r-1-0'),
        routers.fetch(0), routers.fetch(1), bindings
      ),
      edge(
        reference('endpoint-attachment', 'ep_left'),
        routers.fetch(0), endpoints.fetch(0), bindings
      ),
      edge(
        reference('endpoint-attachment', 'ep_right'),
        routers.fetch(1), endpoints.fetch(1), bindings
      )
    ]
    {
      'format' => 'finepaper.noc-domain-implementation-plan',
      'formatVersion' => 1,
      'design' => 'link_bundle',
      'source' => {
        'format' => 'finepaper.noc-domain-constraints', 'formatVersion' => 1
      },
      'realization' => {
        'format' => 'finepaper.noc-domain-realization', 'formatVersion' => 1
      },
      'domainBindings' => [
        domain('clock-test', 'clock', 'timing-domain', members),
        domain('power-test', 'power', 'supply-domain', members)
      ],
      'relationBindings' => [],
      'entityBindings' => entity_bindings,
      'edgeBindings' => edge_bindings
    }
  end

  def domain(id, type, role, members)
    parameters = if role == 'timing-domain'
                   {
                     'reset-release-stages' => {
                       'type' => 'integer',
                       'value' => 2,
                       'source' => {
                         'kind' => 'realization-default',
                         'id' => 'resetReleaseStages'
                       }
                     }
                   }
                 else
                   {}
                 end
    {
      'domain' => id, 'domainType' => type, 'role' => role, 'name' => id,
      'parameters' => parameters, 'members' => deep_copy(members)
    }
  end

  def binding(role, type, id)
    {'role' => role, 'domainType' => type, 'domain' => id}
  end

  def reference(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def edge(edge_reference, from, to, bindings)
    {
      'edge' => edge_reference,
      'fromElement' => from,
      'toElement' => to,
      'fromBindings' => deep_copy(bindings),
      'toBindings' => deep_copy(bindings),
      'stages' => []
    }
  end

  def deep_copy(value)
    Marshal.load(Marshal.dump(value))
  end
end
