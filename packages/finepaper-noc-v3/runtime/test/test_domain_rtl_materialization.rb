# frozen_string_literal: true

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
require_relative 'domain_rtl_fixture'
require_relative '../lib/domain_rtl_context'

class TestV3DomainRtlMaterialization < Minitest::Test
  LEGACY_ROOT = File.expand_path('../legacy-generator', __dir__)
  TEMPLATE_DIR = File.join(LEGACY_ROOT, 'template')
  PARAMETERS = {
    'data_width' => 64,
    'flit_width' => 128,
    'addr_width' => 32
  }.freeze

  def test_router_link_crossing_materializes_two_oriented_fifos
    endpoints = [
      Endpoint.new('ep_left', 'master', 'axi4', 64),
      Endpoint.new('ep_right', 'slave', 'axi4', 64)
    ]
    xps = [
      Xp.new('xp_left', 0, 0, ['ep_left']),
      Xp.new('xp_right', 1, 0, ['ep_right'])
    ]
    connections = [Connection.new('xp_left', 'xp_right', 'east')]
    timing = {
      ['router', 'r-0-0'] => 'clock-left',
      ['endpoint', 'ep_left'] => 'clock-left',
      ['router', 'r-1-0'] => 'clock-right',
      ['endpoint', 'ep_right'] => 'clock-right'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'router_cdc',
      router_ids: %w[r-0-0 r-1-0],
      endpoint_routers: {'ep_left' => 'r-0-0', 'ep_right' => 'r-1-0'},
      router_links: [{
        id: 'link-r-0-0--r-1-0', from: 'r-0-0', to: 'r-1-0'
      }],
      timing_by_element: timing,
      reset_stages: {'clock-left' => 2, 'clock-right' => 5},
      fifo_depth: 8,
      synchronizer_stages: 4
    )
    context = FinepaperNoc::DomainRtlContext.new(
      DomainRtlFixture.deep_copy(plan)
    )

    with_generated_noc('router_cdc', xps, connections, endpoints, plan) do |dir, top|
      left = clock_domain(context, 'clock-left')
      right = clock_domain(context, 'clock-right')
      assert_includes top, '//  - port_mode=domain-token'
      assert_includes top, "input  logic clk_#{left.fetch('token')}"
      assert_includes top, "input  logic clk_#{right.fetch('token')}"
      assert_equal 2, top.scan(/fp_reset_synchronizer #\(/).size
      assert_equal 2, top.scan(/fp_async_ready_valid_fifo #\(/).size

      forward = parameterized_instance_body(
        top, 'fp_async_ready_valid_fifo',
        'u_cdc_link_xp_left_to_xp_right'
      )
      reverse = parameterized_instance_body(
        top, 'fp_async_ready_valid_fifo',
        'u_cdc_link_xp_right_to_xp_left'
      )
      assert_includes forward, '.DEPTH(8)'
      assert_includes forward, '.SYNC_STAGES(4)'
      assert_includes forward, ".src_clk_i(clk_#{left.fetch('token')})"
      assert_includes forward, ".dst_clk_i(clk_#{right.fetch('token')})"
      assert_includes reverse, ".src_clk_i(clk_#{right.fetch('token')})"
      assert_includes reverse, ".dst_clk_i(clk_#{left.fetch('token')})"
      refute_includes top, 'u_cdc_ni_ep_left_to_router'
      refute_includes top, 'u_cdc_router_to_ni_ep_right'

      assert_instance_domain(top, 'u_xp_left', left)
      assert_instance_domain(top, 'u_xp_right', right)
      assert_instance_domain(top, 'u_ni_ep_left', left)
      assert_instance_domain(top, 'u_ni_ep_right', right)
      assert_verilator_lint(dir)
      assert_generated_cdc_simulation(
        dir,
        top_module: 'router_cdc_top',
        left_endpoint: 'ep_left',
        right_endpoint: 'ep_right',
        left_clock_port: "clk_#{left.fetch('token')}",
        right_clock_port: "clk_#{right.fetch('token')}"
      )
    end
  end

  def test_endpoint_attachment_crossing_uses_per_endpoint_ni_domains
    endpoints = [
      Endpoint.new(
        'ep_local', 'master', 'axi4', 64,
        buffer_depth: 16, qos_enabled: false
      ),
      Endpoint.new(
        'ep_remote', 'master', 'axi4', 64,
        buffer_depth: 32, qos_enabled: true
      )
    ]
    xps = [Xp.new('xp_only', 0, 0, %w[ep_local ep_remote])]
    timing = {
      ['router', 'r-0-0'] => 'clock-router',
      ['endpoint', 'ep_local'] => 'clock-router',
      ['endpoint', 'ep_remote'] => 'clock-remote'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'attachment_cdc',
      router_ids: ['r-0-0'],
      endpoint_routers: {
        'ep_local' => 'r-0-0', 'ep_remote' => 'r-0-0'
      },
      router_links: [],
      timing_by_element: timing
    )
    context = FinepaperNoc::DomainRtlContext.new(
      DomainRtlFixture.deep_copy(plan)
    )

    with_generated_noc('attachment_cdc', xps, [], endpoints, plan) do |dir, top|
      router = clock_domain(context, 'clock-router')
      remote = clock_domain(context, 'clock-remote')
      assert_equal 2, top.scan(/fp_async_ready_valid_fifo #\(/).size
      assert_includes top, 'u_cdc_ni_ep_remote_to_router'
      assert_includes top, 'u_cdc_router_to_ni_ep_remote'
      refute_includes top, 'u_cdc_ni_ep_local_to_router'
      refute_includes top, 'u_cdc_router_to_ni_ep_local'

      assert_instance_domain(top, 'u_xp_only', router)
      assert_instance_domain(top, 'u_ni_ep_local', router)
      assert_instance_domain(top, 'u_ni_ep_remote', remote)
      ni_files = Dir[File.join(dir, 'ni_*', '*.v')].map do |path|
        File.basename(path)
      end
      assert ni_files.any? { |name| name.include?('buf16_q0') }
      assert ni_files.any? { |name| name.include?('buf32_q1') }
      assert_equal 2, ni_files.size
      assert_verilator_lint(dir)
      assert_generated_cdc_simulation(
        dir,
        top_module: 'attachment_cdc_top',
        left_endpoint: 'ep_local',
        right_endpoint: 'ep_remote',
        left_clock_port: "clk_#{router.fetch('token')}",
        right_clock_port: "clk_#{remote.fetch('token')}"
      )
    end
  end

  def test_legacy_graph_direction_mismatch_fails_closed
    endpoints = [Endpoint.new('ep0', 'master', 'axi4', 64)]
    xps = [
      Xp.new('xp_left', 0, 0, ['ep0']),
      Xp.new('xp_right', 1, 0, [])
    ]
    timing = {
      ['router', 'r-0-0'] => 'clock-test',
      ['router', 'r-1-0'] => 'clock-test',
      ['endpoint', 'ep0'] => 'clock-test'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'edge_mismatch',
      router_ids: %w[r-0-0 r-1-0],
      endpoint_routers: {'ep0' => 'r-0-0'},
      router_links: [{
        id: 'link-r-0-0--r-1-0', from: 'r-0-0', to: 'r-1-0'
      }],
      timing_by_element: timing
    )
    edge = plan.fetch('edgeBindings').find do |entry|
      entry.dig('edge', 'kind') == 'router-link'
    end
    edge['fromElement'], edge['toElement'] =
      edge.fetch('toElement'), edge.fetch('fromElement')
    edge['fromBindings'], edge['toBindings'] =
      edge.fetch('toBindings'), edge.fetch('fromBindings')
    noc = NocConfig.new(
      'edge_mismatch', '3.0', PARAMETERS, xps,
      [Connection.new('xp_left', 'xp_right', 'east')], endpoints, plan
    )

    Dir.mktmpdir('finepaper-v3-edge-mismatch-') do |dir|
      error = assert_raises(FinepaperNoc::DomainRtlContextError) do
        RtlGenerator.new(noc, TEMPLATE_DIR).generate_partitioned(dir)
      end
      assert_equal 'rtl_context.legacy_edge_mismatch', error.code
    end
  end

  def test_same_signature_endpoints_reuse_one_ni_module_across_domains
    endpoints = %w[ep_a ep_b].map do |id|
      Endpoint.new(id, 'master', 'axi4', 64)
    end
    xps = [Xp.new('xp_only', 0, 0, %w[ep_a ep_b])]
    timing = {
      ['router', 'r-0-0'] => 'clock-router',
      ['endpoint', 'ep_a'] => 'clock-router',
      ['endpoint', 'ep_b'] => 'clock-remote'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'shared_ni_variant',
      router_ids: ['r-0-0'],
      endpoint_routers: {'ep_a' => 'r-0-0', 'ep_b' => 'r-0-0'},
      router_links: [],
      timing_by_element: timing
    )
    context = FinepaperNoc::DomainRtlContext.new(
      DomainRtlFixture.deep_copy(plan)
    )

    with_generated_noc(
      'shared_ni_variant', xps, [], endpoints, plan
    ) do |dir, top|
      ni_files = Dir[File.join(dir, 'ni_*', '*.v')]
      assert_equal 1, ni_files.size
      assert_includes top, ' u_ni_ep_a ('
      assert_includes top, ' u_ni_ep_b ('
      assert_equal 2, top.scan(/ni_bridge_[A-Za-z0-9_]+ #\(/).size
      assert_instance_domain(top, 'u_ni_ep_a', clock_domain(context, 'clock-router'))
      assert_instance_domain(top, 'u_ni_ep_b', clock_domain(context, 'clock-remote'))
      assert_equal 2, top.scan(/fp_async_ready_valid_fifo #\(/).size
      assert_verilator_lint(dir)
    end
  end

  def test_invalid_reset_release_depth_fails_closed_at_renderer_boundary
    endpoints = [Endpoint.new('ep0', 'master', 'axi4', 64)]
    xps = [Xp.new('xp0', 0, 0, ['ep0'])]
    timing = {
      ['router', 'r-0-0'] => 'clock-invalid',
      ['endpoint', 'ep0'] => 'clock-invalid'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'invalid_reset_release',
      router_ids: ['r-0-0'],
      endpoint_routers: {'ep0' => 'r-0-0'},
      router_links: [],
      timing_by_element: timing,
      reset_stages: {'clock-invalid' => 1}
    )
    noc = NocConfig.new(
      'invalid_reset_release', '3.0', PARAMETERS, xps, [], endpoints, plan
    )

    Dir.mktmpdir('finepaper-v3-invalid-reset-release-') do |dir|
      error = assert_raises(FinepaperNoc::DomainRtlContextError) do
        RtlGenerator.new(noc, TEMPLATE_DIR).generate_partitioned(dir)
      end
      assert_equal 'rtl_context.invalid_reset_release_stages', error.code
    end
  end

  private

  def with_generated_noc(name, xps, connections, endpoints, plan)
    noc = NocConfig.new(
      name, '3.0', PARAMETERS, xps, connections, endpoints, plan
    )
    Dir.mktmpdir("finepaper-v3-#{name}-") do |dir|
      RtlGenerator.new(noc, TEMPLATE_DIR).generate_partitioned(dir)
      top = File.read(File.join(dir, "#{name}_top.v"))
      yield dir, top
    end
  end

  def clock_domain(context, id)
    context.domains.fetch(id)
  end

  def instance_body(top, instance)
    start = top.index(" #{instance} (")
    refute_nil start, "missing RTL instance #{instance}"
    finish = top.index("\n  );", start)
    refute_nil finish, "unterminated RTL instance #{instance}"
    top[start...finish]
  end

  def parameterized_instance_body(top, module_name, instance)
    instance_start = top.index(" #{instance} (")
    refute_nil instance_start, "missing RTL instance #{instance}"
    start = top.rindex("#{module_name} #(", instance_start)
    refute_nil start, "missing parameter block for RTL instance #{instance}"
    finish = top.index("\n  );", instance_start)
    refute_nil finish, "unterminated RTL instance #{instance}"
    top[start...finish]
  end

  def assert_instance_domain(top, instance, domain)
    body = instance_body(top, instance)
    assert_includes body, ".clk(clk_#{domain.fetch('token')})"
    assert_includes body, ".rst_n(rst_n_#{domain.fetch('token')})"
  end

  def assert_verilator_lint(dir)
    skip 'verilator not found' unless system(
      'verilator', '--version', out: File::NULL, err: File::NULL
    )

    stdout, stderr, status = Open3.capture3(
      'verilator', '--lint-only', '--sv', '-f', File.join(dir, 'filelist.f')
    )
    assert status.success?, "Verilator lint failed:\n#{stdout}#{stderr}"
  end

  def assert_generated_cdc_simulation(dir, top_module:, left_endpoint:,
                                      right_endpoint:, left_clock_port:,
                                      right_clock_port:)
    skip 'verilator not found' unless system(
      'verilator', '--version', out: File::NULL, err: File::NULL
    )

    testbench = File.join(dir, 'generated_cdc_tb.sv')
    File.write(
      testbench,
      generated_cdc_testbench(
        top_module, left_endpoint, right_endpoint,
        left_clock_port, right_clock_port
      )
    )
    build_dir = File.join(dir, 'simulation')
    stdout, stderr, status = Open3.capture3(
      {'CCACHE_DISABLE' => '1'},
      'verilator', '--binary', '--timing', '--assert', '--sv',
      '--timescale', '1ns/1ps',
      '--top-module', 'generated_cdc_tb',
      '--Mdir', build_dir,
      '-o', 'generated_cdc_sim',
      '-f', File.join(dir, 'filelist.f'), testbench
    )
    assert status.success?, "Verilator simulation build failed:\n#{stdout}#{stderr}"

    stdout, stderr, status = Open3.capture3(
      File.join(build_dir, 'generated_cdc_sim')
    )
    output = stdout + stderr
    assert status.success?, "generated CDC simulation failed:\n#{output}"
    assert_includes output, 'PASS generated CDC both directions'
  end

  def generated_cdc_testbench(top_module, left_endpoint, right_endpoint,
                              left_clock_port, right_clock_port)
    <<~SYSTEM_VERILOG
      module generated_cdc_tb;
        localparam int unsigned FLIT_WIDTH = 128;
        logic router_clk = 1'b0;
        logic remote_clk = 1'b0;
        logic rst_n = 1'b0;

        logic [FLIT_WIDTH-1:0] ep_local_flit_in = '0;
        logic ep_local_flit_in_valid = 1'b0;
        logic ep_local_flit_in_ready;
        logic [FLIT_WIDTH-1:0] ep_local_flit_out;
        logic ep_local_flit_out_valid;
        logic ep_local_flit_out_ready = 1'b0;

        logic [FLIT_WIDTH-1:0] ep_remote_flit_in = '0;
        logic ep_remote_flit_in_valid = 1'b0;
        logic ep_remote_flit_in_ready;
        logic [FLIT_WIDTH-1:0] ep_remote_flit_out;
        logic ep_remote_flit_out_valid;
        logic ep_remote_flit_out_ready = 1'b0;

        #{top_module} dut (
          .#{left_clock_port}(router_clk),
          .#{right_clock_port}(remote_clk),
          .rst_n(rst_n),
          .#{left_endpoint}_flit_in(ep_local_flit_in),
          .#{left_endpoint}_flit_in_valid(ep_local_flit_in_valid),
          .#{left_endpoint}_flit_in_ready(ep_local_flit_in_ready),
          .#{left_endpoint}_flit_out(ep_local_flit_out),
          .#{left_endpoint}_flit_out_valid(ep_local_flit_out_valid),
          .#{left_endpoint}_flit_out_ready(ep_local_flit_out_ready),
          .#{right_endpoint}_flit_in(ep_remote_flit_in),
          .#{right_endpoint}_flit_in_valid(ep_remote_flit_in_valid),
          .#{right_endpoint}_flit_in_ready(ep_remote_flit_in_ready),
          .#{right_endpoint}_flit_out(ep_remote_flit_out),
          .#{right_endpoint}_flit_out_valid(ep_remote_flit_out_valid),
          .#{right_endpoint}_flit_out_ready(ep_remote_flit_out_ready)
        );

        initial forever #3 router_clk = ~router_clk;
        initial begin
          #1;
          forever #5 remote_clk = ~remote_clk;
        end

        task automatic send_local(input logic [FLIT_WIDTH-1:0] payload);
          @(negedge router_clk);
          ep_local_flit_in = payload;
          ep_local_flit_in_valid = 1'b1;
          do @(posedge router_clk); while (!ep_local_flit_in_ready);
          @(negedge router_clk);
          ep_local_flit_in_valid = 1'b0;
        endtask

        task automatic receive_remote(input logic [FLIT_WIDTH-1:0] expected);
          repeat (4) @(posedge remote_clk);
          @(negedge remote_clk);
          ep_remote_flit_out_ready = 1'b1;
          do @(posedge remote_clk); while (!ep_remote_flit_out_valid);
          if (ep_remote_flit_out !== expected)
            $fatal(1, "local-to-remote payload mismatch");
          @(negedge remote_clk);
          ep_remote_flit_out_ready = 1'b0;
        endtask

        task automatic send_remote(input logic [FLIT_WIDTH-1:0] payload);
          @(negedge remote_clk);
          ep_remote_flit_in = payload;
          ep_remote_flit_in_valid = 1'b1;
          do @(posedge remote_clk); while (!ep_remote_flit_in_ready);
          @(negedge remote_clk);
          ep_remote_flit_in_valid = 1'b0;
        endtask

        task automatic receive_local(input logic [FLIT_WIDTH-1:0] expected);
          repeat (4) @(posedge router_clk);
          @(negedge router_clk);
          ep_local_flit_out_ready = 1'b1;
          do @(posedge router_clk); while (!ep_local_flit_out_valid);
          if (ep_local_flit_out !== expected)
            $fatal(1, "remote-to-local payload mismatch");
          @(negedge router_clk);
          ep_local_flit_out_ready = 1'b0;
        endtask

        initial begin
          #17 rst_n = 1'b1;
          repeat (8) @(posedge router_clk);
          fork
            send_local(128'h0123_4567_89ab_cdef_fedc_ba98_7654_3210);
            receive_remote(128'h0123_4567_89ab_cdef_fedc_ba98_7654_3210);
          join
          fork
            send_remote(128'h55aa_0ff0_c33c_a55a_1122_3344_5566_7788);
            receive_local(128'h55aa_0ff0_c33c_a55a_1122_3344_5566_7788);
          join
          $display("PASS generated CDC both directions");
          $finish;
        end

        initial begin
          #20000;
          $fatal(1, "timeout waiting for generated CDC traffic");
        end
      endmodule
    SYSTEM_VERILOG
  end
end
