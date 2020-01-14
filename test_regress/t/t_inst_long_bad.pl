#!/usr/bin/perl
if (!$::Driver) { use FindBin; exec("$FindBin::Bin/bootstrap.pl", @ARGV, $0); die; }
# DESCRIPTION: Verilator: Verilog Test driver/expect definition
#
# Copyright 2003 by Wilson Snyder. This program is free software; you can
# redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.

use IO::File;
use strict;
use vars qw($Self);

scenarios(vlt => 1);

my $length = 200;
my $long = "long_" x (($length + 4) / 5);

sub gen_top {
    my $filename = shift;

    my $fh = IO::File->new(">$filename")
        or $Self->error("Can't write $filename");
    $fh->print("// Generated by t_inst_long.pl\n");
    $fh->print("module t;\n");
    $fh->print("\n");
    $fh->print("  ${long} inst ();\n");
    $fh->print("\n");
    $fh->print("endmodule\n");
    $fh->close;
}

sub gen_sub {
    my $filename = shift;

    my $fh = IO::File->new(">$filename")
        or $Self->error("Can't write $filename");
    $fh->print("// Generated by t_inst_long.pl\n");
    $fh->print("module ${long};\n");
    $fh->print("\n");
    $fh->print("  initial begin\n");
    $fh->print("     \$write(\"*-* All Finished *-*\\n\");\n");
    $fh->print("     \$finish;\n");
    $fh->print("  end\n");
    $fh->print("endmodule\n");
    $fh->close;
}

top_filename("$Self->{obj_dir}/t_inst_long.v", $long);

gen_top($Self->{top_filename});
gen_sub("$Self->{obj_dir}/${long}.v");

lint(
    fails => 1,
    expect_filename => $Self->{golden_filename},
    );

ok(1);
1;
