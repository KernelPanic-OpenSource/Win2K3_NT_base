BINPLACE_PLACEFILE=$(PROJECT_ROOT)\cluster\resdll\vsstask\placefil.txt
MSC_WARNING_LEVEL=/W4 /WX

C_DEFINES=$(C_DEFINES) -DSTRSAFE_LIB

MAJORCOMP=cluster

!IF !DEFINED(NO_SPECIAL_DEBUG)
!IF DEFINED(USE_DEBUG_CRTS)
DEBUG_CRTS=1
!ENDIF
!IF !$(FREEBUILD)
RCOPTIONS=$(RCOPTIONS) -D_DEBUG
!ENDIF
!ENDIF