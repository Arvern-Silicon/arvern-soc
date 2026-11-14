//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    OSER10
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : OSER10.v
// Module Description : Behavioural model of the Gowin GW5A OSER10 primitive
//                      (10:1 output serialiser), for simulation and lint; the
//                      Gowin tools supply the primitive itself. The word on
//                      D9..D0 is taken at each PCLK rising edge and sent D0
//                      first, one bit per FCLK edge (FCLK = 5 x PCLK, same
//                      phase), without the primitive's pipeline latency.
//----------------------------------------------------------------------------

module OSER10 #(
    parameter GSREN = "false",
    parameter LSREN = "true"
) (
    output wire Q,
    input  wire D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    input  wire PCLK,
    input  wire FCLK,
    input  wire RESET
);

reg  [9:0] word = 10'h0;
reg  [9:0] sh   = 10'h0;
reg        q_r    = 1'b0;
integer    k    = 0;
realtime   t_pclk = -1.0;

always @(posedge PCLK) begin
   word   <= {D9, D8, D7, D6, D5, D4, D3, D2, D1, D0};
   t_pclk  = $realtime;
end

always @(FCLK) begin
   #0.1;
   if (RESET) begin
      q_r = 1'b0;
      k = 0;
   end else begin
      if (($realtime - t_pclk) < 0.15) begin       // the word boundary
         sh = word;
         k  = 0;
      end
      q_r = sh[k];
      k = (k < 9) ? k + 1 : 9;
   end
end

assign Q = q_r;

endmodule // OSER10
