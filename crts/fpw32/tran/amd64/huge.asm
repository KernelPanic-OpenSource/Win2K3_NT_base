	page	,132
	title   HUGE    - HUGE value
;*** 
;huge.asm - defines HUGE
;
;	Copyright (c) 1984-2000, Microsoft Corporation.  All rights reserved.
;
;Purpose:
;	defines HUGE
;
;Revision History:
;
;   07/04/84	Greg Whitten
;		initial version
;
;   12/21/84	Greg Whitten
;		add assumes so that C can find variable
;
;   09/23/87	Barry C. McCord
;		add _matherr_flag for the sake of the
;		C floating-point intrinsic functions
;
;   08/29/88	Bill Johinston
;		386 version
;
;   08/27/91	JeffRob
;		ANSI naming
;
;   09/06/91	GeorgioP
;		define HUGE as positive infinity
;
;   09/06/91	GeorgioP
;		define _HUGE_dll
;
;   04/05/93	SteveSa
;		undefine _HUGE_dll
;   10/14/93	GregF
;		Restored _HUGE_DLL for _NTSDK
;
;*******************************************************************************

_DATA$00 SEGMENT PARA PUBLIC 'DATA'
        align   8

ifdef	_NTSDK
ifdef	CRTDLL
public	_HUGE_dll
_HUGE_dll dq	7ff0000000000000R
else
public	_HUGE
_HUGE	dq	7ff0000000000000R
endif
else
public	_HUGE
_HUGE	dq	7ff0000000000000R
endif

_DATA$00 ends

	end
