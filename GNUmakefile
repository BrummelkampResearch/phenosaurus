# makefile for screen-analyzer

.PHONY: firstTarget
firstTarget: all

-include make.config

make.config:
	@ echo "No make.config file, writing a default one"
	@ echo "ZEEP_DIR = ../libzeep # please specify"   > make.config
	@ echo ""                                        >> make.config
	@ echo "ZEEP_INCL = \$$(ZEEP_DIR:%=%/include)"   >> make.config
	@ echo "ZEEP_LIB = \$$(ZEEP_DIR:%=%/lib)"        >> make.config
	@ echo ""                                        >> make.config
	@ echo "INCLUDE_DIR += \$$(ZEEP_INCL)"           >> make.config
	@ echo "LIBRARY_DIR += \$$(ZEEP_LIB)"            >> make.config
	@ echo
	@ echo "The default make.config now contains:"
	@ cat make.config

MRC				?= mrc

ifeq (, $(shell which $(MRC)))
$(warning "")
$(warning "The executable mrc is not found in your path.")
$(warning "Please consider installing it to enable built-in data files.")
$(warning "See https://github.com/mhekkel/mrc")
$(warning "")
else
	DEFINES			+= USE_RSRC
endif

CPU			= $(shell uname -m)
OS			= $(shell uname -o)
SYSLIBDIR	= /usr/lib

ifeq "$(OS)" "GNU/Linux"
	SYSLIBDIR	:= $(SYSLIBDIR)/$(CPU)-linux-gnu
endif

BOOST_LIB_DIR		?= $(SYSLIBDIR)

PACKAGES			=
WARNINGS			= all no-multichar no-unknown-pragmas no-deprecated-declarations

RANLIB				?= ranlib
SVN					?= svn
PROCESSOR			?= $(shell uname -p)

ifneq ($(PACKAGES),)
CFLAGS				+= $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PACKAGES))
LDFLAGS				+= $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PACKAGES) --static )
endif

CFLAGS				+= -std=c++17 -pthread
CFLAGS				+= -ffunction-sections -fdata-sections
CFLAGS				+= $(addprefix -I, $(INCLUDE_DIR))
LDFLAGS				+= -Wl,--gc-sections -pthread # -static-libstdc++ -static-libgcc 

BOOST_LIBS			= date_time iostreams program_options filesystem thread math_c99 math_c99f timer chrono system regex random
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
BOOST_LIBS			:= $(BOOST_LIBS:%=$(BOOST_LIB_DIR)/lib%.a)

ZEEP_LIBS			?= rest webapp http xml el generic
ZEEP_LIBS			:= $(ZEEP_LIBS:%=-lzeep-%)

LIBS				+= m rt stdc++fs
LIBS				:= $(LIBS:%=-l%)
LIBS				+= $(ZEEP_LIBS) $(BOOST_LIBS) -lz -lbz2 -lpthread 

# generic defines

CFLAGS				+= $(addprefix -W, $(WARNINGS))
CFLAGS				+= $(addprefix -D, $(DEFINES))
CFLAGS				+= -g

#LDFLAGS				+= $(LIBRARY_DIR:%=-L %) $(LIBS:%=-l%) -g
LDFLAGS				+= $(LIBRARY_DIR:%=-L %) $(LIBS) -g
LDFLAGS				+= -Wl,-rpath=$(CLIB)

OBJDIR				= obj

ifneq ($(DEBUG),1)
CFLAGS				+= -O3 -ffunction-sections -fdata-sections -DNDEBUG -g
LDFLAGS				+= -static
else
CFLAGS				+= -DDEBUG 
OBJDIR				:= $(OBJDIR).dbg
endif

ifeq ($(PROFILE),1)
CFLAGS				+= -pg
LDFLAGS				+= -pg
OBJDIR				:= $(OBJDIR).profile
endif

SOURCE_DIRS			= ./src/

empty = 
space = $(empty) $(empty)
join-with = 
SRC_VPATH = $(subst $(space),:,$(SOURCE_DIRS))

VPATH += $(SRC_VPATH)

PROGRAMS = screen-analyzer

OBJECTS = \
	$(OBJDIR)/adjust.o \
	$(OBJDIR)/bowtie.o \
	$(OBJDIR)/fisher.o \
	$(OBJDIR)/refseq.o \
	$(OBJDIR)/screen-analyzer.o \
	$(OBJDIR)/screendata.o \
	$(OBJDIR)/screenserver.o \
	$(OBJDIR)/utils.o

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< $(CFLAGS) $(CXXFLAGS)

-include $(OBJECTS:%.o=%.d) $(PROGRAMS:%=$(OBJDIR)/%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

FORCE:

REVISION = $(shell LANG=C $(SVN) info | tr -d '\n' | sed -e's/.*Revision: \([[:digit:]]*\).*/\1/' )
REVISION_FILE = version-info-$(REVISION).txt

$(REVISION_FILE):
	LANG=C $(SVN) info > $@

rsrc/version.txt: $(REVISION_FILE)
	cp $? $@

RSRC = rsrc/version.txt rsrc/ncbi-genes-hg19.txt rsrc/ncbi-genes-hg38.txt
ifneq ($(DEBUG),1)
RSRC += docroot/
endif

src/mrsrc.h:
	$(MRC) --header > $@

# yarn rules
SCRIPTS = $(shell find webapp -name '*.js')
WEBAPP_FILES = $(SCRIPTS)
# SCRIPT_FILES = $(SCRIPTS:webapp/%.js=docroot/scripts/%.js)
SCRIPT_FILES = docroot/scripts/index.js docroot/scripts/screen.js

ifneq ($(DEBUG),1)
WEBPACK_OPTIONS = --env.PRODUCTIE
endif

$(subst .,%,$(SCRIPT_FILES)): $(subst .,%,$(WEBAPP_FILES))
	yarn webpack $(WEBPACK_OPTIONS)

$(OBJDIR)/sa_rsrc.o: $(RSRC) $(SCRIPT_FILES) src/mrsrc.h
	$(MRC) -o $@ $(RSRC)

webappscripts: $(SCRIPT_FILES)

screen-analyzer: $(OBJDIR)/screen-analyzer.o $(OBJDIR)/screendata.o $(OBJDIR)/refseq.o \
		$(OBJDIR)/bowtie.o $(OBJDIR)/fisher.o $(OBJDIR)/screenserver.o $(OBJDIR)/utils.o $(OBJDIR)/sa_rsrc.o
	@ echo '->' $@
	@ $(CXX) -o $@ $^ $(LDFLAGS)

.PHONY: clean all
clean:
	rm -rf $(PROGRAMS) $(OBJDIR)/* $(REVISION_FILE)

all: $(PROGRAMS)

.PHONY: help
help:
	@ echo $(MAKE_VERSION)
