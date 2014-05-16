# define the MCU type
AVRGCC_MCU = atmega328p
AVRDUDE_MCU = m328p

# read FUSE bits
read_fuse:
	${AVRDUDE} ${AVRDUDE_FLAGS} -U hfuse:r:-:h -U lfuse:r:-:h 
