// MIT License
// 
// Copyright (c) 2026 ROZcloud
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <efi.h>
#include <efilib.h>

char adres_hex[100];

#define CHAR_TL '+'
#define CHAR_TR '+'
#define CHAR_BL '+'
#define CHAR_BR '+'
#define CHAR_HL '-'
#define CHAR_VL '|'

// Multiboot info nie jest potrzebne w natywnym UEFI, ale zachowujemy strukturę dla kompatybilności wstecznej kodu
struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
    unsigned int boot_device;
    unsigned int cmdline;
};

// Globalne wskaźniki UEFI
EFI_HANDLE            gImageHandle;
EFI_SYSTEM_TABLE     *gST;
EFI_BOOT_SERVICES    *gBS;
EFI_RUNTIME_SERVICES *gRT;

int str_contains(const char* str, const char* sub) {
    if (!str || !sub) return 0;
    while (*str) {
        const char* h = str;
        const char* n = sub;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return 1;
        str++;
    }
    return 0;
}

// Czyszczenie ekranu za pomocą UEFI
void clear() {
    gST->ConOut->ClearScreen(gST->ConOut);
}

// Wypisywanie tekstu w konkretnym miejscu w UEFI
void print(const char* str, int line, int col, char color) {
    // Mapowanie kolorów VGA na kolory UEFI
    UINTN efi_color = color & 0x0F;
    gST->ConOut->SetAttribute(gST->ConOut, efi_color);
    gST->ConOut->SetCursorPosition(gST->ConOut, col, line);

    // UEFI wymaga ciągów znaków UTF-16 (Unicode)
    // Dokonujemy prostej konwersji ASCII -> Unicode w locie
    CHAR16 wstr[256];
    int i = 0;
    for (; str[i] != '\0' && i < 255; i++) {
        wstr[i] = (CHAR16)str[i];
    }
    wstr[i] = L'\0';

    gST->ConOut->OutputString(gST->ConOut, wstr);
}

char get_ascii_from_efi(EFI_INPUT_KEY key) {
    if (key.ScanCode == 0) {
        // Zwykły znak tekstowy
        if (key.UnicodeChar == L'\r') return '\n';
        if (key.UnicodeChar == L'\b') return '\b';
        if (key.UnicodeChar >= 32 && key.UnicodeChar <= 126) return (char)key.UnicodeChar;
    }
    return 0;
}

void reboot() {
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    while(1);
}

