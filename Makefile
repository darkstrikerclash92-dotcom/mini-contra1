.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT is not set. Export PSL1GHT=$$PS3DEV")
endif

include $(PSL1GHT)/ppu_rules

TARGET      := mini-contra-dos
BUILD       := build
SOURCES     := source
INCLUDES    := include

TITLE       := Mini Contra Dos
APPID       := MCD000001
CONTENTID   := UP0001-$(APPID)_00-0000000000000000
SFOXML      := $(CURDIR)/sfo.xml

CFLAGS      = -O2 -Wall -Wextra -std=gnu99 -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS     = $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS        := -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export BUILDDIR := $(CURDIR)/$(BUILD)
export DEPSDIR  := $(BUILDDIR)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  -I$(CURDIR)/$(SOURCES) \
                  -I$(PSL1GHT)/ppu/include \
                  -I$(BUILDDIR)

export LIBPATHS := -L$(PSL1GHT)/ppu/lib

.PHONY: all clean pkg
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).fake.self $(OUTPUT).pkg $(OUTPUT).gnpdrm.pkg

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
