FUSION_LIB_DEST = $(FUSION_WIN_LIB_DEST)
C_DEFINES = $(C_DEFINES) -DFUSION_WIN=1
C_DEFINES = $(C_DEFINES) -DFUSION_NO_WRAPPERS=1 -DFUSION_ENABLE_UNWRAPPED_NEW=1 -DFUSION_ENABLE_UNWRAPPED_DELETE=1
FUSION_BUILD_TYPE_PREFIX=win
FUSION_INC_PATH=$(FUSION_ALL_INC_PATH);$(FUSION_WIN_INC_PATH)
!undef USE_RTTI
FUSION_LIB_PATH = $(FUSION_WIN_LIB_PATH)
!if !defined(FUSION_BUILD_LEAF_DIRECTORY)
FUSION_BUILD_LEAF_DIRECTORY=whistler
!endif
FUSION_SLIST_LIB=