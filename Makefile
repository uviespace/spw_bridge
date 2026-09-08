CC		= gcc
TARGET		:= spw_bridge
BUILDDIR	:= build
SOURCEDIR	:= ./
INCLUDEDIR	:= ./

SOURCES		:= $(wildcard $(SOURCEDIR)*.c) $(wildcard ecss/*.c)
OBJECTS		:= $(patsubst %.c,$(BUILDDIR)/%.o,$(notdir $(SOURCES)))
DEPS		:= $(OBJECTS:.o=.d)

CPPFLAGS	:= -I$(INCLUDEDIR) -Iecss
CFLAGS		:= -O2 -g -W -Wall -Wextra -Werror -ggdb \
		   -Wconversion -Wsign-conversion -Wshadow \
		   -Wfloat-equal -Wcast-align
LDFLAGS		:=
LIBS		:= -lstar-api \
		   -lrmap_packet_library \
		   -lstar_conf_api_generic \
		   -lstar_conf_api_mk2 \
		   -lstar_conf_api_brick_mk2 \
		   -lstar_conf_api_brick_mk3 \
		   -lstar_conf_api_pcie_mk2 \
		   -lpthread

DEBUG?=1
ifeq "$(shell expr $(DEBUG) \> 1)" "1"
	CFLAGS += -DDEBUGLEVEL=$(DEBUG)
else
	CFLAGS += -DDEBUGLEVEL=1
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILDDIR)/%.o: $(SOURCEDIR)%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: ecss/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean