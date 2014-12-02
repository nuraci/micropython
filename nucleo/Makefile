# Select the board to build for: if not given on the command line,
# then default to STM32NUCF401.
BOARD ?= STM32NUCF401
ifeq ($(wildcard boards/$(BOARD)/.),)
$(error Invalid BOARD specified)
endif

MYDEBUG = 1
BUILD_VERBOSE = 1

# If the build directory is not given, make it reflect the board name.
BUILD ?= build-$(BOARD)

include ../py/mkenv.mk
-include mpconfigport.mk

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h $(BUILD)/pins_qstr.h

# include py core make definitions
include ../py/py.mk

STMHAL_DIR=$(subst nucleo,stmhal,$(CURDIR))
CMSIS_DIR=$(STMHAL_DIR)/cmsis
HAL_DIR=$(STMHAL_DIR)/hal
USBDEV_DIR=$(STMHAL_DIR)/usbdev
FATFS_DIR=$(STMHAL_DIR)/fatfs
COPY_BIN_ON_NUCLEO ?= 1

CROSS_COMPILE = arm-none-eabi-

INC =  -I.
INC += -Iboards/$(BOARD)
INC += -I$(PY_SRC)
INC += -I$(BUILD)
INC += -I$(CMSIS_DIR)/inc
INC += -I$(CMSIS_DIR)/devinc
INC += -I$(STMHAL_DIR)
INC += -I$(HAL_DIR)/inc
INC += -I$(USBDEV_DIR)/core/inc -I$(USBDEV_DIR)/class/cdc_msc_hid/inc
INC += -I$(FATFS_DIR)/src

CFLAGS_CORTEX_M4 = -mthumb -mtune=cortex-m4 -mabi=aapcs-linux -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fsingle-precision-constant -Wdouble-promotion
CFLAGS = $(INC) -Wall -Wpointer-arith -Werror -ansi -std=gnu99 -nostdlib $(CFLAGS_MOD) $(CFLAGS_CORTEX_M4) $(COPT)
CFLAGS += -Iboards/$(BOARD)

LDFLAGS = -nostdlib -T stm32f401xe.ld -Map=$(@:.elf=.map) --cref
STARTUP_FILE = startup_stm32f401xe.s
SYSTEM_FILE = system_stm32f401xe.c
AF_FILE = boards/$(BOARD)/stm32f401xe_af.csv
LIBS =

# Debugging/Optimization
ifeq ($(DEBUG), 1)
CFLAGS += -g -DPENDSV_DEBUG
COPT = -O0
else ifeq ($(MYDEBUG), 1)
CFLAGS += -g3 -fdata-sections -ffunction-sections
COPT += -Os -DNDEBUG
LDFLAGS += --gc-sections
else
CFLAGS += -fdata-sections -ffunction-sections
COPT += -Os -DNDEBUG
LDFLAGS += --gc-sections
endif

# uncomment this if you want libgcc
#LIBS += $(shell $(CC) -print-libgcc-file-name)

SRC_LIB = $(addprefix lib/,\
	libm/math.c \
	libm/mathsincos.c \
	libm/asinfacosf.c \
	libm/atanf.c \
	libm/atan2f.c \
	libm/fmodf.c \
	libm/roundf.c \
	)

SRC_C = $(addprefix $(STMHAL_DIR)/,\
	string0.c \
	mphal.c \
	irq.c \
	pendsv.c \
	systick.c  \
	timer.c \
	led.c \
	pin.c \
	pin_defs_stmhal.c \
	pin_named_pins.c \
	bufhelper.c \
	i2c.c \
	spi.c \
	uart.c \
	printf.c \
	gccollect.c \
	pybstdio.c \
	readline.c \
	pyexec.c \
	input.c \
	modpyb.c \
	modstm.c \
	moduos.c \
	modutime.c \
	moduselect.c \
	modusocket.c \
	modnetwork.c \
	import.c \
	lexerfatfs.c \
	extint.c \
	usrsw.c \
	rtc.c \
	flash.c \
	storage.c \
	file.c \
	sdcard.c \
	diskio.c \
	ffconf.c \
	servo.c \
	adc.c \
	)

SRC_NUCLEO_C = \
	main.c \
	help.c \
	stm32f4x1_it.c \
	$(SYSTEM_FILE) \

SRC_S = $(addprefix $(STMHAL_DIR)/,\
	gchelper.s \
	)
	
SRC_NUCLEO_S = \
	$(STARTUP_FILE) \
	
