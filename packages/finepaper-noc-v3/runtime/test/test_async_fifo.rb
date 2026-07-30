# Independent regression for the V3 async ready/valid FIFO primitive.
require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'

class TestV3AsyncReadyValidFifo < Minitest::Test
  RUNTIME_ROOT = File.expand_path('..', __dir__)
  FIFO_PATH = File.join(
    RUNTIME_ROOT,
    'legacy-generator',
    'template',
    'stubs',
    'fp_async_ready_valid_fifo.sv'
  )
  RESET_PATH = File.join(
    RUNTIME_ROOT,
    'legacy-generator',
    'template',
    'stubs',
    'fp_reset_synchronizer.sv'
  )

  def setup
    @fifo_source = File.read(FIFO_PATH)
  end

  def test_structure_is_gray_pointer_cdc_not_payload_bit_synchronization
    assert_includes @fifo_source, 'The multi-bit payload never passes through per-bit synchronizers'
    assert_equal 2, @fifo_source.scan(/\(\* async_reg = "true" \*\)/).size
    assert_match(/storage_q \[0:EFFECTIVE_DEPTH-1\]/, @fifo_source)
    assert_match(/src_gray_next = \(src_binary_next >> 1\) \^ src_binary_next;/,
                 @fifo_source)
    assert_match(/dst_gray_next = \(dst_binary_next >> 1\) \^ dst_binary_next;/,
                 @fifo_source)
    assert_match(/dst_gray_full_compare\[POINTER_WIDTH-1 -: 2\]/,
                 @fifo_source)
    assert_match(/storage_q\[src_binary_q\[ADDRESS_WIDTH-1:0\]\] <= src_payload_i;/,
                 @fifo_source)
    assert_match(/storage_q\[dst_binary_q\[ADDRESS_WIDTH-1:0\]\]/,
                 @fifo_source)
  end

  def test_parameter_guards_are_strict
    assert_includes @fifo_source, 'EFFECTIVE_DEPTH = DEPTH_VALID ? DEPTH : 2'
    assert_match(
      /EFFECTIVE_PAYLOAD_WIDTH\s*=\s*PAYLOAD_WIDTH > 0 \? PAYLOAD_WIDTH : 1/,
      @fifo_source
    )
    assert_match(
      /EFFECTIVE_SYNC_STAGES\s*=\s*SYNC_STAGES_VALID \? SYNC_STAGES : 2/,
      @fifo_source
    )
    assert_includes @fifo_source,
                    'DEPTH must be a power of two in the inclusive range 2..1024'
    assert_includes @fifo_source, 'SYNC_STAGES must be in the inclusive range 2..8'
  end

  def test_lints_at_supported_parameter_boundaries
    skip 'verilator not found' unless verilator_available?

    stdout, stderr, status = Open3.capture3(
      'verilator', '--lint-only', '--sv',
      '--top-module', 'fp_async_ready_valid_fifo',
      '-GPAYLOAD_WIDTH=1', '-GDEPTH=1024', '-GSYNC_STAGES=8',
      FIFO_PATH
    )
    assert status.success?, "Verilator lint failed:\n#{stdout}#{stderr}"
  end

  def test_invalid_parameters_fail_closed
    skip 'verilator not found' unless verilator_available?

    [
      {width: 0, depth: 4, stages: 2, message: 'PAYLOAD_WIDTH must be positive'},
      {width: 8, depth: 0, stages: 2, message: 'DEPTH must be a power of two in the inclusive range 2..1024'},
      {width: 8, depth: 1, stages: 2, message: 'DEPTH must be a power of two in the inclusive range 2..1024'},
      {width: 8, depth: 3, stages: 2, message: 'DEPTH must be a power of two in the inclusive range 2..1024'},
      {width: 8, depth: 2048, stages: 2, message: 'DEPTH must be a power of two in the inclusive range 2..1024'},
      {width: 8, depth: 4, stages: 0, message: 'SYNC_STAGES must be in the inclusive range 2..8'},
      {width: 8, depth: 4, stages: 1, message: 'SYNC_STAGES must be in the inclusive range 2..8'},
      {width: 8, depth: 4, stages: 9, message: 'SYNC_STAGES must be in the inclusive range 2..8'}
    ].each do |configuration|
      output, status = compile_and_run_invalid_configuration(
        width: configuration.fetch(:width),
        depth: configuration.fetch(:depth),
        stages: configuration.fetch(:stages)
      )
      refute status.success?, "invalid FIFO parameters unexpectedly ran successfully:\n#{output}"
      assert_includes output, configuration.fetch(:message)
    end
  end

  def test_randomized_offset_clocks_preserve_order_and_backpressure
    skip 'verilator not found' unless verilator_available?

    Dir.mktmpdir('finepaper-v3-async-fifo-') do |directory|
      testbench = File.join(directory, 'async_fifo_tb.sv')
      File.write(testbench, randomized_testbench)
      build_dir = File.join(directory, 'obj_dir')

      stdout, stderr, status = Open3.capture3(
        {'CCACHE_DISABLE' => '1'},
        'verilator', '--binary', '--timing', '--assert', '--sv',
        '--timescale', '1ns/1ps',
        '--top-module', 'async_fifo_tb',
        '--Mdir', build_dir,
        '-o', 'async_fifo_sim',
        RESET_PATH, FIFO_PATH, testbench
      )
      assert status.success?, "Verilator build failed:\n#{stdout}#{stderr}"

      stdout, stderr, status = Open3.capture3(File.join(build_dir, 'async_fifo_sim'))
      output = stdout + stderr
      assert status.success?, "async FIFO simulation failed:\n#{output}"
      assert_match(/PASS transactions=257 source_backpressure=\d+ destination_stalls=\d+/,
                   output)
    end
  end

  private

  def verilator_available?
    system('verilator', '--version', out: File::NULL, err: File::NULL)
  end

  def compile_and_run_invalid_configuration(width:, depth:, stages:)
    Dir.mktmpdir('finepaper-v3-invalid-async-fifo-') do |directory|
      testbench = File.join(directory, 'invalid_fifo_tb.sv')
      File.write(testbench, invalid_parameter_testbench(width:, depth:, stages:))
      build_dir = File.join(directory, 'obj_dir')
      stdout, stderr, status = Open3.capture3(
        {'CCACHE_DISABLE' => '1'},
        'verilator', '--binary', '--timing', '--sv',
        '--timescale', '1ns/1ps',
        '--top-module', 'invalid_fifo_tb',
        '--Mdir', build_dir,
        '-o', 'invalid_fifo_sim',
        FIFO_PATH, testbench
      )
      return [stdout + stderr, status] unless status.success?

      run_stdout, run_stderr, run_status = Open3.capture3(
        File.join(build_dir, 'invalid_fifo_sim')
      )
      [run_stdout + run_stderr, run_status]
    end
  end

  def invalid_parameter_testbench(width:, depth:, stages:)
    <<~SYSTEM_VERILOG
      module invalid_fifo_tb;
        localparam int unsigned PAYLOAD_WIDTH = #{width};
        logic clk = 1'b0;
        logic ready;
        logic [(PAYLOAD_WIDTH > 0 ? PAYLOAD_WIDTH : 1)-1:0] payload;
        logic valid;

        always #1 clk = ~clk;

        fp_async_ready_valid_fifo #(
          .PAYLOAD_WIDTH(PAYLOAD_WIDTH),
          .DEPTH(#{depth}),
          .SYNC_STAGES(#{stages})
        ) dut (
          .src_clk_i(clk),
          .src_rst_ni(1'b0),
          .src_payload_i('0),
          .src_valid_i(1'b0),
          .src_ready_o(ready),
          .dst_clk_i(clk),
          .dst_rst_ni(1'b0),
          .dst_payload_o(payload),
          .dst_valid_o(valid),
          .dst_ready_i(1'b0)
        );

        initial begin
          #10;
          $fatal(1, "invalid parameter guard did not fire");
        end
      endmodule
    SYSTEM_VERILOG
  end

  def randomized_testbench
    <<~'SYSTEM_VERILOG'
      module async_fifo_tb;
        localparam int unsigned PAYLOAD_WIDTH = 32;
        localparam int unsigned TRANSACTIONS = 257;

        logic src_clk = 1'b0;
        logic src_async_rst_n = 1'b0;
        logic src_rst_n;
        logic [PAYLOAD_WIDTH-1:0] src_payload = '0;
        logic src_valid = 1'b0;
        logic src_ready;

        logic dst_clk = 1'b0;
        logic dst_async_rst_n = 1'b0;
        logic dst_rst_n;
        logic [PAYLOAD_WIDTH-1:0] dst_payload;
        logic dst_valid;
        logic dst_ready = 1'b0;

        int unsigned received_count = 0;
        int unsigned source_backpressure = 0;
        int unsigned destination_stalls = 0;
        int unsigned destination_cycle = 0;
        logic hold_active = 1'b0;
        logic [PAYLOAD_WIDTH-1:0] held_payload = '0;
        logic all_expected_received = 1'b0;

        fp_reset_synchronizer #(.STAGES(2)) src_reset_sync (
          .clk(src_clk),
          .async_reset_n(src_async_rst_n),
          .reset_n(src_rst_n)
        );

        fp_reset_synchronizer #(.STAGES(2)) dst_reset_sync (
          .clk(dst_clk),
          .async_reset_n(dst_async_rst_n),
          .reset_n(dst_rst_n)
        );

        fp_async_ready_valid_fifo #(
          .PAYLOAD_WIDTH(PAYLOAD_WIDTH),
          .DEPTH(8),
          .SYNC_STAGES(3)
        ) dut (
          .src_clk_i(src_clk),
          .src_rst_ni(src_rst_n),
          .src_payload_i(src_payload),
          .src_valid_i(src_valid),
          .src_ready_o(src_ready),
          .dst_clk_i(dst_clk),
          .dst_rst_ni(dst_rst_n),
          .dst_payload_o(dst_payload),
          .dst_valid_o(dst_valid),
          .dst_ready_i(dst_ready)
        );

        function automatic logic [PAYLOAD_WIDTH-1:0] payload_for(
          input int unsigned index
        );
          payload_for = (index * 32'h9e37_79b9) ^ 32'ha5a5_5a5a;
        endfunction

        initial forever #3 src_clk = ~src_clk;
        initial begin
          #1;
          forever #5 dst_clk = ~dst_clk;
        end

        // The shared asynchronous request is released away from both clocks;
        // each domain observes release only through its local synchronizer.
        initial begin
          #17;
          src_async_rst_n = 1'b1;
          dst_async_rst_n = 1'b1;
        end

        initial begin : source_driver
          int unsigned source_seed = 32'h51a7_2026;
          void'($urandom(source_seed));
          wait (src_rst_n);
          for (int unsigned index = 0; index < TRANSACTIONS; index++) begin
            repeat ($urandom_range(0, 3)) @(posedge src_clk);
            @(negedge src_clk);
            src_payload = payload_for(index);
            src_valid = 1'b1;
            do @(posedge src_clk); while (!src_ready);
            @(negedge src_clk);
            src_valid = 1'b0;
          end
        end

        initial begin : destination_driver
          int unsigned destination_seed = 32'hcdc0_2026;
          void'($urandom(destination_seed));
          wait (dst_rst_n);
          forever begin
            @(negedge dst_clk);
            destination_cycle++;
            // Periodic three-cycle stalls guarantee sustained backpressure;
            // the remaining choices are pseudo-random.
            if ((destination_cycle % 13) < 3)
              dst_ready = 1'b0;
            else
              dst_ready = ($urandom_range(0, 3) != 0);
          end
        end

        always @(posedge src_clk) begin
          if (src_rst_n && src_valid && !src_ready)
            source_backpressure++;
        end

        always @(posedge dst_clk) begin : destination_scoreboard
          if (!dst_rst_n) begin
            received_count <= 0;
            destination_stalls <= 0;
            hold_active <= 1'b0;
            held_payload <= '0;
            all_expected_received <= 1'b0;
          end else begin
            if (hold_active) begin
              if (!dst_valid)
                $fatal(1, "valid dropped before a stalled transfer completed");
              if (dst_payload !== held_payload)
                $fatal(1, "payload changed while valid was backpressured");
              if (dst_ready)
                hold_active <= 1'b0;
            end else if (dst_valid && !dst_ready) begin
              hold_active <= 1'b1;
              held_payload <= dst_payload;
            end

            if (dst_valid && !dst_ready)
              destination_stalls <= destination_stalls + 1;

            if (dst_valid && dst_ready) begin
              if (received_count >= TRANSACTIONS)
                $fatal(1, "duplicate transfer after the expected stream");
              if (dst_payload !== payload_for(received_count))
                $fatal(1, "out-of-order or corrupted transfer at index %0d", received_count);
              received_count <= received_count + 1;
              if (received_count + 1 == TRANSACTIONS)
                all_expected_received <= 1'b1;
            end
          end
        end

        initial begin : completion
          wait (all_expected_received);
          repeat (20) @(posedge dst_clk);
          if (dst_valid)
            $fatal(1, "unexpected transfer remained after the expected stream");
          if (source_backpressure == 0)
            $fatal(1, "test never exercised source backpressure");
          if (destination_stalls == 0)
            $fatal(1, "test never exercised destination backpressure");
          $display(
            "PASS transactions=%0d source_backpressure=%0d destination_stalls=%0d",
            received_count, source_backpressure, destination_stalls
          );
          $finish;
        end

        initial begin : watchdog
          #500000;
          $fatal(1, "timeout waiting for async FIFO traffic to complete");
        end
      endmodule
    SYSTEM_VERILOG
  end
end