void input(char* buffer, int line, int col, int limit) {
    int i = 0;
    EFI_INPUT_KEY key;
    EFI_STATUS status;

    gST->ConOut->SetCursorPosition(gST->ConOut, col, line);

    while(1) {
        status = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (status == EFI_SUCCESS) {
            char c = get_ascii_from_efi(key);
            
            if(c == '\n') {
                buffer[i] = '\0';
                break;
            } 
            else if(c == '\b' && i > 0) {
                i--;
                buffer[i] = '\0';
                print(" ", line, col + i, 0x07);
                gST->ConOut->SetCursorPosition(gST->ConOut, col + i, line);
            }
            else if(c != 0 && i < limit) { 
                buffer[i] = c;
                char tmp[2] = {c, '\0'};
                print(tmp, line, col + i, 0x0F);
                i++;
            }
        }
    }
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void run_hex(char* input) {
    static unsigned char bin[128];
    int p = 0;
    int i = 0;
    while (input[i] != '\0' && p < 127) {
        if (input[i] == ' ') {
            i++;
            continue;
        }
        unsigned char byte = 0;
        for (int j = 0; j < 2; j++) {
            char c = input[i + j];
            byte <<= 4;
            if (c >= '0' && c <= '9') byte += (c - '0');
            else if (c >= 'a' && c <= 'f') byte += (c - 'a' + 10);
        }
        bin[p++] = byte;
        i += 2;
    }
    if (p > 0 && bin[p - 1] != 0xc3) {
        bin[p++] = 0xc3;
    }
    ((void (*)())bin)();
}

void delay(int count) {
    // UEFI udostępnia funkcje mikrosekundowych opóźnień w Boot Services
    gBS->Stall(count * 1000);
}

char* random() {
    static char buf[2];
    UINT64 val = 7; // Prosta inicjalizacja
    // Próba odczytu sprzętowego generatora (jeśli procesor wspiera) lub fallback
    __asm__ __volatile__ (
        "rdrand %0"
        : "=r" (val)
        :
        : "cc"
    );
    buf[0] = (val % 10) + '0'; 
    buf[1] = '\0';
    return buf;
}

// Struktury ACPI zachowane ze względu na kompatybilność logiki frameworka
struct RSDPDescriptor {
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RsdtAddress;
} __attribute__((packed));

struct ACPISDTHeader {
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    char OEMID[6];
    char OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
};

struct FADT {
    struct ACPISDTHeader h;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;
    uint8_t  Reserved;
    uint8_t  PreferredPowerManagementProfile;
    uint16_t SciInterrupt;
    uint32_t SMI_CommandPort;
    uint8_t  AcpiEnable;
    uint8_t  AcpiDisable;
    uint8_t  S4Bios_Req;
    uint8_t  PSTATE_Control;
    uint32_t PM1a_Event_Block;
    uint32_t PM1b_Event_Block;
    uint32_t PM1a_Control_Block;
    uint32_t PM1b_Control_Block;
} __attribute__((packed));

int memcmp_local(const void* s1, const void* s2, int n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while(n--) if(*p1++ != *p2++) return 1;
    return 0;
}

// Pobieranie tablicy ACPI z konfiguracji systemowej UEFI
struct FADT* find_fadt() {
    EFI_GUID acpi20_guid = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        if (CompareGuid(&gST->ConfigurationTable[i].VendorGuid, &acpi20_guid) == 0) {
            struct RSDPDescriptor* rsdp = (struct RSDPDescriptor*)gST->ConfigurationTable[i].VendorTable;
            if (memcmp_local(rsdp->Signature, "RSD PTR ", 8) == 0) {
                struct ACPISDTHeader* rsdt = (struct ACPISDTHeader*)(UINTN)rsdp->RsdtAddress;
                int entries = (rsdt->Length - sizeof(struct ACPISDTHeader)) / 4;
                uint32_t* table_ptr = (uint32_t*)(UINTN)(rsdp->RsdtAddress + sizeof(struct ACPISDTHeader));
                for (int j = 0; j < entries; j++) {
                    struct ACPISDTHeader* h = (struct ACPISDTHeader*)(UINTN)table_ptr[j];
                    if (memcmp_local(h->Signature, "FACP", 4) == 0) {
                        return (struct FADT*)h;
                    }
                }
            }
        }
    }
    return 0;
}

void acpi_enable(struct FADT* fadt) {
    // W UEFI ACPI jest domyślnie włączone przez firmware.
}

