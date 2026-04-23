// Static finepaper framework stub. Placeholder only; no router semantics.
module fp_xp_credit_accounting #(
  parameter VC_COUNT = 2,
  parameter BUFFER_DEPTH = 8
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       flit_accept_i,
  output logic [7:0] credit_level_o
);
  assign credit_level_o = rst_n ? BUFFER_DEPTH : 8'h00;
endmodule
