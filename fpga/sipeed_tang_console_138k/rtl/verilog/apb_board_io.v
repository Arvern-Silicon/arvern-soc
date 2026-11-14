//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    apb_board_io
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : apb_board_io.v
// Module Description : APB4 peripheral for the Tang Console 138K board I/O:
//                     LEDs, SWITCHES and KEYs (i.e. buttons), with IRQ
//                     support, the audio amplifier enable and the microSD card
//                     detect. The USB ports are apb_usb_host completers
//                     of their own.
//
//                     All flops use the shared arv_ipdff primitive and the
//                     input synchronizers/debouncers the arv_synchronizer
//                     primitive (via sync_debouncer_10ms), so the reset
//                     architecture is selectable via ASYNC_RST_EN.
//
//                     Register Map (byte offsets from peripheral base):
//
//                       0x00  LED_CTRL        (R/W)  LED output control [7:0]
//                       0x04  TTY_DATA        (R/W)  TTY/printf data register
//                       0x08  AUDIO_CTRL      (R)    [0] PA_EN: speaker amplifier on (AUDIO_OUT = speaker); writes ignored
//                       0x0C  SD_STATUS       (R)    [0] SD_DET: debounced microSD detect pin level (0: card in)
//                       0x10  AUDIO_OUT       (R/W)  [1:0] the output of the I2S stream: 0 HDMI, 1 speaker,
//                                                    2 Pmod I2S2 on PMOD1, 3 none (reset AUDIO_OUT_RST)
//                       0x20  KEY_SW_VAL      (R)    Debounced key/switch values
//                       0x24  KEY_SW_IRQ_EN   (R/W)  IRQ enable per input [5:0]
//                       0x28  KEY_SW_IRQ_EDGE (R/W)  IRQ edge polarity [5:0] (1=rising, 0=falling)
//                       0x2C  KEY_SW_IRQ_BOTH (R/W)  Any-edge select [5:0] (1=both edges, overrides EDGE)
//                       0x30  KEY_SW_IRQ_VAL  (R/W)  IRQ status, write-1-to-clear [5:0]
//----------------------------------------------------------------------------

module  apb_board_io #(
    parameter       ASYNC_RST_EN  = 1'b1,  // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
    parameter       ADDRW         = 7,
    parameter [1:0] AUDIO_OUT_RST = 2'd0   // AUDIO_OUT reset value: 0 HDMI, 1 speaker, 2 Pmod I2S2, 3 none
) (

// APB CLOCK & RESET
    input  wire             pclk_i,
    input  wire             presetn_i,
    output wire             pclk_en_o,

// APB INTERFACE
    input  wire             psel_i,
    input  wire             penable_i,
    input  wire [ADDRW-1:0] paddr_i,
    input  wire             pwrite_i,
    input  wire      [31:0] pwdata_i,
    input  wire       [3:0] pstrb_i,
    input  wire       [2:0] pprot_i,
    input  wire             psmode_i,
    output wire      [31:0] prdata_o,
    output wire             pready_o,
    output wire             pslverr_o,

// BOARD I/O
    output wire       [7:0] led_o,
    output wire             pa_en_o,                // speaker amplifier enable (MAX98357A SD_MODE)
    output wire       [1:0] audio_out_o,            // the output of the I2S stream (AUDIO_OUT)
    input  wire       [1:0] key_i,
    input  wire       [3:0] sw_i,
    input  wire             sd_det_i,               // microSD card detect pin (asynchronous)

// ACTIVE REGISTERS (exposed for testbench probing)
    output wire      [31:0] tty_data_o,
    output wire      [31:0] key_sw_val_o,

// INTERRUPT OUTPUTS
    output wire             irq_key_o,
    output wire             irq_sw_o
 );


//=============================================================================
// 1)  PARAMETER DECLARATION
//=============================================================================

// Decoder bit width
localparam              DEC_WD            =  ADDRW-2;

// Register addresses (word offset)
localparam [DEC_WD-1:0] LED_CTRL          = 5'h00,
                        TTY_DATA          = 5'h01,
                        AUDIO_CTRL        = 5'h02,
                        SD_STATUS         = 5'h03,
                        AUDIO_OUT         = 5'h04,
                        KEY_SW_VAL        = 5'h08,
                        KEY_SW_IRQ_EN     = 5'h09,
                        KEY_SW_IRQ_EDGE   = 5'h0A,
                        KEY_SW_IRQ_BOTH   = 5'h0B,
                        KEY_SW_IRQ_VAL    = 5'h0C;

