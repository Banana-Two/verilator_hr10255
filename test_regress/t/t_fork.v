// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2003 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

module t (/*AUTOARG*/);

   initial begin
      // With no statements this is a NOP
      fork
      join
      fork
      join_any
      fork
      join_none
      // With one statement this is supported and optimized to a begin/end
      fork : fblk
         begin
            $write("*-* All Finished *-*\n");
            $finish;
         end
      join : fblk
   end

endmodule
