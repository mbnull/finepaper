// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_error_check #(
  parameter FLIT_WIDTH = 128
) (
  input  logic [FLIT_WIDTH-1:0] flit_i,
  output logic                  error_o
);
  assign error_o = ^flit_i === 1'bx;
endmodule
