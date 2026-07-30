// One-entry non-fall-through ready/valid stage for the generated NI shell.
// This provides flow control only; it does not implement protocol ordering.
module fp_ni_request_queue #(
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
  logic [FLIT_WIDTH-1:0] payload_q;
  logic                  valid_q;

  assign ready_o   = rst_n && !valid_q;
  assign payload_o = payload_q;
  assign valid_o   = valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      payload_q <= '0;
      valid_q   <= 1'b0;
    end else begin
      if (valid_q && ready_i)
        valid_q <= 1'b0;

      if (valid_i && ready_o) begin
        payload_q <= payload_i;
        valid_q   <= 1'b1;
      end
    end
  end
endmodule
