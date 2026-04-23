// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_response_queue #(
  parameter FLIT_WIDTH = 128,
  parameter DEPTH = 16
) (
  input  logic                  clk,
  input  logic                  rst_n,
  input  logic [FLIT_WIDTH-1:0] payload_i,
  input  logic                  valid_i,
  output logic                  ready_o,
  output logic [FLIT_WIDTH-1:0] payload_o,
  output logic                  valid_o,
  input  logic                  ready_i
);
  assign ready_o = rst_n & ready_i;
  assign payload_o = payload_i;
  assign valid_o = valid_i & rst_n;
endmodule
