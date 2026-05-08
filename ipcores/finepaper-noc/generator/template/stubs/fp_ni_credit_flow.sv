// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_credit_flow (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       issue_i,
  input  logic       return_credit_i,
  output logic [7:0] credit_count_o,
  output logic       can_issue_o
);
  always_comb begin
    credit_count_o = {7'b0, return_credit_i};
    can_issue_o = rst_n & (return_credit_i | ~issue_i);
  end
endmodule
