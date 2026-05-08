// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_protocol_decode #(
  parameter FLIT_WIDTH = 128,
  parameter DATA_WIDTH = 64
) (
  input  logic [FLIT_WIDTH-1:0] flit_i,
  output logic [3:0]            opcode_o,
  output logic                  is_read_o,
  output logic                  is_write_o
);
  assign opcode_o = flit_i[3:0];
  assign is_read_o = opcode_o[0];
  assign is_write_o = opcode_o[1];
endmodule
