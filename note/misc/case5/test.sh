#########################################################################
# File Name: test.sh
# Author: 16hxliang3
# mail: 16hxliang3@stu.edu.cn
# Created Time: Sun 05 Dec 2021 04:58:54 PM CST
#########################################################################
#This a simple example whose top module has a inout.
#Ast.
#!/bin/bash
../../../bin/verilator -Wno-implicit top_has_inout.v ../LibBlackbox.v --xml-only
hier=`diff -bqBH HierNetlist.v standard/StandardHierNetlist.v`
flat=`diff -bqBH FlatNetlist.v standard/StandardFlatNetlist.v`
[ $hier ] && echo "In case5,$hier." && error=true
[ $flat ] && echo "In case5,$flat." && error=true
