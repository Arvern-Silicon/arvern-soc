//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    hdmi_rx_model
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : hdmi_rx_model.v
// Module Description : HDMI (DVI) sink model for the Tang bench: samples the
//                      four TMDS lanes in the middle of each bit (bit clock
//                      edges from the bench), finds the word boundary from the
//                      clock lane (0b1111100000, bit 0 first), decodes the
//                      three data lanes and drives a parallel RGB interface
//                      (one pixel clock per TMDS word) for the panel model.
//                      In blanking it also takes the HDMI characters as
//                      blanking: video guard bands, data island guard bands
//                      and TERC4 characters (the syncs from channel 0), and
//                      counts the island characters. Counts words whose
//                      lanes disagree on DE.
//----------------------------------------------------------------------------

module hdmi_rx_model (
    input  wire       fclk_i,             // serialiser bit clock (both edges)
    input  wire [2:0] d_i,                // data lanes 0..2 (P side)
    input  wire       clk_lane_i,         // clock lane (P side)
    output reg        pclk_o,             // recovered pixel clock
    output reg        de_o,
    output reg        hs_o,
    output reg        vs_o,
    output reg  [7:0] r_o,
    output reg  [7:0] g_o,
    output reg  [7:0] b_o
);

integer    errors   = 0;
integer    words    = 0;
integer    island   = 0;                  // data island characters (guard bands included)
integer    vguard   = 0;                  // video guard band characters
reg        blank    = 1'b1;               // a control word since the last data word
integer    vg_run   = 0;                  // video guard band characters in a row
reg  [9:0] sr [0:3];
integer    i;

initial begin
   pclk_o = 1'b0; de_o = 1'b0; hs_o = 1'b1; vs_o = 1'b1;
   r_o = 8'h0; g_o = 8'h0; b_o = 8'h0;
   for (i = 0; i < 4; i = i + 1) sr[i] = 10'h0;
end

function is_ctl;
   input [9:0] s;
   is_ctl = (s == 10'b1101010100) || (s == 10'b0010101011) ||
            (s == 10'b0101010100) || (s == 10'b1010101011);
endfunction

// TERC4: {valid, nibble}
function [4:0] terc4_dec;
   input [9:0] s;
   case (s)
      10'b1010011100: terc4_dec = 5'h10; 10'b1001100011: terc4_dec = 5'h11;
      10'b1011100100: terc4_dec = 5'h12; 10'b1011100010: terc4_dec = 5'h13;
      10'b0101110001: terc4_dec = 5'h14; 10'b0100011110: terc4_dec = 5'h15;
      10'b0110001110: terc4_dec = 5'h16; 10'b0100111100: terc4_dec = 5'h17;
      10'b1011001100: terc4_dec = 5'h18; 10'b0100111001: terc4_dec = 5'h19;
      10'b0110011100: terc4_dec = 5'h1A; 10'b1011000110: terc4_dec = 5'h1B;
      10'b1010001110: terc4_dec = 5'h1C; 10'b1001110001: terc4_dec = 5'h1D;
      10'b0101100011: terc4_dec = 5'h1E; 10'b1011000011: terc4_dec = 5'h1F;
      default:        terc4_dec = 5'h00;
   endcase
endfunction

function [7:0] dec;
   input [9:0] s;
   reg   [7:0] v;
   integer     j;
   begin
      v      = s[9] ? ~s[7:0] : s[7:0];
      dec[0] = v[0];
      for (j = 1; j < 8; j = j + 1)
         dec[j] = s[8] ? (v[j] ^ v[j-1]) : ~(v[j] ^ v[j-1]);
   end
endfunction

reg       ctl0, ctl1, ctl2;
reg [4:0] t0, t1, t2;
reg       vgb, igb, terc;

always @(fclk_i) begin
   #2;                                    // middle of the 4 ns bit
   sr[0] = {d_i[0],     sr[0][9:1]};
   sr[1] = {d_i[1],     sr[1][9:1]};
   sr[2] = {d_i[2],     sr[2][9:1]};
   sr[3] = {clk_lane_i, sr[3][9:1]};
   if (sr[3] == 10'b1111100000) begin     // a whole word received
      words = words + 1;
      ctl0  = is_ctl(sr[0]);
      ctl1  = is_ctl(sr[1]);
      ctl2  = is_ctl(sr[2]);
      if ((ctl0 != ctl1) || (ctl0 != ctl2)) begin
         if (errors < 10) $display("ERROR: HDMI sink -- lanes disagree on DE %t", $time);
         errors = errors + 1;
      end
      t0   = terc4_dec(sr[0]); t1 = terc4_dec(sr[1]); t2 = terc4_dec(sr[2]);
      vgb  = (sr[0] == 10'b1011001100) && (sr[1] == 10'b0100110011) && (sr[2] == 10'b1011001100);
      igb  = t0[4] && (sr[1] == 10'b0100110011) && (sr[2] == 10'b0100110011);
      terc = t0[4] && t1[4] && t2[4];
      if (ctl0) begin
         de_o = 1'b0;
         hs_o = (sr[0] == 10'b0010101011) || (sr[0] == 10'b1010101011);
         vs_o = (sr[0] == 10'b0101010100) || (sr[0] == 10'b1010101011);
         r_o  = 8'h0; g_o = 8'h0; b_o = 8'h0;
         blank = 1'b1; vg_run = 0;
      end else if (blank && (vg_run < 2) && vgb) begin
         de_o = 1'b0;
         vguard = vguard + 1; vg_run = vg_run + 1;
      end else if (blank && (vg_run == 0) && (igb || terc)) begin
         de_o = 1'b0;
         hs_o = t0[0]; vs_o = t0[1];
         island = island + 1;
      end else begin
         blank = 1'b0; vg_run = 0;
         de_o = 1'b1;
         b_o  = dec(sr[0]);
         g_o  = dec(sr[1]);
         r_o  = dec(sr[2]);
      end
      pclk_o <= #1  1'b1;                 // scheduled: sampling goes on meanwhile
      pclk_o <= #20 1'b0;
   end
end

endmodule // hdmi_rx_model
