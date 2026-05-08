// Static finepaper framework stub. Placeholder only; no router semantics.
module fp_xp_channel_switch #(
  parameter FLIT_WIDTH = 128
) (
  input  logic [FLIT_WIDTH-1:0] flit_i,
  output logic [FLIT_WIDTH-1:0] flit_o
);
  assign flit_o = flit_i;
endmodule
