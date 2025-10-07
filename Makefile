CC               = gcc
SOURCEDIR	 = ./ 
INCLUDEDIR       = ./
BUILDDIR         = ./schas
PATH            +=
CFLAGS          := -O2 -W -Wall -Wextra -ggdb #-Wno-unused #-Werror -pedantic 
CPPFLAGS        := -I$(INCLUDEDIR)
LDFLAGS         := -lstar-api -lstar_conf_api_generic -lstar_conf_api_mk2 -lstar_conf_api_brick_mk2 -lstar_conf_api_brick_mk3 -lstar_conf_api_pcie_mk2 -lstar-api -lrmap_packet_library -lpthread
SOURCES         := $(wildcard *.c)
#SOURCES         := $(wildcard *.c)
OBJECTS         := $(patsubst %.c, $(BUILDDIR)/%.o, $(subst $(SOURCEDIR)/,, $(SOURCES)))
TARGET          := spw_bridge 

DEBUG?=1
ifeq  "$(shell expr $(DEBUG) \> 1)" "1"
	    CFLAGS += -DDEBUGLEVEL=$(DEBUG)
else
	    CFLAGS += -DDEBUGLEVEL=1
endif


#all: builddir $(OBJECTS) $(BUILDDIR)/$(TARGET)
#	$(CC) $(CPPFLAGS) $(CFLAGS)  $< -o $@
#
#builddir:
#	mkdir -p $(BUILDDIR)
#
#clean:
#	 rm -f $(BUILDDIR)/{$(TARGET), $(OBJECTS)}
#	 rm -rf $(BUILDDIR)
#
#
#$(BUILDDIR)/$(TARGET): $(OBJECTS)
#	$(CC) $^ -o $@
#
#$(OBJECTS): $(SOURCES)
#	$(CC) $(CPPFLAGS) $(CFLAGS) -c $^ -o $@

all: $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $(TARGET)


