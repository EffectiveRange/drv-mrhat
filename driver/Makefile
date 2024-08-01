obj-m += er-mrhat-plat.o 

KVER ?= $(shell uname -r)
PWD := $(CURDIR) 

CFLAGS_er-mrhat-plat.o := -O2 -Wall -Werror $(BQCFLAGS)

all: 
	make -C /lib/modules/$(KVER)/build M=$(PWD) modules 
 
clean: 
	make -C /lib/modules/$(KVER)/build M=$(PWD) clean