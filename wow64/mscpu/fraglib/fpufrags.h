/*++

Copyright (c) 1995 Microsoft Corporation

Module Name:

    fpufrags.h

Abstract:
    
    Prototypes for floating-point instruction fragments.

Author:

    11-Jul-1995 BarryBo, Created

Revision History:

--*/

FRAG0(FpuInit);
FRAG1(FpuSaveContext, BYTE);

FRAG0(F2XM1);
FRAG0(FABS);
FRAG1(FADD32, FLOAT);      // FADD m32real
FRAG1(FADD64, DOUBLE);     // FADD m64real
FRAG1IMM(FADD_STi_ST, INT); // FADD ST(i), ST = add ST to ST(i)
FRAG1IMM(FADD_ST_STi, INT); // FADD ST, ST(i) = add ST(i) to ST
FRAG1IMM(FADDP_STi_ST, INT); // FADDP ST(i), ST = add ST to ST(i) and pop ST
FRAG1(FIADD16, USHORT);   // FIADD m16int
FRAG1(FIADD32, ULONG);    // FIADD m32int
FRAG1(FBLD, BYTE);
FRAG1(FBSTP, BYTE);
FRAG0(FCHS);
FRAG0(FNCLEX);
FRAG1(FCOM32, FLOAT);  // FCOM m32real
FRAG1(FCOM64, DOUBLE); // FCOM m64real
FRAG1IMM(FCOM_STi, INT); // FCOM ST(i)
FRAG1(FCOMP32, FLOAT); // FCOMP m32real
FRAG1(FCOMP64, DOUBLE); // FCOMP m64real
FRAG1IMM(FCOMP_STi, INT); // FCOMP ST(i)
FRAG0(FCOMPP);
FRAG0(FCOS);
FRAG0(FDECSTP);
FRAG1(FDIV32, FLOAT);  // FDIV m32real
FRAG1(FDIV64, DOUBLE); // FDIV m64real
FRAG1IMM(FDIV_ST_STi, INT); // FDIV ST, ST(i)
FRAG1IMM(FDIV_STi_ST, INT); // FDIV ST(i), ST
FRAG1(FIDIV16, USHORT); // FIDIV m16int
FRAG1(FIDIV32, ULONG);   // FIDIV m32int
FRAG1IMM(FDIVP_STi_ST, INT);    // FDIVP ST(i), ST
FRAG1(FDIVR32, FLOAT);     // FDIVR m32real
FRAG1(FDIVR64, DOUBLE);    // FDIVR m64real
FRAG1IMM(FDIVR_ST_STi, INT); // FDIVR ST, ST(i)
FRAG1IMM(FDIVR_STi_ST, INT); // FDIVR ST(i), ST
FRAG1IMM(FDIVRP_STi_ST, INT); // FDIVRP ST(i)
FRAG1(FIDIVR16, USHORT);  // FIDIVR m16int
FRAG1(FIDIVR32, ULONG);   // FIDIVR m32int
FRAG1IMM(FFREE, INT);
FRAG1(FICOM16, USHORT);   // FICOM m16int (Intel docs say m16real);
FRAG1(FICOM32, ULONG);    // FICOM m32int (Intel docs say m32real);
FRAG1(FICOMP16, USHORT);  // FICOMP m16int
FRAG1(FICOMP32, ULONG);   // FICOMP m32int
FRAG1(FILD16, SHORT);    // FILD m16int
FRAG1(FILD32, LONG);     // FILD m32int
FRAG1(FILD64, LONGLONG); // FILD m64int
FRAG0(FINCSTP);
FRAG0(FNINIT);
FRAG1(FIST16, SHORT);     // FIST m16int
FRAG1(FISTP16, SHORT);    // FISTP m16int
FRAG1(FIST32, LONG);      // FIST m32int
FRAG1(FISTP32, LONG);     // FISTP m32int
FRAG1(FIST64, LONGLONG);  // FIST m64int
FRAG1(FISTP64, LONGLONG); // FISTP m64int
FRAG1(FLD32, FLOAT);       // FLD m32real
FRAG1(FLD64, DOUBLE);      // FLD m64real
FRAG1(FLD80, BYTE);        // FLD m80real
FRAG0(FLD1);
FRAG0(FLDL2T);
FRAG0(FLDL2E);
FRAG0(FLDPI);
FRAG0(FLDLG2);
FRAG0(FLDLN2);
FRAG1IMM(FLD_STi, INT);
FRAG0(FLDZ);
FRAG1(FLDCW, USHORT*);
FRAG1(FLDENV, BYTE);
FRAG1(FMUL32, FLOAT);      // FMUL m32real
FRAG2(FMUL64, DOUBLE);     // FMUL m64real
FRAG1IMM(FMUL_STi_ST, INT); // FMUL ST(i), ST
FRAG1IMM(FMUL_ST_STi, INT); // FMUL ST, ST(i)
FRAG1IMM(FMULP_STi_ST, INT);    // FMULP ST(i), ST
FRAG1(FIMUL16, USHORT);      // FIMUL m16int
FRAG1(FIMUL32, ULONG);       // FIMUL m32int
FRAG0(FPATAN);
FRAG0(FPREM);
FRAG0(FPREM1);
FRAG0(FPTAN);
FRAG0(FRNDINT);
FRAG1(FRSTOR, BYTE);
FRAG1(FNSAVE, BYTE);
FRAG0(FSCALE);
FRAG0(FSIN);
FRAG0(FSINCOS);
FRAG0(FSQRT);
FRAG1(FST32, FLOAT);       // FST m32real
FRAG1(FSTP32, FLOAT);      // FSTP m32real
FRAG1(FST64, DOUBLE);      // FST m64real
FRAG1(FSTP64, DOUBLE);     // FSTP m64real
FRAG1(FSTP80, BYTE);       // FSTP m80real
FRAG1IMM(FST_STi, INT);      // FST ST(i)
FRAG1IMM(FSTP_STi, INT);     // FSTP ST(i)
FRAG0(OPT_FSTP_ST0);     // FSTP ST(0)
FRAG1(FNSTCW, USHORT);
FRAG1(FNSTENV, BYTE);
FRAG1(FNSTSW, USHORT);
FRAG0(OPT_FNSTSWAxSahf);    // FNSTSW AX, SAHF
FRAG1(FSUB32, FLOAT);      // FSUB m32real
FRAG1(FSUBP32, FLOAT);     // FSUBP m32real
FRAG1(FSUB64, DOUBLE);     // FSUB m64real
FRAG1(FSUBP64, DOUBLE);    // FSUBP m64real
FRAG1IMM(FSUB_ST_STi, INT);   // FSUB ST, ST(i)
FRAG1IMM(FSUB_STi_ST, INT);  // FSUB ST(i), ST
FRAG1IMM(FSUBP_STi_ST, INT); // FSUBP ST(i), ST
FRAG1(FISUB16, USHORT);   // FISUB m16int
FRAG1(FISUB32, ULONG);    // FISUB m64int
FRAG1(FSUBR32, FLOAT);     // FSUBR m32real
FRAG1(FSUBR64, DOUBLE);    // FSUBR m64real
FRAG1IMM(FSUBR_ST_STi, INT); // FSUBR ST, ST(i)
FRAG1IMM(FSUBR_STi_ST, INT); // FSUBR ST(i), ST
FRAG1IMM(FSUBRP_STi_ST, INT); // FSUBRP ST(i)
FRAG1(FISUBR16, USHORT);
FRAG1(FISUBR32, ULONG);
FRAG0(FTST);
FRAG1IMM(FUCOM, INT);        // FUCOM ST(i) / FUCOM
FRAG1IMM(FUCOMP, INT);       // FUCOMP ST(i) / FUCOMP
FRAG0(FUCOMPP);
FRAG0(FXAM);
FRAG1IMM(FXCH_STi, INT);
FRAG0(FXTRACT);
FRAG0(FYL2X);
FRAG0(FYL2XP1);
FRAG0(WaitFrag);
FRAG0(FNOP);
