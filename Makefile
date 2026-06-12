TARGET = BOOT64.EFI
SO_FILE = kernel.so
OBJ_FILE = kernel.o

EFI_INC = /usr/include/efi
EFI_INC_X86 = $(EFI_INC)/x86_64
EFI_LIB = /usr/lib

CC = gcc
LD = ld
OBJCOPY = objcopy

# Dodano flagi -fno-builtin oraz -O2 dla stabilizacji kodu UEFI
CFLAGS = -I$(EFI_INC) -I$(EFI_INC_X86) \
         -fpic -ffreestanding -fno-builtin \
         -fno-stack-protector -fno-stack-check \
         -fshort-wchar -mno-red-zone \
         -maccumulate-outgoing-args \
         -O2 -Wall -Wextra -c

# Dodano jawną flagę punktu wejścia -e efi_main
LDFLAGS = -shared -Bsymbolic -e efi_main \
          -L$(EFI_LIB) \
          -T $(EFI_LIB)/elf_x86_64_efi.lds \
          $(EFI_LIB)/crt0-efi-x86_64.o

LIBS = -lefi -lgnuefi

all: $(TARGET)

$(OBJ_FILE): kernel.c
	$(CC) $(CFLAGS) kernel.c -o $(OBJ_FILE)

$(SO_FILE): $(OBJ_FILE)
	$(LD) $(LDFLAGS) $(OBJ_FILE) -o $(SO_FILE) $(LIBS)

# Dokładne sekcje wymagane przez UEFI Windows/Firmware Loader
$(TARGET): $(SO_FILE)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
	           -j .dynamic -j .dynsym -j .rel -j .rela \
	           -j .rel.* -j .rela.* -j .reloc \
	           --target=efi-app-x86_64 $(SO_FILE) $(TARGET)

clean:
	rm -f $(OBJ_FILE) $(SO_FILE) $(TARGET)

.PHONY: all clean