// Register one-hot decoder
localparam              DEC_SZ            = (1 << DEC_WD);
localparam [DEC_SZ-1:0] BASE_REG          = {{DEC_SZ-1{1'b0}}, 1'b1};

localparam [DEC_SZ-1:0] LED_CTRL_D        = (BASE_REG << LED_CTRL       ),
                        TTY_DATA_D        = (BASE_REG << TTY_DATA       ),
                        AUDIO_CTRL_D      = (BASE_REG << AUDIO_CTRL     ),
                        SD_STATUS_D       = (BASE_REG << SD_STATUS      ),
                        AUDIO_OUT_D       = (BASE_REG << AUDIO_OUT      ),
                        KEY_SW_VAL_D      = (BASE_REG << KEY_SW_VAL     ),
                        KEY_SW_IRQ_EN_D   = (BASE_REG << KEY_SW_IRQ_EN  ),
                        KEY_SW_IRQ_EDGE_D = (BASE_REG << KEY_SW_IRQ_EDGE),
                        KEY_SW_IRQ_BOTH_D = (BASE_REG << KEY_SW_IRQ_BOTH),
                        KEY_SW_IRQ_VAL_D  = (BASE_REG << KEY_SW_IRQ_VAL );


//=============================================================================
// 2)  APB ACCESS PHASE DETECTION
//=============================================================================
// APB holds the address, control and write data from SETUP to the end of
// ACCESS: the transfer is decoded straight from the bus.

wire                   dph_write  = psel_i & penable_i &  pwrite_i;
wire                   dph_read   = psel_i & penable_i & ~pwrite_i;
wire      [DEC_WD-1:0] dph_addr   = paddr_i[ADDRW-1:2];
wire             [3:0] dph_byte_mask = pstrb_i;

// Enable clock (for architectural clock-gating)
assign pclk_en_o          =  psel_i;


//============================================================================
// 3)  REGISTER DECODER
//============================================================================

wire [DEC_SZ-1:0] reg_dec =  (LED_CTRL_D        &  {DEC_SZ{dph_addr==LED_CTRL       }})  |
                             (TTY_DATA_D        &  {DEC_SZ{dph_addr==TTY_DATA       }})  |
                             (AUDIO_CTRL_D      &  {DEC_SZ{dph_addr==AUDIO_CTRL     }})  |
                             (SD_STATUS_D       &  {DEC_SZ{dph_addr==SD_STATUS      }})  |
                             (AUDIO_OUT_D       &  {DEC_SZ{dph_addr==AUDIO_OUT      }})  |
                             (KEY_SW_VAL_D      &  {DEC_SZ{dph_addr==KEY_SW_VAL     }})  |
                             (KEY_SW_IRQ_EN_D   &  {DEC_SZ{dph_addr==KEY_SW_IRQ_EN  }})  |
                             (KEY_SW_IRQ_EDGE_D &  {DEC_SZ{dph_addr==KEY_SW_IRQ_EDGE}})  |
                             (KEY_SW_IRQ_BOTH_D &  {DEC_SZ{dph_addr==KEY_SW_IRQ_BOTH}})  |
                             (KEY_SW_IRQ_VAL_D  &  {DEC_SZ{dph_addr==KEY_SW_IRQ_VAL }})  ;

// Read/Write vectors
wire [DEC_SZ-1:0] reg_wr  = reg_dec & {DEC_SZ{dph_write}};
wire [DEC_SZ-1:0] reg_rd  = reg_dec & {DEC_SZ{dph_read}};


//============================================================================
// 4)  APB RESPONSE
//============================================================================

// No wait states, no error response
assign pready_o           = 1'b1;
assign pslverr_o          = 1'b0;


//============================================================================
// 5)  INPUT DEBOUNCING
//============================================================================

wire [1:0] key_deb;
wire [3:0] sw_deb;
wire       sd_det_deb;

sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN), .RST_VAL(1'b1)) sync_deb_key1 (.signal_debounced(key_deb[1]), .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(key_i[1]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN), .RST_VAL(1'b1)) sync_deb_key0 (.signal_debounced(key_deb[0]), .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(key_i[0]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw3  (.signal_debounced(sw_deb[3]),  .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(sw_i[3]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw2  (.signal_debounced(sw_deb[2]),  .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(sw_i[2]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw1  (.signal_debounced(sw_deb[1]),  .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(sw_i[1]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw0  (.signal_debounced(sw_deb[0]),  .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(sw_i[0]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN), .RST_VAL(1'b1)) sync_deb_sd_det (.signal_debounced(sd_det_deb), .clk_50mhz(pclk_i), .rst(~presetn_i), .signal_async(sd_det_i));

wire [7:0] key_sw_val = {1'b0,      1'b0,      key_deb[1], key_deb[0],
                         sw_deb[3], sw_deb[2], sw_deb[1],  sw_deb[0] };


//============================================================================
// 6)  REGISTERS
//============================================================================

// LED Control Register (offset 0x00)
//-------------------------------------
wire [7:0] led_ctrl;
wire [7:0] led_ctrl_nxt;

wire       led_ctrl_wr   = reg_wr[LED_CTRL] & dph_byte_mask[0];

assign     led_ctrl_nxt  = led_ctrl_wr ? pwdata_i[7:0] : led_ctrl;

arv_ipdff #(.WIDTH(8), .ARST_EN(ASYNC_RST_EN)) u_led_ctrl (
                      .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                           .d_i (led_ctrl_nxt),
                                                           .q_o (led_ctrl));

assign     led_o         = led_ctrl;


// TTY Data Register (offset 0x04)
//----------------------------------
wire [31:0] tty_data;
wire [31:0] tty_data_nxt;

wire  [3:0] tty_data_wr    = {4{reg_wr[TTY_DATA]}} & dph_byte_mask;

assign tty_data_nxt[ 7: 0] = tty_data_wr[0] ? pwdata_i[ 7: 0] : tty_data[ 7: 0];
assign tty_data_nxt[15: 8] = tty_data_wr[1] ? pwdata_i[15: 8] : tty_data[15: 8];
assign tty_data_nxt[23:16] = tty_data_wr[2] ? pwdata_i[23:16] : tty_data[23:16];
assign tty_data_nxt[31:24] = tty_data_wr[3] ? pwdata_i[31:24] : tty_data[31:24];

arv_ipdff #(.WIDTH(32), .ARST_EN(ASYNC_RST_EN)) u_tty_data (
                       .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                            .d_i (tty_data_nxt),
                                                            .q_o (tty_data));

assign tty_data_o          = tty_data;


// AUDIO_CTRL Register (offset 0x08) - Read only
//---------------------------------------------------
// The amplifier is on when the speaker is the output
wire       pa_en;

assign     pa_en_o       = pa_en;


// AUDIO_OUT Register (offset 0x10)
//-----------------------------------
wire [1:0] audio_out;

wire       audio_out_wr  = reg_wr[AUDIO_OUT] & dph_byte_mask[0];

arv_ipdff #(.WIDTH(2), .RST_VAL(AUDIO_OUT_RST), .ARST_EN(ASYNC_RST_EN)) u_audio_out (
                      .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(audio_out_wr),
                                                           .d_i (pwdata_i[1:0]),
                                                           .q_o (audio_out));

assign     pa_en         = (audio_out == 2'd1);
assign     audio_out_o   = audio_out;


// KEY_SW_VAL Register (offset 0x20) - Read only
//------------------------------------------------
assign key_sw_val_o        = {24'h000000, key_sw_val};


// KEY_SW_IRQ_EN Register (offset 0x24)
//---------------------------------------
wire [5:0] key_sw_irq_en;
wire [5:0] key_sw_irq_en_nxt;

wire       key_sw_irq_en_wr  = reg_wr[KEY_SW_IRQ_EN] & dph_byte_mask[0];

assign     key_sw_irq_en_nxt = key_sw_irq_en_wr ? pwdata_i[5:0] : key_sw_irq_en;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_en (
    .clk_i   (pclk_i),
    .rst_n_i (presetn_i),
    .en_i    (1'b1),
    .d_i     (key_sw_irq_en_nxt),
    .q_o     (key_sw_irq_en));


// KEY_SW_IRQ_EDGE Register (offset 0x28)
//-----------------------------------------
wire [5:0] key_sw_irq_edge;
wire [5:0] key_sw_irq_edge_nxt;

wire       key_sw_irq_edge_wr  = reg_wr[KEY_SW_IRQ_EDGE] & dph_byte_mask[0];

assign     key_sw_irq_edge_nxt = key_sw_irq_edge_wr ? pwdata_i[5:0] : key_sw_irq_edge;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_edge (
                             .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                                  .d_i (key_sw_irq_edge_nxt),
                                                                  .q_o (key_sw_irq_edge));


// KEY_SW_IRQ_BOTH Register (offset 0x2C)
//-----------------------------------------
// When a bit is set, that source interrupts on ANY edge (rising OR falling),
// overriding the single-edge polarity in KEY_SW_IRQ_EDGE.
wire [5:0] key_sw_irq_both;
wire [5:0] key_sw_irq_both_nxt;

wire       key_sw_irq_both_wr  = reg_wr[KEY_SW_IRQ_BOTH] & dph_byte_mask[0];

assign     key_sw_irq_both_nxt = key_sw_irq_both_wr ? pwdata_i[5:0] : key_sw_irq_both;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_both (
                             .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                                  .d_i (key_sw_irq_both_nxt),
                                                                  .q_o (key_sw_irq_both));


// KEY_SW_IRQ_VAL Register (offset 0x30) - Write-1-to-clear
//-----------------------------------------------------------
wire [5:0] key_sw_irq_val;

wire       key_sw_irq_val_wr  = reg_wr[KEY_SW_IRQ_VAL] & dph_byte_mask[0];
wire [5:0] key_sw_irq_clr     = pwdata_i[5:0] & {6{key_sw_irq_val_wr}};
wire [5:0] key_sw_irq_set;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_val (
                            .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                                 .d_i (key_sw_irq_set | (~key_sw_irq_clr & key_sw_irq_val)),
                                                                 .q_o (key_sw_irq_val));

assign irq_key_o = |key_sw_irq_val[5:4];
assign irq_sw_o  = |key_sw_irq_val[3:0];


//============================================================================
// 7)  IRQ GENERATION
//============================================================================

// Delay debounced signal for edge detection
wire [5:0] key_sw_deb_dly;
arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_deb_dly (
                            .clk_i(pclk_i), .rst_n_i(presetn_i), .en_i(1'b1),
                                                                 .d_i (key_sw_val[5:0]),
                                                                 .q_o (key_sw_deb_dly));

wire [5:0] key_sw_posedge =  key_sw_val[5:0] & ~key_sw_deb_dly;
wire [5:0] key_sw_negedge = ~key_sw_val[5:0] &  key_sw_deb_dly;

// Single-edge selection (per KEY_SW_IRQ_EDGE), or any-edge when KEY_SW_IRQ_BOTH is set.
wire [5:0] key_sw_edge_sel = (key_sw_posedge  &  key_sw_irq_edge[5:0]) |
                             (key_sw_negedge  & ~key_sw_irq_edge[5:0]);
wire [5:0] key_sw_edge     = ( key_sw_irq_both[5:0] & (key_sw_posedge | key_sw_negedge)) |
                             (~key_sw_irq_both[5:0] &  key_sw_edge_sel);

assign     key_sw_irq_set =  key_sw_irq_en[5:0] & key_sw_edge;


//============================================================================
// 8)  READ MUX
//============================================================================

wire [31:0] led_ctrl_rd        = ({24'h000000, led_ctrl}        & {32{reg_rd[LED_CTRL]       }});
wire [31:0] tty_data_rd        = (tty_data                      & {32{reg_rd[TTY_DATA]       }});
wire [31:0] audio_ctrl_rd      = ({31'h00000000, pa_en}         & {32{reg_rd[AUDIO_CTRL]     }});
wire [31:0] sd_status_rd       = ({31'h00000000, sd_det_deb}    & {32{reg_rd[SD_STATUS]      }});
wire [31:0] audio_out_rd       = ({30'h00000000, audio_out}     & {32{reg_rd[AUDIO_OUT]      }});
wire [31:0] key_sw_val_rd      = ({24'h000000, key_sw_val}      & {32{reg_rd[KEY_SW_VAL]     }});
wire [31:0] key_sw_irq_en_rd   = ({26'h0000000, key_sw_irq_en}  & {32{reg_rd[KEY_SW_IRQ_EN]  }});
wire [31:0] key_sw_irq_edge_rd = ({26'h0000000, key_sw_irq_edge}& {32{reg_rd[KEY_SW_IRQ_EDGE]}});
wire [31:0] key_sw_irq_both_rd = ({26'h0000000, key_sw_irq_both}& {32{reg_rd[KEY_SW_IRQ_BOTH]}});
wire [31:0] key_sw_irq_val_rd  = ({26'h0000000, key_sw_irq_val} & {32{reg_rd[KEY_SW_IRQ_VAL] }});

assign      prdata_o           =  led_ctrl_rd        |
                                  tty_data_rd        |
                                  audio_ctrl_rd      |
                                  sd_status_rd       |
                                  audio_out_rd       |
                                  key_sw_val_rd      |
                                  key_sw_irq_en_rd   |
                                  key_sw_irq_edge_rd |
                                  key_sw_irq_both_rd |
                                  key_sw_irq_val_rd  ;


//-------------------------------------------------
// Lint cleanup
//-------------------------------------------------
wire  [1:0] paddr_unused   = paddr_i[1:0];
wire  [2:0] pprot_unused   = pprot_i;
wire        psmode_unused  = psmode_i;
wire        reg_dec_unused = |{reg_wr[DEC_SZ-1:KEY_SW_IRQ_VAL+1], reg_rd[DEC_SZ-1:KEY_SW_IRQ_VAL+1],
                               reg_wr[KEY_SW_VAL-1:AUDIO_OUT+1],  reg_rd[KEY_SW_VAL-1:AUDIO_OUT+1],
                               reg_wr[KEY_SW_VAL], reg_wr[SD_STATUS], reg_wr[AUDIO_CTRL]};


endmodule // apb_board_io
