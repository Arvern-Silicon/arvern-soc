//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ahb_led_key_sw
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ahb_led_key_sw.v
// Module Description : AHB peripheral for the DE0-Nano-SoC board
//                     for driving LEDs and reading SWITCHES and KEYs
//                     (i.e. buttons), with IRQ support.
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
//                       0x20  KEY_SW_VAL      (R)    Debounced key/switch values
//                       0x24  KEY_SW_IRQ_EN   (R/W)  IRQ enable per input [5:0]
//                       0x28  KEY_SW_IRQ_EDGE (R/W)  IRQ edge polarity [5:0] (1=rising, 0=falling)
//                       0x2C  KEY_SW_IRQ_BOTH (R/W)  Any-edge select [5:0] (1=both edges, overrides EDGE)
//                       0x30  KEY_SW_IRQ_VAL  (R/W)  IRQ status, write-1-to-clear [5:0]
//----------------------------------------------------------------------------

module  ahb_led_key_sw #(
    parameter ASYNC_RST_EN = 1'b1,  // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
    parameter ADDRW        = 7
) (

// AHB CLOCK & RESET
    input  wire             hclk_i,
    input  wire             hresetn_i,
    output wire             hclk_en_o,

// AHB INTERFACE
    input  wire [ADDRW-1:0] haddr_i,
    input  wire       [3:0] hprot_i,
    input  wire             hready_i,
    input  wire       [2:0] hsize_i,
    input  wire             hsmode_i,
    input  wire       [1:0] htrans_i,
    input  wire      [31:0] hwdata_i,
    input  wire             hwrite_i,
    input  wire             hsel_i,
    output wire      [31:0] hrdata_o,
    output wire             hreadyout_o,
    output wire             hresp_o,

// BOARD I/O
    output wire       [7:0] led_o,
    input  wire       [1:0] key_i,
    input  wire       [3:0] sw_i,

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
                        KEY_SW_VAL_D      = (BASE_REG << KEY_SW_VAL     ),
                        KEY_SW_IRQ_EN_D   = (BASE_REG << KEY_SW_IRQ_EN  ),
                        KEY_SW_IRQ_EDGE_D = (BASE_REG << KEY_SW_IRQ_EDGE),
                        KEY_SW_IRQ_BOTH_D = (BASE_REG << KEY_SW_IRQ_BOTH),
                        KEY_SW_IRQ_VAL_D  = (BASE_REG << KEY_SW_IRQ_VAL );


//=============================================================================
// 2)  AHB ADDRESS/DATA PHASE DETECTION
//=============================================================================

wire                   aph_valid;
wire                   aph_write;
wire             [3:0] aph_byte_mask;

wire                   dph_valid;
wire                   dph_write;
wire                   dph_read;
wire      [DEC_WD-1:0] dph_addr;
wire             [3:0] dph_byte_mask;

// Detect valid AHB transaction (address Phase)
assign   aph_valid     = hsel_i && hready_i && htrans_i[1];
assign   aph_write     = aph_valid &&  hwrite_i;

// Compute byte mask based on address LSB and size
assign   aph_byte_mask = {((hsize_i[1:0]==2'b00) && (haddr_i[1:0]==2'b11)) || ((hsize_i[1:0]==2'b01) && (haddr_i[1]==1'b1)) || (hsize_i[1:0]==2'b10),
                          ((hsize_i[1:0]==2'b00) && (haddr_i[1:0]==2'b10)) || ((hsize_i[1:0]==2'b01) && (haddr_i[1]==1'b1)) || (hsize_i[1:0]==2'b10),
                          ((hsize_i[1:0]==2'b00) && (haddr_i[1:0]==2'b01)) || ((hsize_i[1:0]==2'b01) && (haddr_i[1]==1'b0)) || (hsize_i[1:0]==2'b10),
                          ((hsize_i[1:0]==2'b00) && (haddr_i[1:0]==2'b00)) || ((hsize_i[1:0]==2'b01) && (haddr_i[1]==1'b0)) || (hsize_i[1:0]==2'b10)};

// Data Phase registers: on aph_valid latch the address-phase info; else clear
// when the bus is ready; else hold (en = aph_valid | hready_i).
wire              dph_en  = aph_valid | hready_i;
wire [DEC_WD+5:0] dph_nxt = aph_valid ? {1'b1, aph_write, haddr_i[ADDRW-1:2], aph_byte_mask}
                                      : {(DEC_WD+6){1'b0}};

arv_ipdff #(.WIDTH(DEC_WD+6), .ARST_EN(ASYNC_RST_EN)) u_dph (
                        .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(dph_en),
                                                             .d_i (dph_nxt),
                                                             .q_o ({dph_valid, dph_write, dph_addr, dph_byte_mask}));

assign dph_read           =  dph_valid & ~dph_write;

// Enable clock (for architectural clock-gating)
assign hclk_en_o          =  aph_valid | dph_valid;


//============================================================================
// 3)  REGISTER DECODER
//============================================================================

wire [DEC_SZ-1:0] reg_dec =  (LED_CTRL_D        &  {DEC_SZ{dph_addr==LED_CTRL       }})  |
                             (TTY_DATA_D        &  {DEC_SZ{dph_addr==TTY_DATA       }})  |
                             (KEY_SW_VAL_D      &  {DEC_SZ{dph_addr==KEY_SW_VAL     }})  |
                             (KEY_SW_IRQ_EN_D   &  {DEC_SZ{dph_addr==KEY_SW_IRQ_EN  }})  |
                             (KEY_SW_IRQ_EDGE_D &  {DEC_SZ{dph_addr==KEY_SW_IRQ_EDGE}})  |
                             (KEY_SW_IRQ_BOTH_D &  {DEC_SZ{dph_addr==KEY_SW_IRQ_BOTH}})  |
                             (KEY_SW_IRQ_VAL_D  &  {DEC_SZ{dph_addr==KEY_SW_IRQ_VAL }})  ;

// Read/Write vectors
wire [DEC_SZ-1:0] reg_wr  = reg_dec & {DEC_SZ{dph_write}};
wire [DEC_SZ-1:0] reg_rd  = reg_dec & {DEC_SZ{dph_read}};


//============================================================================
// 4)  AHB RESPONSE
//============================================================================

// No wait states, no error response
assign hreadyout_o        = 1'b1;
assign hresp_o            = 1'b0;


//============================================================================
// 5)  INPUT DEBOUNCING
//============================================================================

wire [1:0] key_deb;
wire [3:0] sw_deb;

sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_key1 (.signal_debounced(key_deb[1]), .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(key_i[1]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_key0 (.signal_debounced(key_deb[0]), .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(key_i[0]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw3  (.signal_debounced(sw_deb[3]),  .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(sw_i[3]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw2  (.signal_debounced(sw_deb[2]),  .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(sw_i[2]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw1  (.signal_debounced(sw_deb[1]),  .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(sw_i[1]));
sync_debouncer_10ms #(.ASYNC_RST_EN(ASYNC_RST_EN)) sync_deb_sw0  (.signal_debounced(sw_deb[0]),  .clk_50mhz(hclk_i), .rst(~hresetn_i), .signal_async(sw_i[0]));

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

assign     led_ctrl_nxt  = led_ctrl_wr ? hwdata_i[7:0] : led_ctrl;

arv_ipdff #(.WIDTH(8), .ARST_EN(ASYNC_RST_EN)) u_led_ctrl (
                      .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
                                                           .d_i (led_ctrl_nxt),
                                                           .q_o (led_ctrl));

assign     led_o         = led_ctrl;


// TTY Data Register (offset 0x04)
//----------------------------------
wire [31:0] tty_data;
wire [31:0] tty_data_nxt;

wire  [3:0] tty_data_wr    = {4{reg_wr[TTY_DATA]}} & dph_byte_mask;

assign tty_data_nxt[ 7: 0] = tty_data_wr[0] ? hwdata_i[ 7: 0] : tty_data[ 7: 0];
assign tty_data_nxt[15: 8] = tty_data_wr[1] ? hwdata_i[15: 8] : tty_data[15: 8];
assign tty_data_nxt[23:16] = tty_data_wr[2] ? hwdata_i[23:16] : tty_data[23:16];
assign tty_data_nxt[31:24] = tty_data_wr[3] ? hwdata_i[31:24] : tty_data[31:24];

arv_ipdff #(.WIDTH(32), .ARST_EN(ASYNC_RST_EN)) u_tty_data (
                       .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
                                                            .d_i (tty_data_nxt),
                                                            .q_o (tty_data));

assign tty_data_o          = tty_data;


// KEY_SW_VAL Register (offset 0x20) - Read only
//------------------------------------------------
assign key_sw_val_o        = {24'h000000, key_sw_val};


// KEY_SW_IRQ_EN Register (offset 0x24)
//---------------------------------------
wire [5:0] key_sw_irq_en;
wire [5:0] key_sw_irq_en_nxt;

wire       key_sw_irq_en_wr  = reg_wr[KEY_SW_IRQ_EN] & dph_byte_mask[0];

assign     key_sw_irq_en_nxt = key_sw_irq_en_wr ? hwdata_i[5:0] : key_sw_irq_en;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_en (
    .clk_i   (hclk_i),
    .rst_n_i (hresetn_i),
    .en_i    (1'b1),
    .d_i     (key_sw_irq_en_nxt),
    .q_o     (key_sw_irq_en));


// KEY_SW_IRQ_EDGE Register (offset 0x28)
//-----------------------------------------
wire [5:0] key_sw_irq_edge;
wire [5:0] key_sw_irq_edge_nxt;

wire       key_sw_irq_edge_wr  = reg_wr[KEY_SW_IRQ_EDGE] & dph_byte_mask[0];

assign     key_sw_irq_edge_nxt = key_sw_irq_edge_wr ? hwdata_i[5:0] : key_sw_irq_edge;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_edge (
                             .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
                                                                  .d_i (key_sw_irq_edge_nxt),
                                                                  .q_o (key_sw_irq_edge));


// KEY_SW_IRQ_BOTH Register (offset 0x30)
//-----------------------------------------
// When a bit is set, that source interrupts on ANY edge (rising OR falling),
// overriding the single-edge polarity in KEY_SW_IRQ_EDGE.
wire [5:0] key_sw_irq_both;
wire [5:0] key_sw_irq_both_nxt;

wire       key_sw_irq_both_wr  = reg_wr[KEY_SW_IRQ_BOTH] & dph_byte_mask[0];

assign     key_sw_irq_both_nxt = key_sw_irq_both_wr ? hwdata_i[5:0] : key_sw_irq_both;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_both (
                             .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
                                                                  .d_i (key_sw_irq_both_nxt),
                                                                  .q_o (key_sw_irq_both));


// KEY_SW_IRQ_VAL Register (offset 0x2C) - Write-1-to-clear
//-----------------------------------------------------------
wire [5:0] key_sw_irq_val;

wire       key_sw_irq_val_wr  = reg_wr[KEY_SW_IRQ_VAL] & dph_byte_mask[0];
wire [5:0] key_sw_irq_clr     = hwdata_i[5:0] & {6{key_sw_irq_val_wr}};
wire [5:0] key_sw_irq_set;

arv_ipdff #(.WIDTH(6), .ARST_EN(ASYNC_RST_EN)) u_key_sw_irq_val (
                            .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
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
                            .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
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
wire [31:0] key_sw_val_rd      = ({24'h000000, key_sw_val}      & {32{reg_rd[KEY_SW_VAL]     }});
wire [31:0] key_sw_irq_en_rd   = ({26'h0000000, key_sw_irq_en}  & {32{reg_rd[KEY_SW_IRQ_EN]  }});
wire [31:0] key_sw_irq_edge_rd = ({26'h0000000, key_sw_irq_edge}& {32{reg_rd[KEY_SW_IRQ_EDGE]}});
wire [31:0] key_sw_irq_both_rd = ({26'h0000000, key_sw_irq_both}& {32{reg_rd[KEY_SW_IRQ_BOTH]}});
wire [31:0] key_sw_irq_val_rd  = ({26'h0000000, key_sw_irq_val} & {32{reg_rd[KEY_SW_IRQ_VAL] }});

assign      hrdata_o           =  led_ctrl_rd        |
                                  tty_data_rd        |
                                  key_sw_val_rd      |
                                  key_sw_irq_en_rd   |
                                  key_sw_irq_edge_rd |
                                  key_sw_irq_both_rd |
                                  key_sw_irq_val_rd  ;


//-------------------------------------------------
// Lint cleanup
//-------------------------------------------------
wire        htrans0_unused = htrans_i[0];
wire        hsize2_unused  = hsize_i[2];
wire  [3:0] hprot_unused   = hprot_i;
wire        hsmode_unused  = hsmode_i;
wire        reg_dec_unused = |{reg_wr[DEC_SZ-1:KEY_SW_IRQ_VAL+1], reg_rd[DEC_SZ-1:KEY_SW_IRQ_VAL+1],
                               reg_wr[KEY_SW_VAL-1:TTY_DATA+1],   reg_rd[KEY_SW_VAL-1:TTY_DATA+1],
                               reg_wr[KEY_SW_VAL]};


endmodule // ahb_led_key_sw
