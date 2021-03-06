/*
 * @(#)_rawstack.cxx 1.0 3/30/98
 *
*  Copyright (C) 1998,1999 Microsoft Corporation. All rights reserved. *
 */
#include "stdinc.h"
#include "core.hxx"
#pragma hdrstop

#include "_rawstack.hxx"

//===========================================================================
RawStack::RawStack(long entrySize, long growth)
    : _lEntrySize(entrySize), _lGrowth(growth), _pStack(NULL), _ncUsed(0), _ncSize(0)
{
}

RawStack::~RawStack()
{
    if (_pStack != NULL)
    {
        delete _pStack;
        _pStack = NULL;
    }            
}

char*
RawStack::__push()
{
    // No magic object construction -- user has to do this.
    // NTRAID#NTBUG9 - 571792 - jonwis - 2002/04/25 - Dead code removal
#ifdef FUSION_USE_OLD_XML_PARSER_SOURCE
	char* newStack = new_ne char[_lEntrySize * ( _ncSize + _lGrowth) ];
#else
	char* newStack = NEW (char[_lEntrySize * ( _ncSize + _lGrowth) ]);
#endif
    if (newStack == NULL)
    {
        return NULL;
    }
    ::memset(newStack, 0, _lEntrySize * (_ncSize + _lGrowth));
    if (_ncUsed > 0)
    {
        ::memcpy(newStack, _pStack, _lEntrySize * _ncUsed);
    }
    _ncSize += _lGrowth;
    delete _pStack;
    _pStack = newStack;

    return &_pStack[_lEntrySize * _ncUsed++];
}