void acpi_disable(struct FADT* fadt) {
    // Wyłączenie komputera pod UEFI realizujemy funkcją z Runtime Services
    gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

int wait_for_power(struct FADT* fadt) {
    return 0;
}

struct FADT* fadt = 0;

void dev() {
    while(1) {
        clear();
        print("Developer shell", 0, 0, 0x0C);
        print("Dev>>", 1, 0, 0x0F);
        char dshell[16] = {0};
        input(dshell, 1, 6, 0x0F);
        if(strcmp(dshell, "acpi") == 0) {
            fadt = find_fadt();
        }
    }
}

int boot_param(struct multiboot_info* mbi, const char* param_name) {
    return 0;
}

// RozOS GUI Framework w środowisku UEFI tekstowym
#define SCR_W 80
#define SCR_H 25
#define MAX_COMP 16
#define MAX_WIN 10
#define MAX_TERMINAL_CMDS 12
#define MAX_MENU_ITEMS 5
#define MAX_SUB_ITEMS 6
#define TERM_MAX_LINES 32
#define TERM_LINE_WIDTH 50

#define COL_DESKTOP     0x1F
#define COL_MENU_BAR    0x70
#define COL_MENU_SEL    0x4F
#define COL_WIN_ACT     0x1F  
#define COL_WIN_PAS     0x70  
#define COL_HDR_ACT     0x3F  
#define COL_HDR_PAS     0x78  
#define COL_COMP_FOCUS  0x4F  
#define COL_INPUT_BOX   0x0F  
#define COL_TERM_BG     0x07  
#define COL_SHADOW      0x08  

struct Component;
struct Window;

typedef void (*TUI_EventHandler)(struct Window* win, struct Component* comp);
typedef void (*TUI_ConfirmHandler)(int result_yes);
typedef void (*TUI_CmdHandler)(int win_id, int term_idx);
typedef void (*TUI_MenuHandler)(void); 
typedef void (*TUI_FKeyHandler)(void);

typedef enum { COMP_TEXT, COMP_BUTTON, COMP_PROGRESS, COMP_INPUT, COMP_TERMINAL_APP } CompType;

struct Component {
    CompType type;
    const char* text;
    int rel_x, rel_y, w, h, value;
    char* buffer;
    int buffer_len, curr_chars;    
    char term_history[TERM_MAX_LINES][TERM_LINE_WIDTH];
    int term_head;
    const char* term_prompt;
    TUI_EventHandler on_action;
};

struct Window {
    int id, x, y, w, h;
    const char* title;
    const char* help_text;
    int is_active, current_focus_comp, scroll_y, virtual_h, child_count, next_item_y, is_modal;
    int is_permanent; 
    TUI_ConfirmHandler confirm_callback;
    TUI_FKeyHandler f_handlers[13];
    struct Component children[MAX_COMP]; 
};

struct TerminalCommand { const char* name; TUI_CmdHandler handler; };

struct SubMenuItem {
    const char* name;
    TUI_MenuHandler handler;
};

struct MainMenuItem {
    const char* name;
    int x_pos;
    struct SubMenuItem sub_items[MAX_SUB_ITEMS];
    int sub_count;
};

static unsigned short vga_shadow_matrix[SCR_W * SCR_H];
static struct Window windows_pool[MAX_WIN];
static struct Window* rendering_layers[MAX_WIN];
static int total_windows = 0;
static struct TerminalCommand registered_cmds[MAX_TERMINAL_CMDS];
static int total_registered_cmds = 0;
static struct MainMenuItem menu_bar[MAX_MENU_ITEMS];
static int total_menu_items = 0;
static int menu_active = 0;         
static int menu_selected_main = 0;
static int menu_dropdown_open = 0;  
static int menu_selected_sub = 0;
static const char* global_custom_help_text = " F1 Pomoc  F10 Menu  Alt+X Zamknij  Alt+WSAD Ruch Okna  Alt+TAB Przelacz";

static int win_glowny;
static int win_statystyki;
static char command_input[16];

static int core_strlen(const char* s) { int l = 0; while(s[l]) l++; return l; }
static void core_poke(int x, int y, char attr, unsigned char ch) { 
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) {
        vga_shadow_matrix[y * SCR_W + x] = (attr << 8) | ch; 
    }
}
static int core_strcmp(const char* s1, const char* s2) { 
    while (*s1 && (*s1 == *s2)) { s1++; s2++; } 
    return *(unsigned char*)s1 - *(unsigned char*)s2; 
}

static void core_layer_bring_to_front(int idx) {
    if (idx < 0 || idx >= total_windows || (rendering_layers[total_windows-1]->is_modal && total_windows > 0)) return;
    struct Window* target = rendering_layers[idx];
    for (int i = idx; i < total_windows - 1; i++) rendering_layers[i] = rendering_layers[i + 1];
    rendering_layers[total_windows - 1] = target;
    for (int i = 0; i < total_windows; i++) rendering_layers[i]->is_active = (i == total_windows - 1);
}

