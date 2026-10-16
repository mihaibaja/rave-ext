ifndef BUILD_DIR
  $(error Need to define variable BUILD_DIR)
endif
ifndef RVV
  $(error Need to define variable RVV)
endif
ifndef LLVM_DIR
  $(error Need to provide variable LLVM_DIR)
endif

COMMON_OBJ=\
counters_generic.o \
state.o \
events.o \
formatting.o \
init_exit.o \
profiling.o \
rave2prv.o \
regions.o \
threading.o \
scalar_blocks.o \
inst_trace.o

QEMU_OBJ=\
utils.o \
tb_hook.o 

CC=gcc
CFLAGS=-O3 -Wall -Wno-unused-parameter -Werror -fPIC -g
ifeq ($(PEDANTIC),1) 
  CFLAGS += -Wextra -Wpedantic -Wshadow #-Wconversion
endif
CFLAGS+= `pkg-config --cflags glib-2.0`

LIBDIR=$(BUILD_DIR)/qemu-rave/RVV-$(RVV)/lib
BINDIR=$(BUILD_DIR)/qemu-rave/RVV-$(RVV)/bin
RAVE_DIR=rave_sources

ISA_OBJ= \
callbacks.o \
counters.o \
instr_counters.o \
instr_data.o 

ifeq ($(RVV),0_7_1) 
ISA_OBJ += \
07_decode.o \
instr2prv_0_7.o \
write_pcf_0_7.o

QEMU_DIR = ./downloads/qemu-rvv-0_7_1
CFLAGS += -DRVV_07
OBJDIR=$(RAVE_DIR)/obj/rvv07
else
ISA_OBJ += \
instr2prv_1_0.o \
write_pcf_1_0.o

QEMU_DIR=./downloads/qemu-rvv-1_0
OBJDIR=$(RAVE_DIR)/obj/rvv10
CFLAGS += -DRVV_10
endif

ISA_FOLDER=rvv
SIM=qemu

OBJ:=$(addprefix $(OBJDIR)/common/, $(COMMON_OBJ)) $(addprefix $(OBJDIR)/$(SIM)/, $(QEMU_OBJ)) $(addprefix $(OBJDIR)/$(ISA_FOLDER)/, $(ISA_OBJ)) 

# Todas las cabeceras del proyecto son dependencia de todos los objetos: sin
# esto make no recompila los .o que incluyen una cabecera modificada y se
# mezclan objetos compilados con versiones distintas de un mismo struct
# (p. ej. rave_counters), lo que corrompe la memoria en ejecucion.
RAVE_HEADERS=$(shell find $(RAVE_DIR)/include -name '*.h')
$(OBJ): $(RAVE_HEADERS)

ELFUTILS_DIR=./build/elfutils
IFLAGS=-I$(RAVE_DIR)/include/common -I$(RAVE_DIR)/include/rvv -I$(RAVE_DIR)/include/$(SIM) -I$(QEMU_DIR)/include/qemu -I$(ELFUTILS_DIR)/include

LDFLAGS=-L${ELFUTILS_DIR}/lib -lelf -ldw

.SECONDARY: $(OBJ)
.PHONY: wrappers interfaces $(BINDIR)/rave  
all: $(LIBDIR)/librave.so interfaces wrappers 

#### LIBRAVE

$(OBJDIR)/common: ;
	mkdir -p $@
$(OBJDIR)/$(ISA_FOLDER): ;
	mkdir -p $@
$(OBJDIR)/$(SIM): ;
	mkdir -p $@
$(LIBDIR): ;
	mkdir -p $@

$(OBJDIR)/%.o: $(RAVE_DIR)/src/%.c | $(OBJDIR)/common $(OBJDIR)/$(ISA_FOLDER) $(OBJDIR)/$(SIM)
	$(CC) $< $(CFLAGS) $(IFLAGS) -c -o $@

$(LIBDIR)/librave.so: $(OBJ) | $(LIBDIR)
	$(CC) $^ -shared -fPIC -o $@ $(LDFLAGS)


#### WRAPPERS
$(BINDIR): ;
	mkdir -p $@
$(BINDIR)/rave: | $(BINDIR)
	cp $(RAVE_DIR)/scripts/rvv/rave-$(RVV).sh $@
$(BINDIR)/rave_gdb: | $(BINDIR)
	cp ./utils/rave_gdb $@
	sed -i "s/EXT/${RVV}/g" $@
wrappers: $(BINDIR)/rave $(BINDIR)/rave_gdb


### INTERFACES

CLANG=$(LLVM_DIR)/bin/clang
FLANG=$(LLVM_DIR)/bin/flang
INTERFACES_DIR=$(BUILD_DIR)/interfaces
RAVE_INCL=-I$(RAVE_DIR)/interfaces/rvv

$(INTERFACES_DIR): ;
	mkdir -p $@

$(INTERFACES_DIR)/rave_user_events_f.o: | $(INTERFACES_DIR)
	$(CLANG) $(RAVE_INCL) $(RAVE_DIR)/interfaces/common/rave_fortran_c_func.c -c -o $(OBJDIR)/rave_fortran_c_func.o
	$(FLANG) $(RAVE_INCL) $(RAVE_DIR)/interfaces/common/rave_user_events.f90 -c -o $(OBJDIR)/rave_user_events_tmp.o -J$(INTERFACES_DIR)
	$(CLANG) -r $(OBJDIR)/rave_fortran_c_func.o $(OBJDIR)/rave_user_events_tmp.o -o $@

$(INTERFACES_DIR)/rave_user_events.h: $(RAVE_DIR)/interfaces/rvv/rave_user_events.h | $(INTERFACES_DIR)
	cp $< $@

$(INTERFACES_DIR)/rave_user_events.so: $(RAVE_DIR)/interfaces/rvv/py_module.py | $(BUILD_DIR)/sysroot
	CC=$(CLANG) LDSHARED="$(CLANG) -pthread -shared" SYSROOT=$(BUILD_DIR)/sysroot python3 $< build --build-temp=./python_tmp --build-lib $(INTERFACES_DIR)
	cd $(INTERFACES_DIR) && ln -sf rave_user_events.cpython-* rave_user_events.so
	rm -rf ./python_tmp

interfaces: $(INTERFACES_DIR)/rave_user_events.h $(INTERFACES_DIR)/rave_user_events_f.o $(INTERFACES_DIR)/rave_user_events.so

## CLEANING

clean:
	rm -rf $(RAVE_DIR)/obj/* $(LIBDIR)/librave.so
