/*++

Copyright (c) 1990-1999 Microsoft Corporation

Module Name:

    contain.cxx

Abstract:

    This module contains the definition for the CONTAINER class, the most
    primitive, abstract class in the container sub-hierarchy. Given it's
    abstract, prmitive nature there is minimal implementation at this point
    in the hierarchy.

Environment:

    ULIB, User Mode

--*/
#include <pch.cxx>

#define _ULIB_MEMBER_

#include "ulib.hxx"
#include "contain.hxx"


DEFINE_CONSTRUCTOR( CONTAINER, OBJECT );

CONTAINER::~CONTAINER(
    )
{
}
