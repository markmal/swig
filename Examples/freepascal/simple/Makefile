TOP        = ../..
SWIGEXE       = $(TOP)/../swig
SWIG_LIB_DIR = $(TOP)/../$(TOP_BUILDDIR_TO_TOP_SRCDIR)Lib
CXXSRCS    = example.c
TARGET     = example
WTARGET     = example_wrap
INTERFACE  = example.i
SWIGOPT    =
FREEPASCALSRCS = example.pas
FREEPASCALFLAGS= 

all::	freepascal

freepascal::
	$(MAKE) -f $(TOP)/Makefile CXXSRCS='$(CXXSRCS)' SWIGEXE='$(SWIGEXE)' SWIG_LIB_DIR='$(SWIG_LIB_DIR)' \
	SWIGOPT='$(SWIGOPT)' TARGET='$(TARGET)' WTARGET='$(WTARGET)' INTERFACE='$(INTERFACE)' freepascal_cpp
#	$(MAKE) -f $(TOP)/Makefile freepascal_cpp_run
	$(MAKE) -f $(TOP)/Makefile FREEPASCALSRCS='$(FREEPASCALSRCS)' freepascal_compile
	$(MAKE) -f $(TOP)/Makefile FREEPASCALSRCS='runme.pp' freepascal_compile
	$(MAKE) -f $(TOP)/Makefile FREEPASCAL_RUNME='./runme' freepascal_run

clean::
	$(MAKE) -f $(TOP)/Makefile freepascal_clean

check: all