#########################################################################
# File Name: test.sh
# Author: 16hxliang3
# mail: 16hxliang3@stu.edu.cn
# Created Time: Sun 05 Dec 2021 04:58:54 PM CST
#########################################################################
#!/bin/bash
../../../bin/verilator -Wno-implicit has_bigger_constant_value.v ../LibBlackbox.v --xml-only
hier=`diff -bqBH HierNetlist.v standard/StandardHierNetlist.v`
flat=`diff -bqBH FlatNetlist.v standard/StandardFlatNetlist.v`
[ $hier ] && echo "In case6,$hier." && error=true
[ $flat ] && echo "In case6,$flat." && error=true