void tui_refresh_screen() {
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        vga_shadow_matrix[i] = (COL_DESKTOP << 8) | 32;
    }
    
    for (int c = 0; c < SCR_W; c++) {
        vga_shadow_matrix[0 * SCR_W + c] = (COL_MENU_BAR << 8) | ' ';
    }
    for (int i = 0; i < total_menu_items; i++) {
        char attr = (menu_active && menu_selected_main == i) ? COL_MENU_SEL : COL_MENU_BAR;
        int xp = menu_bar[i].x_pos;
        
        vga_shadow_matrix[0 * SCR_W + xp] = (attr << 8) | ' ';
        int j = 0;
        for (; menu_bar[i].name[j]; j++) {
            vga_shadow_matrix[0 * SCR_W + (xp + 1 + j)] = (attr << 8) | menu_bar[i].name[j];
        }
        vga_shadow_matrix[0 * SCR_W + (xp + 1 + j)] = (attr << 8) | ' ';
    }

    const char* help_to_display = global_custom_help_text;
    if (total_windows > 0) {
        struct Window* active_win = rendering_layers[total_windows - 1];
        if (active_win->help_text != 0) {
            help_to_display = active_win->help_text;
        }
    }

    int row_offset = (SCR_H - 1) * SCR_W;
    for (int c = 0; c < SCR_W; c++) {
        vga_shadow_matrix[row_offset + c] = (COL_MENU_BAR << 8) | ' ';
    }
    for (int i = 0; help_to_display[i] != 0 && i < SCR_W; i++) {
        vga_shadow_matrix[row_offset + i] = (COL_MENU_BAR << 8) | help_to_display[i];
    }

    for (int z = 0; z < total_windows; z++) {
        struct Window* win = rendering_layers[z];
        char w_col = win->is_active ? COL_WIN_ACT : COL_WIN_PAS;
        char h_col = win->is_active ? COL_HDR_ACT : COL_HDR_PAS;
        for (int l = 1; l <= win->h; l++) {
            int sx1 = win->x + win->w, sx2 = win->x + win->w + 1, sy = win->y + l;
            if (sy < SCR_H - 1) {
                if (sx1 < SCR_W) vga_shadow_matrix[sy * SCR_W + sx1] = (COL_SHADOW << 8) | (vga_shadow_matrix[sy * SCR_W + sx1] & 0x00FF);
                if (sx2 < SCR_W) vga_shadow_matrix[sy * SCR_W + sx2] = (COL_SHADOW << 8) | (vga_shadow_matrix[sy * SCR_W + sx2] & 0x00FF);
            }
        }
        for (int c = 2; c < win->w + 2; c++) {
            int sx = win->x + c, sy = win->y + win->h;
            if (sx < SCR_W && sy < SCR_H - 1) vga_shadow_matrix[sy * SCR_W + sx] = (COL_SHADOW << 8) | (vga_shadow_matrix[sy * SCR_W + sx] & 0x00FF);
        }

        for (int l = 0; l < win->h; l++) {
            for (int c = 0; c < win->w; c++) {
                unsigned char ch = ' ';
                if (l == 0 || l == win->h - 1) ch = 205;
                else if (c == 0 || c == win->w - 1) ch = 186;
                if (l == 0 && c == 0) ch = 201;
                if (l == 0 && c == win->w - 1) ch = 187;
                if (l == win->h - 1 && c == 0) ch = 200;
                if (l == win->h - 1 && c == win->w - 1) ch = 188;
                int target_x = win->x + c;
                int target_y = win->y + l;
                if (target_x >= 0 && target_x < SCR_W && target_y >= 0 && target_y < SCR_H) {
                    vga_shadow_matrix[target_y * SCR_W + target_x] = (w_col << 8) | ch;
                }
            }
        }
        
        for (int c = 1; c < win->w - 1; c++) {
            vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + c)] = (h_col << 8) | ' ';
        }
        
        vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 2)] = (h_col << 8) | '[';
        if (win->is_permanent) {
            vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 3)] = (h_col << 8) | '-';
        } else {
            vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 3)] = (0x4F << 8) | 'X';
        }
        vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 4)] = (h_col << 8) | ']';

        int tl = core_strlen(win->title); 
        int t_start = win->x + ((win->w - tl) / 2);
        for (int i = 0; i < tl && (t_start + i) < (win->x + win->w - 2); i++) {
            vga_shadow_matrix[(win->y + 1) * SCR_W + (t_start + i)] = (h_col << 8) | win->title[i];
        }

        for (int i = 0; i < win->child_count; i++) {
            struct Component* comp = &win->children[i];
            int scrolled_rel_y = comp->rel_y - win->scroll_y;
            if (scrolled_rel_y < 2 || scrolled_rel_y >= win->h - 1) continue;
            int cx = win->x + comp->rel_x; 
            int cy = win->y + scrolled_rel_y;
            int has_focus = (!menu_active && win->is_active && win->current_focus_comp == i);
            char dynamic_attr = has_focus ? COL_COMP_FOCUS : w_col;
            if (cx < 0 || cx >= SCR_W || cy < 0 || cy >= SCR_H) continue;
            if (comp->type == COMP_TEXT) {
                for (int j = 0; comp->text[j] && (cx + j) < SCR_W; j++) {
                    vga_shadow_matrix[cy * SCR_W + (cx + j)] = (w_col << 8) | comp->text[j];
                }
            } else if (comp->type == COMP_BUTTON) {
                int len = 0;
                while(comp->text[len] != '\0') len++;
                
                vga_shadow_matrix[cy * SCR_W + cx] = (dynamic_attr << 8) | CHAR_TL;
                for(int j = 0; j < len + 2; j++) {
                    vga_shadow_matrix[cy * SCR_W + (cx + 1 + j)] = (dynamic_attr << 8) | CHAR_HL;
                }
                vga_shadow_matrix[cy * SCR_W + (cx + len + 3)] = (dynamic_attr << 8) | CHAR_TR;
                
                vga_shadow_matrix[(cy + 1) * SCR_W + cx] = (dynamic_attr << 8) | CHAR_VL;
                for(int j = 0; j < len; j++) {
                    vga_shadow_matrix[(cy + 1) * SCR_W + (cx + 2 + j)] = (dynamic_attr << 8) | comp->text[j];
                }
                vga_shadow_matrix[(cy + 1) * SCR_W + (cx + len + 3)] = (dynamic_attr << 8) | CHAR_VL;
                
                vga_shadow_matrix[(cy + 2) * SCR_W + cx] = (dynamic_attr << 8) | CHAR_BL;
                for(int j = 0; j < len + 2; j++) {
                    vga_shadow_matrix[(cy + 2) * SCR_W + (cx + 1 + j)] = (dynamic_attr << 8) | CHAR_HL;
                }
                vga_shadow_matrix[(cy + 2) * SCR_W + (cx + len + 3)] = (dynamic_attr << 8) | CHAR_BR;
                
            } else if (comp->type == COMP_INPUT) {
                int lbl_l = core_strlen(comp->text);
                for (int j = 0; j < lbl_l && (cx + j) < SCR_W; j++) {
                    vga_shadow_matrix[cy * SCR_W + (cx + j)] = (w_col << 8) | comp->text[j];
                }
                char input_attr = has_focus ? 0x4F : COL_INPUT_BOX;
                for (int b = 0; b < comp->w && (cx + lbl_l + b) < SCR_W; b++) {
                    unsigned char ch = (comp->buffer && comp->buffer[b] != '\0') ? comp->buffer[b] : ' ';
                    vga_shadow_matrix[cy * SCR_W + (cx + lbl_l + b)] = (input_attr << 8) | ch;
                }
            } else if (comp->type == COMP_TERMINAL_APP) {
                for (int th = 0; th < comp->h && (cy + th) < SCR_H; th++) {
                    for (int tw = 0; tw < comp->w && (cx + tw) < SCR_W; tw++) {
                        vga_shadow_matrix[(cy + th) * SCR_W + (cx + tw)] = (COL_TERM_BG << 8) | ' ';
                    }
                }
                int print_row = 0;
                for (int line_idx = 0; line_idx < comp->term_head && print_row < comp->h; line_idx++) {
                    char* line_text = comp->term_history[line_idx];
                    for (int char_idx = 0; line_text[char_idx] != '\0' && (cx + char_idx) < SCR_W; char_idx++) {
                        if ((cy + print_row) < SCR_H) {
                            vga_shadow_matrix[(cy + print_row) * SCR_W + (cx + char_idx)] = (0x0A << 8) | line_text[char_idx];
                        }
                    }
                    print_row++;
                }
            }
        }
    }

    if (menu_active && menu_dropdown_open) {
        struct MainMenuItem* main_m = &menu_bar[menu_selected_main];
        int m_x = main_m->x_pos;
        int m_w = 18; 
        int m_h = main_m->sub_count + 2;
        for (int l = 0; l < m_h; l++) {
            for (int c = 0; c < m_w; c++) {
                char attr = COL_MENU_BAR;
                unsigned char ch = ' ';
                if (l == 0 || l == m_h - 1) ch = 196;
                else if (c == 0 || c == m_w - 1) ch = 179;
                if (l == 0 && c == 0) ch = 218;
                if (l == 0 && c == m_w - 1) ch = 191;
                if (l == m_h - 1 && c == 0) ch = 192;
                if (l == m_h - 1 && c == m_w - 1) ch = 193;
                if ((m_x + c) < SCR_W && (1 + l) < SCR_H) {
                    vga_shadow_matrix[(1 + l) * SCR_W + (m_x + c)] = (attr << 8) | ch;
                }
            }
        }
        for (int i = 0; i < main_m->sub_count; i++) {
            char item_attr = (menu_selected_sub == i) ? COL_MENU_SEL : COL_MENU_BAR;
            for (int c = 1; c < m_w - 1; c++) {
                if ((m_x + c) < SCR_W && (2 + i) < SCR_H) {
                    vga_shadow_matrix[(2 + i) * SCR_W + (m_x + c)] = (item_attr << 8) | ' ';
                }
            }
            const char* name = main_m->sub_items[i].name;
            for (int j = 0; name[j] && j < (m_w - 3); j++) {
                if ((m_x + 2 + j) < SCR_W && (2 + i) < SCR_H) {
                    vga_shadow_matrix[(2 + i) * SCR_W + (m_x + 2 + j)] = (item_attr << 8) | name[j];
                }
            }
        }
    }

    // Odzwierciedlamy macierz cieniowaną na ekran konsoli UEFI element po elemencie
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            unsigned short cell = vga_shadow_matrix[y * SCR_W + x];
            char ch = cell & 0xFF;
            char attr = (cell >> 8) & 0xFF;
            char str_cell[2] = {ch, '\0'};
            print(str_cell, y, x, attr);
        }
    }
}

