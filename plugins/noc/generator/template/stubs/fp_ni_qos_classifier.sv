// Static finepaper framework stub. Placeholder only; no protocol semantics.
module fp_ni_qos_classifier #(
  parameter QOS_ENABLED = 1
) (
  input  logic [3:0] opcode_i,
  output logic [3:0] qos_class_o
);
  assign qos_class_o = QOS_ENABLED ? opcode_i : 4'h0;
endmodule
