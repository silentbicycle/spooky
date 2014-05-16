# Common Makefile for generic AVR development with avr-gcc and avrdude

# command names
CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude

# c options
OPT = -Os
WARN = -Wall -Wstrict-prototypes
CSTANDARD = -std=c99

CFLAGS += -mmcu=${AVRGCC_MCU} -I. -g -DF_CPU=${F_CPU}UL ${OPT}
CFLAGS += ${INCS} ${CDEFS}
CFLAGS += -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums
CFLAGS += ${WARN} ${CSTANDARD}

# linker options
LDFLAGS += -Wl,-Map=${TARGET}.map,--cref -lm

# targets
OBJ = $(SRC:.c=.o)
FIRMWARE = ${TARGET}.hex

# default target
all: firmware

firmware: ${FIRMWARE}

help:
	@echo "targets: firmware (default), program, clean"

# compile
.c.o:
	${CC} -c ${CFLAGS} $? -o $@

${TARGET}.elf: ${OBJ}
	${CC} ${CFLAGS} ${OBJ} -o ${TARGET}.elf ${LDFLAGS}

${FIRMWARE}: ${TARGET}.elf
	${OBJCOPY} ${STUFF} ${TARGET}.elf -O ihex ${FIRMWARE}

# flashing options
#AVRDUDE_PORT = usb
AVRDUDE_PORT = /dev/tty.usbmodemfd121

#AVRDUDE_PROGRAMMER = usbtiny
AVRDUDE_PROGRAMMER = arduino

AVRDUDE_FLASH_CMD = -U flash:w:${FIRMWARE}

program: ${FIRMWARE}
	${AVRDUDE} -p ${AVRDUDE_MCU} -P ${AVRDUDE_PORT} -c ${AVRDUDE_PROGRAMMER} \
		${AVRDUDE_FLASH_CMD}

clean:
	rm -f ${TARGET}.{hex,elf,map,eep,lst,lss,sym}
	rm -f ${OBJ}