int ui_menu_add_category(const char* name) {
    if (total_menu_items >= MAX_MENU_ITEMS) return -1;
    int prev_x = (total_menu_items == 0) ? 2 : menu_bar[total_menu_items - 1].x_pos + core_strlen(menu_bar[total_menu_items - 1].name) + 4;
    menu_bar[total_menu_items].name = name;
    menu_bar[total_menu_items].x_pos = prev_x;
    menu_bar[total_menu_items].sub_count = 0;
    return total_menu_items++;
}

void ui_menu_add_item(int cat_id, const char* name, TUI_MenuHandler handler) {
    if (cat_id < 0 || cat_id >= total_menu_items) return;
    struct MainMenuItem* m = &menu_bar[cat_id];
    if (m->sub_count < MAX_SUB_ITEMS) {
        m->sub_items[m->sub_count].name = name;
        m->sub_items[m->sub_count].handler = handler;
        m->sub_count++;
    }
}

void ui_set_bottom_help(const char* text) {
    global_custom_help_text = text;
}

void tui_init_framework() { 
    total_windows = 0; total_registered_cmds = 0; total_menu_items = 0; menu_active = 0;
}

int tui_create_window(const char* title, int x, int y, int w, int h) {
    if (total_windows >= MAX_WIN) return -1;
    struct Window* win = &windows_pool[total_windows];
    win->id = total_windows; win->title = title; win->x = x; win->y = y; win->w = w;
    win->h = h;
    win->current_focus_comp = 0; win->scroll_y = 0; win->virtual_h = h; win->next_item_y = 3; win->child_count = 0;
    win->is_modal = 0; win->confirm_callback = 0;
    win->is_permanent = 0;

    for (int f = 1; f <= 12; f++) {
        win->f_handlers[f] = 0;
    } 

    rendering_layers[total_windows] = win; total_windows++; core_layer_bring_to_front(total_windows - 1);
    return win->id;
}

