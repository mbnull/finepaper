// Asynchronous-assert, synchronous-deassert reset synchronizer for one clock
// Domain. The incoming reset may assert without a running clock; release is
// observed only after STAGES consecutive local clock edges.
`timescale 1ns/1ps
`default_nettype none

module fp_reset_synchronizer #(
  parameter int unsigned STAGES = 2
) (
  input  logic clk,
  input  logic async_reset_n,
  output logic reset_n
);
  localparam int unsigned EFFECTIVE_STAGES = STAGES < 2 ? 2 : STAGES;

  (* async_reg = "true" *)
  logic [EFFECTIVE_STAGES-1:0] sync_q;

  initial begin
    if (STAGES < 2)
      $fatal(1, "fp_reset_synchronizer: STAGES must be at least 2");
  end

  // Asynchronous assertion prevents state from escaping reset without a
  // clock. Deassertion advances only through this clock Domain.
  always_ff @(posedge clk or negedge async_reset_n) begin
    if (!async_reset_n) begin
      sync_q <= '0;
    end else begin
      sync_q[0] <= 1'b1;
      for (int unsigned stage = 1; stage < EFFECTIVE_STAGES; stage++)
        sync_q[stage] <= sync_q[stage - 1];
    end
  end

  assign reset_n = STAGES < 2 ? 1'b0 : sync_q[EFFECTIVE_STAGES - 1];
endmodule

`default_nettype wire
