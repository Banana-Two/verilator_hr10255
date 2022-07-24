module typical_example(f,d,e,a,b,ci,CK,D,RN,SN,input_x,enable,co,sum,Q,QN,CO,S,output_x);
   inout d;
   inout e;
   inout [3:0] f;
   input a;
   input b;
   input ci;
   input CK;
   input D;
   input RN;
   input SN;
   input [4:0]input_x;
   input [4:0]enable;
   output co;
   output sum;
   output Q;
   output QN;
   output CO;
   output S;
   output [4:0]output_x;
   wire [13:0]C;
  // Top port is input connecting to an ins inout: the ins inout is used as
  // input.
  // PADBID PADBID_U1 (.PAD(input_x[0]), .I(a), .OEN(b), .C(C[1]));

  // Top port is output connecting to an ins inout: the ins inout is used as
  // output.
  PADBID PADBID_U0 (.PAD(Q), .I(a), .OEN(b), .C(C[0]));

  // Top port is inout connecting to an ins inout and an ins input: the ins
  // inout and top inout are both used as inout.
  PADBID PADBID_U8 (.PAD(e), .I(), .OEN(e), .C());
  // Top port is inout connecting to an ins inout: the ins inout and top
  // inout are both used as inout.
  PADBID PADBID_U2 (.PAD(d), .I(a), .OEN(b), .C(C[2]));
  // Top port is inout connecting to multiple ins inouts: all inouts
  // including top inout are used as input.
  PADBID PADBID_U9 (.PAD(f[1]), .I(a), .OEN(b), .C());
  PADBID PADBID_U10 (.PAD(f[1]), .I(a), .OEN(b), .C());
  PADBID PADBID_U11 (.PAD(f[1]), .I(a), .OEN(b), .C());
  // Top port is inout connecting to an ins inout and an ins output: the ins
  // inout is used as input and the top inout is used as inout.
  PADBID PADBID_U12 (.PAD(f[2]), .I(a), .OEN(b), .C(f[3]));
  PADBID PADBID_U13 (.PAD(f[3]), .I(a), .OEN(b), .C());

  // Top port is wire connection to an ins inout and an ins input: the ins
  // inout is used as output.
  PADBID PADBID_U4 (.PAD(C[6]), .I(a), .OEN(b), .C(C[5]));
  PADBID PADBID_U7 (.PAD(C[7]), .I(C[6]), .OEN(b), .C(C[4]));
  // Top port is wire connecting to an ins inout and an ins output: the ins
  // inout is used as input.
  PADBID PADBID_U3 (.PAD(C[1]), .I(a), .OEN(b), .C(C[3]));
  PADBID PADBID_U5 (.PAD(C[3]), .I(a), .OEN(b), .C(C[8]));
  // Top port is wire connecting to two ins inouts: both ins
  // inouts are used as inout.
  PADBID PADBID_U6 (.PAD(C[9]), .I(a), .OEN(b), .C(C[13]));
  PADBID PADBID_U14 (.PAD(C[9]), .I(a), .OEN(b), .C(C[13]));
  PADBID PADBID_U15 (.PAD(C[10]), .I(a), .OEN(b), .C(C[13]));
  DFFRS_X1 U0_D (.CK(CK), .D(D), .RN(RN), .SN(SN), .Q(Q), .QN(QN));
  FA_X1 U1 (.A(a), .B(b), .CI(ci), .CO(CO), .S(S));
  AON_BUF_X1 assignment_assignsIndex1 (.A(output_x[3]), .Z(output_x[4]));
  INV_X1_LVT U1_i_1_0 (.A(enable[3]), .ZN(U1_n_0));
  TBUF_X1_LVT U1_i_0 (.A(input_x[3]), .EN(U1_n_0), .Z(output_x[3]));
  INV_X1_LVT U2_i_1_0 (.A(enable[4]), .ZN(U2_n_0));
  TBUF_X1_LVT U2_i_0 (.A(input_x[4]), .EN(U2_n_0), .Z(output_x[3]));
  OR3_X1_LVT U0_i_3_1_0 (.A1(enable[0]), .A2(enable[1]), .A3(enable[2]), .ZN(
      U0_n_3_0));
  INV_X1_LVT U0_i_3_0 (.A(U0_n_3_0), .ZN(U0_n_0));
  TBUF_X1_LVT U0_i_2 (.A(input_x[2]), .EN(U0_n_0), .Z(output_x[2]));
  TBUF_X1_LVT U0_i_1 (.A(input_x[1]), .EN(U0_n_0), .Z(output_x[1]));
  TBUF_X1_LVT U0_i_0 (.A(input_x[0]), .EN(U0_n_0), .Z(output_x[0]));
  INV_X1_LVT U1_i_0_0 (.A(a), .ZN(U1_n_0_0));
  INV_X1_LVT U1_i_0_2 (.A(ci), .ZN(U1_n_0_2));
  OAI222_X1_LVT U1_i_0_3 (.A1(U1_n_0_0), .A2(U1_n_0_1), .B1(U1_n_0_1), .B2(
      U1_n_0_2), .C1(U1_n_0_0), .C2(U1_n_0_2), .ZN(co));
  XNOR2_X1_LVT U0_i_0_0 (.A(a), .B(ci), .ZN(U0_n_0_0));
  XNOR2_X1_LVT U0_i_0_1 (.A(U0_n_0_0), .B(b), .ZN(sum));
  INV_X1_LVT U1_i_0_i0 (.A(1'b0), .ZN(U1_n_0_i0));
  INV_X1_LVT U1_i_0_i1 (.A(1'b1), .ZN(U1_n_0_i1));
  INV_X1_LVT U1_i_0_ix (.A(1'bx), .ZN(U1_n_0_ix));
  INV_X1_LVT U1_i_0_iz (.A(1'bz), .ZN(U1_n_0_iz));
endmodule

