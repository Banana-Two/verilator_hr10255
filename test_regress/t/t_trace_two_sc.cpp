// DESCRIPTION: Verilator: Verilog Test
//
// Copyright 2003-2020 by Wilson Snyder. This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.

#include "verilatedos.h"
#include VM_PREFIX_INCLUDE
#include "Vt_trace_two_b.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// Compile in place
#include "Vt_trace_two_b.cpp"
#include "Vt_trace_two_b__Syms.cpp"
#include "Vt_trace_two_b__Trace.cpp"
#include "Vt_trace_two_b__Trace__Slow.cpp"

// General headers
#include "verilated.h"
#include "systemc.h"
#include "verilated_vcd_sc.h"

VM_PREFIX* ap;
Vt_trace_two_b* bp;

int sc_main(int argc, char** argv) {
    sc_signal<bool> clk;
    sc_time sim_time(1100, SC_NS);
    Verilated::commandArgs(argc, argv);
    Verilated::debug(0);
    srand48(5);
    ap = new VM_PREFIX("topa");
    bp = new Vt_trace_two_b("topb");
    ap->clk(clk);
    bp->clk(clk);

#if VM_TRACE
    Verilated::traceEverOn(true);
    VerilatedVcdSc* tfp = new VerilatedVcdSc;
    ap->trace(tfp, 99);
    bp->trace(tfp, 99);
    tfp->open(VL_STRINGIFY(TEST_OBJ_DIR) "/simx.vcd");
#endif
    {
        clk = false;
#if (SYSTEMC_VERSION>=20070314)
        sc_start(10, SC_NS);
#else
        sc_start(10);
#endif
    }
    while (sc_time_stamp() < sim_time && !Verilated::gotFinish()) {
        clk = !clk;
#if (SYSTEMC_VERSION>=20070314)
        sc_start(5, SC_NS);
#else
        sc_start(5);
#endif
    }
    if (!Verilated::gotFinish()) {
        vl_fatal(__FILE__, __LINE__, "main", "%Error: Timeout; never got a $finish");
    }
    ap->final();
    bp->final();
#if VM_TRACE
    if (tfp) tfp->close();
#endif  // VM_TRACE

    VL_DO_DANGLING(delete ap, ap);
    VL_DO_DANGLING(delete bp, bp);
    exit(0L);
}