void ui_window_set_permanent(int win_id, int status) {
    if (win_id >= 0 && win_id < MAX_WIN) {
        windows_pool[win_id].is_permanent = status;
    }
}

int ui_register_f_key(int f_number, TUI_FKeyHandler handler) {
    if (f_number == 10 || f_number < 1 || f_number > 12) return 0;
    if (total_windows == 0) return 0;
    struct Window* active_win = rendering_layers[total_windows - 1];
    active_win->f_handlers[f_number] = handler;
    return 1;
}

void ui_unregister_f_key(int f_number) {
    if (f_number == 10 || f_number < 1 || f_number > 12) return;
    if (total_windows == 0) return;
    struct Window* active_win = rendering_layers[total_windows - 1];
    active_win->f_handlers[f_number] = 0;
}

void ui_add_text(int win_id, const char* text) {
    struct Window* win = &windows_pool[win_id];
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_TEXT;
    c->text = text;
    c->rel_x = 3;
    c->rel_y = win->next_item_y++;
}

int ui_add_button(int win_id, const char* label, TUI_EventHandler handler) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count;
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_BUTTON;
    c->text = label;
    c->rel_x = 3;
    c->rel_y = win->next_item_y;
    c->on_action = handler;
    int len = 0;
    while(label[len] != '\0') len++;
    c->w = len + 4;
    c->h = 3;
    win->next_item_y += 4;
    return idx;
}

