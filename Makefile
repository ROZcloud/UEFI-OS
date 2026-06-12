# Nazwy plików wyjściowych
TARGET = BOOT64.EFI
SO_FILE = kernel.so
OBJ_FILE = kernel.o

# Ścieżki nagłówków i bibliotek gnu-efi (standardowe dla Ubuntu/Debian)
EFI_INC = /usr/include/efi
EFI_INC_X86 = $(EFI_INC)/x86_64
EFI_LIB = /usr/lib

# Kompilator i narzędzia
CC = gcc
LD = ld
OBJCOPY = objcopy

# Flagi kompilacji dla architektury UEFI x86_64
CFLAGS = -I$(EFI_INC) -I$(EFI_INC_X86) \
         -fpic -ffreestanding \
         -fno-stack-protector -fno-stack-check \
         -fshort-wchar -mno-red-zone \
         -maccumulate-outgoing-args \
         -Wall -Wextra -c

# Flagi linkera (skrypt formatu ELF, crt0 i powiązanie bibliotek)
LDFLAGS = -shared -Bsymbolic \
          -L$(EFI_LIB) \
          -T $(EFI_LIB)/elf_x86_64_efi.lds \
          $(EFI_LIB)/crt0-efi-x86_64.o

LIBS = -lefi -lgnuefi

# Domyślna reguła - buduje końcowy plik EFI
all: $(TARGET)

# Krok 1: Kompilacja pliku .c do obiektu .o
$(OBJ_FILE): kernel.c
	$(CC) $(CFLAGS) kernel.c -o $(OBJ_FILE)

# Krok 2: Linkowanie obiektu .o do biblioteki współdzielonej .so
$(SO_FILE): $(OBJ_FILE)
	$(LD) $(LDFLAGS) $(OBJ_FILE) -o $(SO_FILE) $(LIBS)

# Krok 3: Konwersja formatu ELF (.so) do Portable Executable (.EFI)
$(TARGET): $(SO_FILE)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
	           -j .dynamic -j .dynsym -j .rel -j .rela \
	           -j .rel.* -j .rela.* -j .reloc \
	           --target=efi-app-x86_64 $(SO_FILE) $(TARGET)

# Reguła czyszczenia plików tymczasowych
clean:
	rm -f $(OBJ_FILE) $(SO_FILE) $(TARGET)

# Informacja, że te reguły nie reprezentują fizycznych plików
.PHONY: all clean