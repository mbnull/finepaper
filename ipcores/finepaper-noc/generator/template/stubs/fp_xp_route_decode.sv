// Static finepaper framework stub. Placeholder only; no router semantics.
module fp_xp_route_decode #(
  parameter ADDR_WIDTH = 32
) (
  input  logic [ADDR_WIDTH-1:0] dst_addr_i,
  output logic [2:0]            route_sel_o
);
  assign route_sel_o = dst_addr_i[2:0];
endmodule
