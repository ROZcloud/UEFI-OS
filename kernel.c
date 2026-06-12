#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    
    // Tylko wypisz coś na ekran, żebyśmy wiedzieli, że działa
    Print(L"JESTEM W SRODKU!\n");
    
    // Czekaj, żebyś zdążył przeczytać
    while(1); 
    
    return EFI_SUCCESS;
}