int ui_add_progress(int win_id, int init_val) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count;
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_PROGRESS;
    c->rel_x = 3;
    c->rel_y = win->next_item_y;
    c->w = win->w - 7;
    c->value = init_val;
    win->next_item_y += 2;
    return idx;
}

int ui_add_input(int win_id, const char* label, char* buf, int max_len) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count;
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_INPUT;
    c->text = label;
    c->rel_x = 3;
    c->rel_y = win->next_item_y;
    c->w = 14;
    c->buffer = buf;
    c->buffer_len = max_len;
    c->curr_chars = 0;
    buf[0] = '\0';
    win->next_item_y += 2;
    return idx;
}

int ui_add_terminal(int win_id, int height_lines) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count;
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_TERMINAL_APP;
    c->rel_x = 3;
    c->rel_y = win->next_item_y;
    c->w = win->w - 7;
    c->h = height_lines;
    c->term_head = 0;
    c->term_prompt = "OS> ";
    win->next_item_y += (height_lines + 1);
    return idx;
}

void gui_start_tui() {
    tui_init_framework();
    win_glowny = tui_create_window("RozOS Glowny Pulpit", 5, 3, 50, 15);
    ui_add_text(win_glowny, "Witaj w RozOS GUI Framework v7.0");
    ui_add_text(win_glowny, "System gotowy do pracy.");
    ui_add_input(win_glowny, "Komenda: ", command_input, 15);
    
    tui_refresh_screen();
    delay(2000); // Pozwalamy użytkownikowi zobaczyć framework przed powrotem do CLI
}

// Główna pętla powłoki tekstowej systemu
void shell_loop() {
    char buffer[128];
    while(1) {
        clear();
        print("RozOS Interactive Shell", 0, 0, 0x0E);
        print("RozOS>", 1, 0, 0x0F);
        
        input(buffer, 1, 7, 100);
        
        if (strcmp(buffer, "reboot") == 0) {
            reboot();
        }
        else if (strcmp(buffer, "version") == 0) {
            clear();
            print("RozOS Version 7.0 (UEFI x86_64 Build)", 1, 0, 0x0F);
            delay(1500);
        }
        else if (strcmp(buffer, "status") == 0) {
            clear();
            print("System Status: OK (Running under UEFI)", 1, 0, 0x0A);
            delay(1500);
        }
        else if (strcmp(buffer, "hex") == 0) {
            clear();
            print("Enter hex machine code:", 0, 0, 0x0F);
            char hex_cmd[128];
            input(hex_cmd, 1, 0, 100);
            run_hex(hex_cmd);
            delay(1000);
        }
        else if (strcmp(buffer, "dev") == 0) {
            dev();
        }
        else if (strcmp(buffer, "gui") == 0) {
            gui_start_tui();
        }
        else if (strcmp(buffer, "logout") == 0) {
            clear();
            print("System turning off...", 0, 0, 0x04);
            acpi_disable(fadt);
        }
        else if (strcmp(buffer, "/") == 0) {
            clear();
            print("Commands:", 1, 0, 0x0F);
            print("/       - help commands", 2, 0, 0x0F);
            print("logout  - poweroff system", 3, 0, 0x0F);
            print("status  - system status", 4, 0, 0x0F);
            print("reboot  - reboot system", 5, 0, 0x0F);
            print("version - show system version", 6, 0, 0x0F);
            print("hex     - enter and run hex", 7, 0, 0x0F);
            print("gui     - start RozOS TUI", 8, 0, 0x0F);
            delay(2500);
        }
    }
}

// Główny punkt wejścia (Entry Point) aplikacji UEFI BOOT64.EFI
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    // Inicjalizacja globalnych wskaźników UEFI
    gImageHandle = ImageHandle;
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gRT = SystemTable->RuntimeServices;

    // Inicjalizacja biblioteki efilib
    InitializeLib(ImageHandle, SystemTable);

    // Czyszczenie ekranu przy starcie
    clear();
    
    // Uruchomienie pętli systemowej powłoki
    shell_loop();

    return EFI_SUCCESS;
}