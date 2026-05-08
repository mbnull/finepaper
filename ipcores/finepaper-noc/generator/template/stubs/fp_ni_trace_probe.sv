// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_trace_probe #(
  parameter FLIT_WIDTH = 128
) (
  input  logic                  clk,
  input  logic [FLIT_WIDTH-1:0] flit_i,
  output logic                  event_o
);
  assign event_o = clk & (|flit_i);
endmodule
