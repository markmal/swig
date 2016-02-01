TOP        = ../..
SWIGEXE       = $(TOP)/../swig
SWIG_LIB_DIR = $(TOP)/../$(TOP_BUILDDIR_TO_TOP_SRCDIR)Lib
SRCS    = example.c
TARGET     = example
INTERFACE  = example.i
SWIGOPT    =
FREEPASCALSRCS = example.pas
FREEPASCALFLAGS= 

all::	freepascal

freepascal::
	$(MAKE) -f $(TOP)/Makefile SRCS='$(SRCS)' SWIGEXE='$(SWIGEXE)' SWIG_LIB_DIR='$(SWIG_LIB_DIR)' \
	SWIGOPT='$(SWIGOPT)' TARGET='$(TARGET)' INTERFACE='$(INTERFACE)' freepascal_cpp
#	$(MAKE) -f $(TOP)/Makefile freepascal_cpp_run
	$(MAKE) -f $(TOP)/Makefile FREEPASCALSRCS='$(FREEPASCALSRCS)' freepascal_compile
	$(MAKE) -f $(TOP)/Makefile FREEPASCALSRCS='runme.pp' freepascal_compile
	$(MAKE) -f $(TOP)/Makefile FREEPASCAL_RUNME='./runme' freepascal_run

clean::
	$(MAKE) -f $(TOP)/Makefile freepascal_clean

check: all