// delaopnt -- operator delete[](void *, nothrow_t) REPLACEABLE
#define _USE_ANSI_CPP // suppress defaultlib directive for Std C++ Lib
#include <new>

extern void __cdecl operator delete[](void *ptr) _THROW0();

void __cdecl operator delete[](void *ptr,
	const std::nothrow_t&) _THROW0()
	{	// free an allocated object
	operator delete[](ptr);
	}

/*
* Copyright (c) 1992-2001 by P.J. Plauger.  ALL RIGHTS RESERVED.
 * Consult your license regarding permissions and restrictions.
 V3.10:0009 */
