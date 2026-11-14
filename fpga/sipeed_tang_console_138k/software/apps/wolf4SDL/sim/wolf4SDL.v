//----------------------------------------------------------------------------
// Wolf3D port (private): stimulus for the Tang simulation
//
//   The microSD slot holds the shareware data (sd_card_img.v, FAT12 image of
//   gen_sdimage.py). The firmware's TTY characters are echoed; every 20th
//   panel frame is saved (frame_log/fb_<n>.hex, 480 x 272 RGB565 -> PNG).
//   A gamepad on USB-A J15 (the bench's device model usb_dev0, a
//   DragonRise-style SNES pad attached at reset, reports pushed to its
//   interrupt endpoint): at panel frame PAD_T0 Start (the attract demo stops,
//   menu on "Read This!"), A (the article), A (next page), B (back to the menu),
//   Up five times (New Game; Save Game is skipped), A three times (New Game, episode, difficulty),
//   Up held (walk), A twice (fire). Then a save and a load: Start, Down x4
//   (Save Game), A, A (slot 0, the name the game proposes), Down x4 (Back to
//   Game), A, Up held (walk away), Start, Up x5 (Load Game), A, A (slot 0).
//   The menu moves are counted (menu_moves: the cursor's move sound, "sfx 4"
//   on the TTY): a tap the menu did not take is tapped again, one it took
//   twice is undone by a tap the other way.
//   The simulation build reports the game state on the TTY after a save and
//   after a load ("wolf save: ..." / "wolf load: ...": position, angle,
//   health, ammo, map, checksum); the load must restore the saved state.
//   Stops after WOLF_FRAMES panel frames (~31 s simulated, hence the 36 s
//   limit). Without a limit:
//     SIMULATION_DEFINES="-D NO_TIMEOUT -D NOTRACE" ./run wolf4SDL -nodump
// vscreen: no_input
// defines: -D TIMEOUT_MS=36000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

integer    frame_nr;
reg [255:0] fname;
localparam integer WOLF_FRAMES = 1900;
localparam integer PAD_T0      = 330;

// Gamepad reports (8 bytes, byte 0 in bits [7:0]): byte 0 X, byte 1 Y (00 up,
// 7F centre), byte 5 bit 5 A, bit 6 B, byte 6 bit 5 Start
localparam [63:0] PAD_IDLE  = 64'h0000_0F80_8000_7F7F,
                  PAD_A     = 64'h0000_2F80_8000_7F7F,
                  PAD_B     = 64'h0000_4F80_8000_7F7F,
                  PAD_START = 64'h0020_0F80_8000_7F7F,
                  PAD_UP    = 64'h0000_0F80_8000_007F,
                  PAD_DOWN  = 64'h0000_0F80_8000_FF7F;

task pad_set;
   input [63:0] rpt;
   begin
      usb_dev0.push_report(8, rpt);
      $display("  pad %h at panel frame %0d", rpt, frame_nr);
   end
endtask

task wait_frames;
   input integer n;
   integer k;
   begin
      for (k = 0; k < n; k = k + 1) @(panel.frame_done);
   end
endtask

task pad_press;
   input [63:0] rpt;
   input integer hold;
   begin
      pad_set(rpt);
      wait_frames(hold);
      pad_set(PAD_IDLE);
   end
endtask

// A tap: pressed until the host has acknowledged the report (the device
// model's int_ack_cnt probe advances; the previous release went out frames
// ago) and one panel frame more, so a menu that reads the pad once per frame
// sees exactly one press
task pad_tap;
   input [63:0] rpt;
   integer n0;
   begin
      n0 = usb_dev0.int_ack_cnt;
      pad_set(rpt);
      wait (usb_dev0.int_ack_cnt != n0);
      wait_frames(1);
      pad_set(PAD_IDLE);
   end
endtask

// Menu moves: n cursor moves with taps of rpt (back: the other way). The
// firmware samples the pad once per menu loop and a tap is visible for about
// one USB poll (10 ms), so a slow loop (the HDMI board) can miss a tap and a
// fast one (the single displayed frame) take it twice: after a move the menu
// waits 20 tics or until the pad is released, and 20 tics are 0.6 ms at the
// simulation's game clock. Each move draws the cursor at its new place with
// its sound (MOVEGUN2SND, "sfx 4" on the TTY), counted here; the taps go on
// (up to 4 n + 4) until the count is n.
integer gun_n;
initial gun_n = 0;

task menu_moves;
   input [63:0] rpt;
   input [63:0] back;
   input integer n;
   integer done, k, g0;
   begin
      done = 0;
      for (k = 0; (k < 4*n + 4) && (done != n); k = k + 1) begin
         g0 = gun_n;
         pad_tap((done < n) ? rpt : back);
         wait_frames(3);
         if (gun_n == g0)
           $display("  menu move not taken at panel frame %0d: tap again", frame_nr);
         else begin
            done = (done < n) ? done + (gun_n - g0) : done - (gun_n - g0);
            if (done > n)
              $display("  menu moved %0d past its target at panel frame %0d: tap back", done - n, frame_nr);
         end
         wait_frames(7);
      end
      if (done != n)
        $display("  FAIL: menu cursor %0d moves off its target", done - n);
   end
endtask

