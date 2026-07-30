# frozen_string_literal: true

require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'

class TestV3ResetSynchronizer < Minitest::Test
  RUNTIME_ROOT = File.expand_path('..', __dir__)
  RESET_PATH = File.join(
    RUNTIME_ROOT,
    'legacy-generator',
    'template',
    'stubs',
    'fp_reset_synchronizer.sv'
  )

  def setup
    @source = File.read(RESET_PATH)
  end

  def test_structure_marks_the_chain_and_fails_closed
    assert_includes @source, '(* async_reg = "true" *)'
    assert_includes @source, 'EFFECTIVE_STAGES = STAGES < 2 ? 2 : STAGES'
    assert_includes @source, '$fatal(1, "fp_reset_synchronizer: STAGES must be at least 2")'
    assert_match(/always_ff @\(posedge clk or negedge async_reset_n\)/, @source)
  end

  def test_supported_stage_boundaries_lint
    skip 'verilator not found' unless verilator_available?

    [2, 8].each do |stages|
      stdout, stderr, status = Open3.capture3(
        'verilator', '--lint-only', '--sv',
        '--top-module', 'fp_reset_synchronizer',
        "-GSTAGES=#{stages}", RESET_PATH
      )
      assert status.success?,
             "reset synchronizer STAGES=#{stages} lint failed:\n#{stdout}#{stderr}"
    end
  end

  def test_invalid_stage_count_fails_at_runtime
    skip 'verilator not found' unless verilator_available?

    output, status = compile_and_run(invalid_testbench)
    refute status.success?, "invalid STAGES unexpectedly ran successfully:\n#{output}"
    assert_includes output, 'STAGES must be at least 2'
  end

  def test_asserts_asynchronously_and_releases_after_local_clock_edges
    skip 'verilator not found' unless verilator_available?

    output, status = compile_and_run(functional_testbench)
    assert status.success?, "reset synchronizer simulation failed:\n#{output}"
    assert_includes output, 'PASS reset synchronizer'
  end

  private

  def verilator_available?
    system('verilator', '--version', out: File::NULL, err: File::NULL)
  end

  def compile_and_run(testbench)
    Dir.mktmpdir('finepaper-v3-reset-sync-') do |directory|
      testbench_path = File.join(directory, 'reset_sync_tb.sv')
      File.write(testbench_path, testbench)
      build_dir = File.join(directory, 'obj_dir')
      stdout, stderr, status = Open3.capture3(
        {'CCACHE_DISABLE' => '1'},
        'verilator', '--binary', '--timing', '--sv',
        '--timescale', '1ns/1ps',
        '--top-module', 'reset_sync_tb',
        '--Mdir', build_dir,
        '-o', 'reset_sync_sim',
        RESET_PATH, testbench_path
      )
      return [stdout + stderr, status] unless status.success?

      run_stdout, run_stderr, run_status = Open3.capture3(
        File.join(build_dir, 'reset_sync_sim')
      )
      [run_stdout + run_stderr, run_status]
    end
  end

  def invalid_testbench
    <<~'SYSTEM_VERILOG'
      module reset_sync_tb;
        logic clk = 1'b0;
        logic reset_n;
        always #1 clk = ~clk;

        fp_reset_synchronizer #(.STAGES(1)) dut (
          .clk(clk),
          .async_reset_n(1'b0),
          .reset_n(reset_n)
        );

        initial begin
          #10;
          $fatal(1, "invalid parameter guard did not fire");
        end
      endmodule
    SYSTEM_VERILOG
  end

  def functional_testbench
    <<~'SYSTEM_VERILOG'
      module reset_sync_tb;
        logic clk = 1'b0;
        logic async_reset_n = 1'b0;
        logic reset_n;
        always #5 clk = ~clk;

        fp_reset_synchronizer #(.STAGES(2)) dut (
          .clk(clk),
          .async_reset_n(async_reset_n),
          .reset_n(reset_n)
        );

        initial begin
          #2;
          if (reset_n !== 1'b0)
            $fatal(1, "reset did not assert asynchronously");
          #5 async_reset_n = 1'b1;
          @(posedge clk); #1;
          if (reset_n !== 1'b0)
            $fatal(1, "reset released before two local clock edges");
          @(posedge clk); #1;
          if (reset_n !== 1'b1)
            $fatal(1, "reset did not release after two local clock edges");
          #2 async_reset_n = 1'b0;
          #1;
          if (reset_n !== 1'b0)
            $fatal(1, "reset assertion waited for a clock edge");
          $display("PASS reset synchronizer");
          $finish;
        end
      endmodule
    SYSTEM_VERILOG
  end
end