SRC_HAL = $(addprefix $(HAL_DIR)/src/,\
	stm32f4xx_hal.c \
	stm32f4xx_hal_adc.c \
	stm32f4xx_hal_adc_ex.c \
	stm32f4xx_hal_can.c \
	stm32f4xx_hal_cortex.c \
	stm32f4xx_hal_dac.c \
	stm32f4xx_hal_dac_ex.c \
	stm32f4xx_hal_dma.c \
	stm32f4xx_hal_flash.c \
	stm32f4xx_hal_flash_ex.c \
	stm32f4xx_hal_gpio.c \
	stm32f4xx_hal_i2c.c \
	stm32f4xx_hal_pcd.c \
	stm32f4xx_hal_pcd_ex.c \
	stm32f4xx_hal_pwr.c \
	stm32f4xx_hal_rcc.c \
	stm32f4xx_hal_rcc_ex.c \
	stm32f4xx_hal_rng.c \
	stm32f4xx_hal_rtc.c \
	stm32f4xx_hal_rtc_ex.c \
	stm32f4xx_hal_sd.c \
	stm32f4xx_hal_spi.c \
	stm32f4xx_hal_tim.c \
	stm32f4xx_hal_tim_ex.c \
	stm32f4xx_hal_uart.c \
	stm32f4xx_ll_sdmmc.c \
	stm32f4xx_ll_usb.c \
	)

SRC_FATFS = $(addprefix $(FATFS_DIR)/src/,\
	ff.c \
	option/ccsbcs.c \
	)

OBJ =
OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_LIB:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_NUCLEO_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_S:.s=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_NUCLEO_S:.s=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_HAL:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_FATFS:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_MOD:.c=.o))
OBJ += $(BUILD)/pins_$(BOARD).o

# We put ff.o and stm32f4xx_hal_sd.o into the first 16K section with the ISRs.
# If we compile these using -O0 then it won't fit. So if you really want these
# to be compiled with -O0, then edit stm32f405.ld (in the .isr_vector section)
# and comment out the following 2 lines.
$(BUILD)/$(FATFS_DIR)/src/ff.o: COPT += -Os
$(BUILD)/$(HAL_DIR)/src/stm32f4xx_hal_sd.o: COPT += -Os

all: $(BUILD)/firmware.bin $(BUILD)/firmware.elf

.PHONY: deploy

deploy: $(BUILD)/firmware.bin
	$(ECHO) "Writing $< to the board"
ifeq ($(COPY_BIN_ON_NUCLEO),1)
	$(Q)$(CP) $< /media/$(USER)/NUCLEO
endif

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(ECHO) "Create the (mbed Enabled) binary $@"
	$(Q)$(OBJCOPY) -O binary -j .isr_vector -j .text -j .data $^ $(BUILD)/firmware.bin

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	$(Q)$(SIZE) $@

MAKE_PINS = boards/make-pins.py
BOARD_PINS = boards/$(BOARD)/pins.csv

PREFIX_FILE = boards/stm32f4xx_prefix.c
GEN_PINS_SRC = $(BUILD)/pins_$(BOARD).c
GEN_PINS_HDR = $(HEADER_BUILD)/pins.h
GEN_PINS_QSTR = $(BUILD)/pins_qstr.h
GEN_PINS_AF_CONST = $(HEADER_BUILD)/pins_af_const.h
GEN_PINS_AF_PY = $(BUILD)/pins_af.py


# Making OBJ use an order-only depenedency on the generated pins.h file
# has the side effect of making the pins.h file before we actually compile
# any of the objects. The normal dependency generation will deal with the
# case when pins.h is modified. But when it doesn't exist, we don't know
# which source files might need it.
$(OBJ): | $(HEADER_BUILD)/pins.h

$(BUILD)/main.o: $(GEN_CDCINF_HEADER)

# Use a pattern rule here so that make will only call make-pins.py once to make
# both pins_$(BOARD).c and pins.h
$(BUILD)/%_$(BOARD).c $(HEADER_BUILD)/%.h $(HEADER_BUILD)/%_af_const.h $(BUILD)/%_qstr.h: boards/$(BOARD)/%.csv $(MAKE_PINS) $(AF_FILE) $(PREFIX_FILE) | $(HEADER_BUILD)
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(MAKE_PINS) --board $(BOARD_PINS) --af $(AF_FILE) --prefix $(PREFIX_FILE) --hdr $(GEN_PINS_HDR) --qstr $(GEN_PINS_QSTR) --af-const $(GEN_PINS_AF_CONST) --af-py $(GEN_PINS_AF_PY) > $(GEN_PINS_SRC)

$(BUILD)/pins_$(BOARD).o: $(BUILD)/pins_$(BOARD).c
	$(call compile_c)


include ../py/mkrules.mk