initial
   begin
      @(posedge dut.hresetn);
      usb_dev0.attach(usb_dev0.P_GAMEPAD);
      pad_set(PAD_IDLE);
      wait (frame_nr == PAD_T0);
      pad_press(PAD_START, 3); wait_frames(60);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_A, 3);     wait_frames(40);
      pad_press(PAD_B, 3);     wait_frames(60);
      menu_moves(PAD_UP, PAD_DOWN, 5);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_A, 3);     wait_frames(120);
      pad_press(PAD_UP, 60);   wait_frames(20);
      pad_press(PAD_A, 3);     wait_frames(20);
      pad_press(PAD_A, 3);     wait_frames(40);
      // Save in slot 0, back to the game, walk, load slot 0
      pad_press(PAD_START, 3); wait_frames(60);
      menu_moves(PAD_DOWN, PAD_UP, 4);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_A, 3);     wait_frames(80);
      menu_moves(PAD_DOWN, PAD_UP, 4);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_UP, 40);   wait_frames(30);
      pad_press(PAD_START, 3); wait_frames(60);
      menu_moves(PAD_UP, PAD_DOWN, 5);
      pad_press(PAD_A, 3);     wait_frames(60);
      pad_press(PAD_A, 3);
   end

// The game state the firmware reports (simulation build): the save's line
// and the load's must match after their prefixes
reg [8*80-1:0] tty_line, saved_line;
integer        tty_n, saves, loads, restores;
initial begin tty_line = 0; saved_line = 0; tty_n = 0; saves = 0; loads = 0; restores = 0; end
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0]) begin
     if (dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0] == 8'h0A) begin
        if (tty_line[8*80-1 -: 8*10] == "wolf save:") begin
           saves = saves + 1;
           saved_line = tty_line << (8*10);
        end else if (tty_line[8*80-1 -: 8*10] == "wolf load:") begin
           loads = loads + 1;
           if ((tty_line << (8*10)) == saved_line) restores = restores + 1;
           else $display("  FAIL: the loaded state differs from the saved one");
        end else if (tty_line[8*80-1 -: 8*6] == "sfx 4 ") begin
           gun_n = gun_n + 1;
        end else if (tty_line[8*80-1 -: 8*15] == "wolf load, bad ") begin
           loads = loads + 1;
           $display("  FAIL: the saved game's checksum does not match");
        end
        tty_line = 0;
        tty_n    = 0;
     end else if (tty_n < 80) begin
        tty_line[8*(79 - tty_n) +: 8] = dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0];
        tty_n = tty_n + 1;
     end
  end


// Bench interface of the card model
wire [3:0] spi_pad = {2'b11, SD_DAT0, SD_CMD};
reg        t_none;
reg  [2:0] t_sel;
reg        sd_dat0_oe;
reg        sd_dat0_v;

assign SD_DAT0 = sd_dat0_oe ? sd_dat0_v : 1'bz;

task dev_drive;
   input [3:0] oe;
   input [3:0] v;
   begin
      sd_dat0_oe = oe[1];
      sd_dat0_v  = v[1];
   end
endtask

`include "../../../software/apps/wolf4SDL/sim/sd_card_img.v"

always @(negedge SD_DAT3) sd_cs_assert;
always @(posedge SD_DAT3) sd_cs_release;
always @(posedge SD_CLK)  sd_sample;
always @(negedge SD_CLK)  sd_launch;

// TTY echo
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0])
    $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);

// Music on the build's default sound output (HDMI: the Belenos I2S input;
// panel: the Pmod I2S2, LRCK PMOD1[2], SCLK PMOD1[4], SDIN PMOD1[6]): the
// left sample of each frame (16 bits, MSB first, from the second SCLK edge
// after an LRCK change); frames and the peak since the previous panel frame
wire       a_sck = TB_HDMI ? dut.i2s_hdmi_bclk : PMOD1[4];
wire       a_lr  = TB_HDMI ? dut.i2s_hdmi_lrck : PMOD1[2];
wire       a_sd  = TB_HDMI ? dut.i2s_hdmi_sd   : PMOD1[6];
integer    a_pc, a_frames, a_peak, a_total;
reg        a_lr_q;
reg [15:0] a_sl;
initial begin a_pc = 0; a_frames = 0; a_peak = 0; a_total = 0; a_lr_q = 1'b1; a_sl = 16'h0; end
always @(posedge a_sck)
  begin
     if (a_lr !== a_lr_q) a_pc = 0;
     else                 a_pc = a_pc + 1;
     if ((a_pc >= 1) && (a_pc <= 16) && !a_lr) a_sl = {a_sl[14:0], a_sd};
     if (a_lr && (a_pc == 16))
       begin
          a_frames = a_frames + 1;
          a_total  = a_total + 1;
          if ($signed(a_sl) > a_peak)  a_peak = $signed(a_sl);
          if (-$signed(a_sl) > a_peak) a_peak = -$signed(a_sl);
       end
     a_lr_q = a_lr;
  end

// Panel frames
always @(panel.frame_done) if ($time > 0)
  begin
     if ((frame_nr % 20 == 0) || ((frame_nr >= PAD_T0) && (frame_nr % 10 == 0)))
       begin
          $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
          $writememh(fname, panel.fb);
       end
     $display("  panel frame %0d at %0t, SD commands %0d, audio %0d frames peak %0d", frame_nr, $time, sd_ncmd,
              a_frames, a_peak);
     a_frames = 0;
     a_peak   = 0;
     frame_nr = frame_nr + 1;
     if (frame_nr == WOLF_FRAMES) begin
        if (saves == 1 && loads == 1 && restores == 1)
          $display("  PASS: a game saved on the card and loaded back, its state restored");
        else begin
          $display("  FAIL: %0d save(s), %0d load(s), %0d restored (1, 1, 1 expected)", saves, loads, restores);
          error = error + 1;
        end
        stimulus_done = 1;
     end
  end

initial
   begin
      frame_nr = 0; t_none = 1'b0; t_sel = 3'd0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);
      $display("");
      $display(" Wolf3D port: shareware data on the microSD card model");
      $display("");
   end
