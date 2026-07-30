// V3 clock-domain crossing primitive for one ready/valid stream.
//
// Only the Gray-coded ownership pointers cross the synchronizer chains below.
// The multi-bit payload never passes through per-bit synchronizers: it resides
// in dual-clock storage, is written in the source domain, and is read only from
// a slot whose ownership has safely reached the destination domain.
//
// Reset assertion is independent and asynchronous in each clock domain. A
// system integrating this FIFO must quiesce traffic and assert both resets as
// one coordinated operation. A unilateral live reset is outside this module's
// contract and may replay, lose, or overwrite data. Each reset must be
// deasserted through a synchronizer for its local clock.
`default_nettype none

module fp_async_ready_valid_fifo #(
  parameter int unsigned PAYLOAD_WIDTH = 128,
  parameter int unsigned DEPTH         = 4,
  parameter int unsigned SYNC_STAGES   = 2
) (
  input  logic                                                   src_clk_i,
  input  logic                                                   src_rst_ni,
  input  logic [(PAYLOAD_WIDTH > 0 ? PAYLOAD_WIDTH : 1)-1:0]     src_payload_i,
  input  logic                                                   src_valid_i,
  output logic                                                   src_ready_o,

  input  logic                                                   dst_clk_i,
  input  logic                                                   dst_rst_ni,
  output logic [(PAYLOAD_WIDTH > 0 ? PAYLOAD_WIDTH : 1)-1:0]     dst_payload_o,
  output logic                                                   dst_valid_o,
  input  logic                                                   dst_ready_i
);
  localparam bit DEPTH_VALID =
    (DEPTH >= 2) && (DEPTH <= 1024) && ((DEPTH & (DEPTH - 1)) == 0);
  localparam bit SYNC_STAGES_VALID =
    (SYNC_STAGES >= 2) && (SYNC_STAGES <= 8);
  // Invalid public parameters still elaborate a small, structurally valid
  // shell so the executable guards below can report the real contract error.
  localparam int unsigned EFFECTIVE_PAYLOAD_WIDTH =
    PAYLOAD_WIDTH > 0 ? PAYLOAD_WIDTH : 1;
  localparam int unsigned EFFECTIVE_DEPTH = DEPTH_VALID ? DEPTH : 2;
  localparam int unsigned EFFECTIVE_SYNC_STAGES =
    SYNC_STAGES_VALID ? SYNC_STAGES : 2;
  localparam int unsigned ADDRESS_WIDTH = $clog2(EFFECTIVE_DEPTH);
  localparam int unsigned POINTER_WIDTH = ADDRESS_WIDTH + 1;

  // These checks are executable so an unsupported configuration fails closed
  // in simulation instead of silently producing a malformed CDC structure.
  initial begin : validate_parameters
    if (PAYLOAD_WIDTH < 1)
      $fatal(1, "fp_async_ready_valid_fifo: PAYLOAD_WIDTH must be positive");
    if (!DEPTH_VALID)
      $fatal(1, "fp_async_ready_valid_fifo: DEPTH must be a power of two in the inclusive range 2..1024");
    if (!SYNC_STAGES_VALID)
      $fatal(1, "fp_async_ready_valid_fifo: SYNC_STAGES must be in the inclusive range 2..8");
  end

  logic [EFFECTIVE_PAYLOAD_WIDTH-1:0] storage_q [0:EFFECTIVE_DEPTH-1];

  logic [POINTER_WIDTH-1:0] src_binary_q;
  logic [POINTER_WIDTH-1:0] src_binary_next;
  logic [POINTER_WIDTH-1:0] src_gray_q;
  logic [POINTER_WIDTH-1:0] src_gray_next;
  logic                     src_full_q;
  logic                     src_full_next;
  logic                     src_push;

  logic [POINTER_WIDTH-1:0] dst_binary_q;
  logic [POINTER_WIDTH-1:0] dst_binary_next;
  logic [POINTER_WIDTH-1:0] dst_gray_q;
  logic [POINTER_WIDTH-1:0] dst_gray_next;
  logic                     dst_empty_q;
  logic                     dst_empty_next;
  logic                     dst_pop;

  // Mark only the Gray-pointer synchronizer registers as asynchronous CDC
  // chains. Applying this attribute to payload storage would be incorrect.
  (* async_reg = "true" *)
  logic [POINTER_WIDTH-1:0] dst_gray_src_sync_q [0:EFFECTIVE_SYNC_STAGES-1];
  (* async_reg = "true" *)
  logic [POINTER_WIDTH-1:0] src_gray_dst_sync_q [0:EFFECTIVE_SYNC_STAGES-1];

  logic [POINTER_WIDTH-1:0] dst_gray_src_q;
  logic [POINTER_WIDTH-1:0] src_gray_dst_q;
  logic [POINTER_WIDTH-1:0] dst_gray_full_compare;

  assign dst_gray_src_q = dst_gray_src_sync_q[EFFECTIVE_SYNC_STAGES-1];
  assign src_gray_dst_q = src_gray_dst_sync_q[EFFECTIVE_SYNC_STAGES-1];

  assign src_ready_o = src_rst_ni && !src_full_q;
  assign src_push    = src_valid_i && src_ready_o;
  assign dst_valid_o = dst_rst_ni && !dst_empty_q;
  assign dst_pop     = dst_valid_o && dst_ready_i;

  // Returning zero while empty/reset keeps the public payload deterministic.
  // While valid is stalled, dst_binary_q cannot advance and this value remains
  // stable until the consumer completes the handshake.
  assign dst_payload_o = dst_valid_o
                       ? storage_q[dst_binary_q[ADDRESS_WIDTH-1:0]]
                       : '0;

  always_comb begin
    src_binary_next = src_binary_q
                    + {{ADDRESS_WIDTH{1'b0}}, src_push};
    src_gray_next = (src_binary_next >> 1) ^ src_binary_next;

    // Full is the next write Gray pointer meeting the synchronized read
    // pointer after its wrap and quadrant bits have both inverted.
    dst_gray_full_compare = dst_gray_src_q;
    dst_gray_full_compare[POINTER_WIDTH-1 -: 2]
      = ~dst_gray_src_q[POINTER_WIDTH-1 -: 2];
    src_full_next = (src_gray_next == dst_gray_full_compare);
  end

  always_comb begin
    dst_binary_next = dst_binary_q
                    + {{ADDRESS_WIDTH{1'b0}}, dst_pop};
    dst_gray_next = (dst_binary_next >> 1) ^ dst_binary_next;
    dst_empty_next = (dst_gray_next == src_gray_dst_q);
  end

  always_ff @(posedge src_clk_i or negedge src_rst_ni) begin
    if (!src_rst_ni) begin
      src_binary_q <= '0;
      src_gray_q   <= '0;
      src_full_q   <= 1'b0;
    end else begin
      src_binary_q <= src_binary_next;
      src_gray_q   <= src_gray_next;
      src_full_q   <= src_full_next;
      if (src_push)
        storage_q[src_binary_q[ADDRESS_WIDTH-1:0]] <= src_payload_i;
    end
  end

  always_ff @(posedge dst_clk_i or negedge dst_rst_ni) begin
    if (!dst_rst_ni) begin
      dst_binary_q <= '0;
      dst_gray_q   <= '0;
      dst_empty_q  <= 1'b1;
    end else begin
      dst_binary_q <= dst_binary_next;
      dst_gray_q   <= dst_gray_next;
      dst_empty_q  <= dst_empty_next;
    end
  end

  always_ff @(posedge src_clk_i or negedge src_rst_ni) begin : sync_read_pointer_to_source
    if (!src_rst_ni) begin
      for (int unsigned stage = 0; stage < EFFECTIVE_SYNC_STAGES; stage++)
        dst_gray_src_sync_q[stage] <= '0;
    end else begin
      dst_gray_src_sync_q[0] <= dst_gray_q;
      for (int unsigned stage = 1; stage < EFFECTIVE_SYNC_STAGES; stage++)
        dst_gray_src_sync_q[stage] <= dst_gray_src_sync_q[stage-1];
    end
  end

  always_ff @(posedge dst_clk_i or negedge dst_rst_ni) begin : sync_write_pointer_to_destination
    if (!dst_rst_ni) begin
      for (int unsigned stage = 0; stage < EFFECTIVE_SYNC_STAGES; stage++)
        src_gray_dst_sync_q[stage] <= '0;
    end else begin
      src_gray_dst_sync_q[0] <= src_gray_q;
      for (int unsigned stage = 1; stage < EFFECTIVE_SYNC_STAGES; stage++)
        src_gray_dst_sync_q[stage] <= src_gray_dst_sync_q[stage-1];
    end
  end
endmodule

`default_nettype wire
