//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    free_run
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : free_run.v
// Module Description : arvern FPGA free-running stimulus for the virtual board
//                      (./run <app> -screen -free): any firmware runs with the
//                      RGB panel model of the bench and no check. The simulation
//                      ends when the window is closed.
//----------------------------------------------------------------------------

initial
   begin
      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA FREE RUN (virtual board; close it to stop)       |");
      $display(" ====================================================================");
      $display("");
   end
