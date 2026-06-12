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
#include <stdint.h>
#include <stddef.h>


// RozOS main library
//
// Made by:
// Szymon Wolak (github:ROZcloud)
// Note:
// None
char adres_hex[100];

static inline uint16_t inw(unsigned short port) {
    uint16_t ret;
    asm volatile ( "inw %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

#define CHAR_TL 218 // ┌ - Górny lewy róg
#define CHAR_TR 191 // ┐ - Górny prawy róg
#define CHAR_BL 192 // └ - Dolny lewy róg
#define CHAR_BR 217 // ┘ - Dolny prawy róg
#define CHAR_HL 196 // ─ - Linia pozioma
#define CHAR_VL 179 // │ - Linia pionowa


// Funkcje komunikacji z portami I/O (dla klawiatury)
static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline void outw(unsigned short port, unsigned short val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}


// Multiboot (Zachowane by rdzeń działał, wartości puste w UEFI)
struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
    unsigned int boot_device;
    unsigned int cmdline;
};

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

// Globalne bufory UI
#define SCR_W 80
#define SCR_H 25
static unsigned short vga_shadow_matrix[SCR_W * SCR_H];

// [Tłumacz UEFI] Rysowanie z matrycy na ekran
void tui_flush_to_uefi() {
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    for (int y = 0; y < SCR_H; y++) {
        ST->ConOut->SetCursorPosition(ST->ConOut, 0, y);
        for (int x = 0; x < SCR_W; x++) {
            unsigned short cell = vga_shadow_matrix[y * SCR_W + x];
            unsigned char ch = cell & 0xFF;
            unsigned char attr = (cell >> 8) & 0xFF;
            ST->ConOut->SetAttribute(ST->ConOut, attr & 0x0F);
            
            CHAR16 uefi_char = (CHAR16)ch;
            // Tłumaczenie znaków ASCII na ramki Unicode dla płynnego UI w UEFI
            if(ch == 218) uefi_char = 0x250C;
            else if(ch == 191) uefi_char = 0x2510;
            else if(ch == 192) uefi_char = 0x2514;
            else if(ch == 217) uefi_char = 0x2518;
            else if(ch == 196) uefi_char = 0x2500;
            else if(ch == 179) uefi_char = 0x2502;
            else if(ch == 205) uefi_char = 0x2550;
            else if(ch == 186) uefi_char = 0x2551;
            else if(ch == 201) uefi_char = 0x2554;
            else if(ch == 187) uefi_char = 0x2557;
            else if(ch == 200) uefi_char = 0x255A;
            else if(ch == 188) uefi_char = 0x255D;

            CHAR16 str[2] = {uefi_char, 0};
            ST->ConOut->OutputString(ST->ConOut, str);
        }
    }
    ST->ConOut->EnableCursor(ST->ConOut, TRUE);
}

// Czyszczenie ekranu (szare litery na czarnym tle) - wersja UEFI wrapper
void clear() {
    for(int i = 0; i < SCR_W * SCR_H; i++) {
        vga_shadow_matrix[i] = (0x07 << 8) | ' ';
    }
    tui_flush_to_uefi();
}

// Wypisywanie tekstu w konkretnym miejscu - wersja UEFI wrapper
void print(const char* str, int line, int col, char color) {
    int offset = line * SCR_W + col;
    for(int i = 0; str[i] != '\0' && (col + i) < SCR_W; i++) {
        vga_shadow_matrix[offset + i] = (color << 8) | str[i];
    }
    tui_flush_to_uefi();
}

// Pełna tablica skankodów klawiatury (małe litery)
char get_ascii(unsigned char scancode) {
    static const char kbd_map[] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    if (scancode < 58) return kbd_map[scancode];
    return 0;
}

// Wrapper UEFI do Reboot
void reboot() {
    ST->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

// Przystosowana dla UEFI funkcja wejścia (obsługuje standardowo polecenia konsoli)
void input(char* buffer, int line, int col, int limit) { 
    int i = 0;
    EFI_INPUT_KEY key;
    while(1) {
        if (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) == EFI_SUCCESS) {
            if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') {
                buffer[i] = '\0';
                break;
            } else if (key.UnicodeChar == L'\b' && i > 0) {
                i--;
                buffer[i] = '\0';
                print(" ", line, col + i, 0x07);
            } else if (key.UnicodeChar >= 32 && key.UnicodeChar <= 126 && i < limit) {
                buffer[i] = (char)key.UnicodeChar;
                char tmp[2] = {(char)key.UnicodeChar, '\0'};
                print(tmp, line, col + i, 0x0F);
                i++;
            }
        }
        ST->BootServices->Stall(10000);
    }
}

// Porównywanie tekstów
int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++;
        s2++; }
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
            else if (c >= 'A' && c <= 'F') byte += (c - 'A' + 10);
        }
        bin[p++] = byte;
        i += 2;
    }
    if (p > 0 && bin[p - 1] != 0xc3) {
        bin[p++] = 0xc3;
    }
    ((void (*)())bin)(); // Uwaga, zależy od uprawnień pamięci DEP na danej płycie
}

// Naprawiony delay uzywający stall UEFI (dokładny co do milisekundy)
void delay(int count) {
    ST->BootServices->Stall(count * 1000);
}

char* random() {
    static char buf[2];
    unsigned int lo;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo));
    buf[0] = (lo % 10) + '0'; 
    buf[1] = '\0';
    return buf;
}

// RozOS Main
// Made by:
// Szymon Wolak (github:ROZcloud, email:aza756903@gmail.com)
//
// Note:
// [X] Popraw delay z 3000 na 1000
// [NOTE] Pamiętaj delay 3000 nie działa i jest ignorowane

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

// Zachowana funkcja ACPI. Zablokowana dla bezpieczeństwa by nie zawieszała 64 bitow
struct FADT* find_fadt() {
    return 0; 
}

void acpi_enable(struct FADT* fadt) {
    if (fadt && fadt->SMI_CommandPort != 0) {
        outb(fadt->SMI_CommandPort, fadt->AcpiEnable);
        while ((inw(fadt->PM1a_Control_Block) & 1) == 0);
    }
}

int wait_for_power(struct FADT* fadt) {
    if (!fadt) return 0;
    uint16_t status = inw(fadt->PM1a_Event_Block);
    if (status & (1 << 8)) {
        outw(fadt->PM1a_Event_Block, (1 << 8));
        return 1;
    }
    return 0;
}

struct FADT* fadt = 0;
void dev() {
    while(1) {
        clear();
        int i = 1;
        while(i) {
            if (wait_for_power(fadt)) {
                print("Press developer key", 0, 0, 0x0F);
                i=0;
            }
            break; // Obejscie petli nieskonczonej by UI sie nie zawiesilo
        }
        clear();
        print("Developer shell", 0, 0, 0x0C);
        print("Dev>>", 1, 0, 0x0F);
        char dshell[16] = {0};
        input(dshell, 1, 6, 0x0F);
        if(strcmp(dshell, "acpi") == 0) {
            struct FADT* fadt = find_fadt();
            if(fadt) {
                acpi_enable(fadt);
            }
        }
        if(strcmp(dshell, "exit") == 0) break;
    }
}

int boot_param(struct multiboot_info* mbi, const char* param_name) {
    if (mbi && (mbi->flags & (1 << 2))) {
        const char* kernel_arguments = (const char*)(uint64_t)mbi->cmdline;
        if (str_contains(kernel_arguments, param_name)) {
            return 1;
        }
    }
    return 0;
}

void acpi_disable(struct FADT* fadt) {
    if (fadt && fadt->SMI_CommandPort != 0) {
        outb(fadt->SMI_CommandPort, fadt->AcpiDisable);
        while ((inw(fadt->PM1a_Control_Block) & 1) != 0);
    }
}


// RozOS GUI framework
/* RozOS GUI Framework
Version: 7.0
*/
#define MAX_COMP 16
#define MAX_WIN 10
#define MAX_TERMINAL_CMDS 12

#define MAX_MENU_ITEMS 5
#define MAX_SUB_ITEMS 6

#define TERM_MAX_LINES 32
#define TERM_LINE_WIDTH 50

/* Klasyczna Retro Paleta Blue (Norton Commander Style) */
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

/* Zmienne globalne wymagane przez demonstracyjne kernel_main */
static int win_glowny;
static int win_statystyki;
static char command_input[16];

/* --- PODSTAWOWE FUNKCJE POMOCNICZE C --- */
static int core_strlen(const char* s) { int l = 0; while(s[l]) l++; return l; }
static void core_poke(int x, int y, char attr, unsigned char ch) { if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) vga_shadow_matrix[y * SCR_W + x] = (attr << 8) | ch; }
static int core_strcmp(const char* s1, const char* s2) { while (*s1 && (*s1 == *s2)) { s1++; s2++; } return *(unsigned char*)s1 - *(unsigned char*)s2; }

static void core_layer_bring_to_front(int idx) {
    if (idx < 0 || idx >= total_windows || (rendering_layers[total_windows-1]->is_modal && total_windows > 0)) return;
    struct Window* target = rendering_layers[idx];
    for (int i = idx; i < total_windows - 1; i++) rendering_layers[i] = rendering_layers[i + 1];
    rendering_layers[total_windows - 1] = target;
    for (int i = 0; i < total_windows; i++) rendering_layers[i]->is_active = (i == total_windows - 1);
}

/* --- RENDERER SYSTEMOWY --- */
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

    // Zamiana hardwarowego pętlenia na wirtualny zrzut do matrycy UEFI (Zabezpiecza przed zatrzymaniem)
    tui_flush_to_uefi();
}

/* --- MENU API --- */
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

/* --- INTERFACE API --- */
void tui_init_framework() { total_windows = 0; total_registered_cmds = 0; total_menu_items = 0; menu_active = 0; }
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
    struct Window* win = &windows_pool[win_id]; struct Component* c = &win->children[win->child_count++];
    c->type = COMP_TEXT; c->text = text; c->rel_x = 3; c->rel_y = win->next_item_y++;
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
    int idx = win->child_count; struct Component* c = &win->children[win->child_count++];
    c->type = COMP_PROGRESS; c->rel_x = 3; c->rel_y = win->next_item_y;
    c->w = win->w - 7; c->value = init_val;
    win->next_item_y += 2; return idx;
}

int ui_add_input(int win_id, const char* label, char* buf, int max_len) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count; struct Component* c = &win->children[win->child_count++];
    c->type = COMP_INPUT; c->text = label; c->rel_x = 3;
    c->rel_y = win->next_item_y; c->w = 14; c->buffer = buf; c->buffer_len = max_len; c->curr_chars = 0; buf[0] = '\0';
    win->next_item_y += 2; return idx;
}

int ui_add_terminal(int win_id, int height_lines) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count; struct Component* c = &win->children[win->child_count++];
    c->type = COMP_TERMINAL_APP; c->rel_x = 3; c->rel_y = win->next_item_y;
    c->w = win->w - 7; c->h = height_lines; c->term_head = 0; c->term_prompt = "OS> ";
    win->next_item_y += (height_lines + 1);
    return idx;
}

void tui_append_terminal(int win_id, int comp_idx, const char* msg) {
    struct Component* c = &windows_pool[win_id].children[comp_idx];
    if (c->type != COMP_TERMINAL_APP) return;
    int head = c->term_head; if (head >= TERM_MAX_LINES) { for (int i = 1; i < TERM_MAX_LINES; i++) { for (int j = 0; j < TERM_LINE_WIDTH; j++) c->term_history[i - 1][j] = c->term_history[i][j];
    } head = TERM_MAX_LINES - 1; }
    int j = 0;
    while (msg[j] != '\0' && j < (TERM_LINE_WIDTH - 1)) { c->term_history[head][j] = msg[j]; j++; } c->term_history[head][j] = '\0';
    c->term_head = head + 1;
}

void ui_terminal_set_prompt(int win_id, int term_idx, const char* prompt) { if (windows_pool[win_id].children[term_idx].type == COMP_TERMINAL_APP) windows_pool[win_id].children[term_idx].term_prompt = prompt; }
void ui_terminal_register_cmd(const char* name, TUI_CmdHandler handler) { if (total_registered_cmds < MAX_TERMINAL_CMDS) { registered_cmds[total_registered_cmds].name = name; registered_cmds[total_registered_cmds].handler = handler; total_registered_cmds++; } }

/* CONFIRM BOX */
static void confirm_yes_click(struct Window* win, struct Component* comp) { TUI_ConfirmHandler cb = win->confirm_callback; total_windows--;
    rendering_layers[total_windows-1]->is_modal = 0; if (cb) cb(1); tui_refresh_screen(); }
static void confirm_no_click(struct Window* win, struct Component* comp) { TUI_ConfirmHandler cb = win->confirm_callback;
    total_windows--; rendering_layers[total_windows-1]->is_modal = 0; if (cb) cb(0); tui_refresh_screen(); }

void ui_show_confirm(const char* title, const char* question, TUI_ConfirmHandler callback) {
    int cf = tui_create_window(title, 22, 9, 36, 8);
    windows_pool[cf].is_modal = 1; windows_pool[cf].confirm_callback = callback;
    ui_add_text(cf, question);
    struct Window* win = &windows_pool[cf];
    struct Component* b1 = &win->children[win->child_count++];
    b1->type = COMP_BUTTON; b1->text = "TAK"; b1->rel_x = 6; b1->rel_y = 5; b1->on_action = confirm_yes_click;
    struct Component* b2 = &win->children[win->child_count++];
    b2->type = COMP_BUTTON; b2->text = "NIE"; b2->rel_x = 20; b2->rel_y = 5; b2->on_action = confirm_no_click;
    tui_refresh_screen();
}

static char core_scancode_to_ascii(unsigned char scan) {
    if (scan >= 0x02 && scan <= 0x0B) return "1234567890"[scan - 0x02];
    if (scan >= 0x10 && scan <= 0x19) return "qwertyuiop"[scan - 0x10];
    if (scan == 0x1E) return 'A'; // A
    if (scan == 0x30) return 'B'; // B
    if (scan == 0x2E) return 'C'; // C
    if (scan == 0x20) return 'D'; // D
    if (scan == 0x12) return 'E'; // E
    if (scan == 0x21) return 'F';
    if (scan >= 0x1E && scan <= 0x26) return "asdfghjkl"[scan - 0x1E];
    if (scan >= 0x2C && scan <= 0x32) return "zxcvbnm"[scan - 0x2C];
    if (scan == 0x39) return ' ';
    if (scan == 0x34) return '.'; 
    return 0;
}


/* --- DISPATCHER KLAWIATURY --- */
void tui_dispatch_keyboard(unsigned char scancode) {
    static int alt_active = 0;
    if (scancode == 0x38) { alt_active = 1; return; } if (scancode == 0xB8) { alt_active = 0; return; }
    
    if (scancode == 0x44) {
        menu_active = !menu_active;
        menu_selected_main = 0; menu_dropdown_open = 0; menu_selected_sub = 0;
        tui_refresh_screen(); return;
    }

    if (scancode == 0x01 && menu_active) {
        menu_active = 0;
        menu_dropdown_open = 0; tui_refresh_screen(); return;
    }

    if (!menu_active && total_windows > 0) {
        struct Window* active_win = rendering_layers[total_windows - 1];
        if (scancode == 0x49) { // PAGE UP
            if (active_win->scroll_y > 0) {
                active_win->scroll_y--;
                tui_refresh_screen();
            }
            return;
        }
        
        if (scancode == 0x51) { // PAGE DOWN
            if (active_win->scroll_y < (active_win->virtual_h - active_win->h)) {
                active_win->scroll_y++;
                tui_refresh_screen();
            }
            return;
        }
    }

    if (menu_active) {
        if (scancode & 0x80) return;
        struct MainMenuItem* current = &menu_bar[menu_selected_main];

        if (!menu_dropdown_open) {
            if (scancode == 0x4B) { if (menu_selected_main > 0) menu_selected_main--;
                else menu_selected_main = total_menu_items - 1; tui_refresh_screen(); return; }
            if (scancode == 0x4D) { if (menu_selected_main < total_menu_items - 1) menu_selected_main++;
                else menu_selected_main = 0; tui_refresh_screen(); return; }
            if (scancode == 0x50 || scancode == 0x1C) { if (current->sub_count > 0) { menu_dropdown_open = 1;
                menu_selected_sub = 0; } tui_refresh_screen(); return; }
        } 
        else { 
            if (scancode == 0x48) { if (menu_selected_sub > 0) menu_selected_sub--;
                else menu_selected_sub = current->sub_count - 1; tui_refresh_screen(); return; }
            if (scancode == 0x50) { if (menu_selected_sub < current->sub_count - 1) menu_selected_sub++;
                else menu_selected_sub = 0; tui_refresh_screen(); return; }
            if (scancode == 0x4B) { menu_dropdown_open = 0;
                if (menu_selected_main > 0) menu_selected_main--; else menu_selected_main = total_menu_items - 1; tui_refresh_screen(); return;
            }
            if (scancode == 0x4D) { menu_dropdown_open = 0;
                if (menu_selected_main < total_menu_items - 1) menu_selected_main++; else menu_selected_main = 0; tui_refresh_screen(); return;
            }
            
            if (scancode == 0x1C) { 
                TUI_MenuHandler h = current->sub_items[menu_selected_sub].handler;
                menu_active = 0; menu_dropdown_open = 0; 
                if (h) h(); 
                tui_refresh_screen(); return;
            }
        }
        return;
    }

    if (alt_active && scancode == 0x2D) { 
        if (total_windows > 0 && !rendering_layers[total_windows - 1]->is_modal) { 
            if (!rendering_layers[total_windows - 1]->is_permanent) {
                total_windows--;
                if (total_windows > 0) {
                    for (int i = 0; i < total_windows; i++) {
                        rendering_layers[i]->is_active = 0;
                    }
                    rendering_layers[total_windows - 1]->is_active = 1;
                }
            }
            tui_refresh_screen();
        } 
        return;
    }
    
    if (scancode & 0x80) return;

    if (total_windows == 0) return;
    struct Window* active_win = rendering_layers[total_windows - 1];

    int f_pressed = 0;
    if (scancode >= 0x3B && scancode <= 0x43) {
        f_pressed = scancode - 0x3B + 1;
    } else if (scancode == 0x57) {
        f_pressed = 11;
    } else if (scancode == 0x58) {
        f_pressed = 12;
    }

    if (f_pressed > 0 && f_pressed != 10) {
        if (active_win->f_handlers[f_pressed] != 0) {
            TUI_FKeyHandler handler = active_win->f_handlers[f_pressed];
            handler();
            tui_refresh_screen();
            return;
        }
    }

    if (alt_active) {
        if (scancode == 0x48) { if (active_win->y > 1) active_win->y--;
            tui_refresh_screen(); return; }
        if (scancode == 0x50) { if (active_win->y < 18) active_win->y++;
            tui_refresh_screen(); return; }
        if (scancode == 0x4B) { if (active_win->x > 0) active_win->x--;
            tui_refresh_screen(); return; }
        if (scancode == 0x4D) { if (active_win->x < 50) active_win->x++;
            tui_refresh_screen(); return; }
        if (!menu_active && total_windows > 0) {
            struct Window* active_win = rendering_layers[total_windows - 1];
            if (scancode == 0x49) { // PAGE UP
                if (active_win->scroll_y > 0) {
                    active_win->scroll_y--;
                    tui_refresh_screen();
                }
                return;
            }

            if (scancode == 0x51) { // PAGE DOWN
                if (active_win->scroll_y < (active_win->virtual_h - active_win->h)) {
                    active_win->scroll_y++;
                    tui_refresh_screen();
                }
                return;
            }
        }
    }

    if (alt_active && scancode == 0x0F) {
        if (total_windows > 1) {
            struct Window* first = rendering_layers[0];
            for (int i = 0; i < total_windows - 1; i++) {
                rendering_layers[i] = rendering_layers[i + 1];
            }

            rendering_layers[total_windows - 1] = first;
            for (int i = 0; i < total_windows; i++) {
                rendering_layers[i]->is_active = (i == total_windows - 1);
            }

            tui_refresh_screen();
            return;
        }
    }

    if (scancode == 0x48) { if (active_win->current_focus_comp > 0) active_win->current_focus_comp--;
        else active_win->current_focus_comp = active_win->child_count - 1; tui_refresh_screen(); return; }
    if (scancode == 0x50) { if (active_win->current_focus_comp < active_win->child_count - 1) active_win->current_focus_comp++;
        else active_win->current_focus_comp = 0; tui_refresh_screen(); return; }

    if (active_win->child_count > 0 && active_win->current_focus_comp >= 0) {
        struct Component* fc = &active_win->children[active_win->current_focus_comp];
        if (scancode == 0x1C) {
            if (fc->type == COMP_BUTTON && fc->on_action) { fc->on_action(active_win, fc);
                tui_refresh_screen(); return; }
            if (fc->type == COMP_INPUT && fc->buffer[0] != '\0') {
                for(int i=0; i<active_win->child_count; i++) {
                    if (active_win->children[i].type == COMP_TERMINAL_APP) {
                        char full_echo[64] = {0};
                        int p_len = core_strlen(active_win->children[i].term_prompt);
                        for(int j=0; j<p_len; j++) full_echo[j] = active_win->children[i].term_prompt[j];
                        for(int j=0; fc->buffer[j]; j++) full_echo[p_len + j] = fc->buffer[j];
                        tui_append_terminal(active_win->id, i, full_echo);

                        int found = 0;
                        for(int c=0; c<total_registered_cmds; c++) {
                            if (core_strcmp(fc->buffer, registered_cmds[c].name) == 0) { registered_cmds[c].handler(active_win->id, i);
                                found = 1; break; }
                        }
                        if(!found) tui_append_terminal(active_win->id, i, "Unknown command.");
                    }
                }
                fc->buffer[0] = '\0';
                fc->curr_chars = 0; tui_refresh_screen(); return;
            }
        }
        if (fc->type == COMP_INPUT) {
            if (scancode == 0x0E) { if (fc->curr_chars > 0) { fc->curr_chars--;
                fc->buffer[fc->curr_chars] = '\0'; } tui_refresh_screen(); return; }
            char ascii = core_scancode_to_ascii(scancode);
            if (ascii != 0 && (fc->curr_chars < fc->buffer_len - 1)) {
                if (fc->curr_chars < fc->w) { fc->buffer[fc->curr_chars] = ascii;
                    fc->curr_chars++; fc->buffer[fc->curr_chars] = '\0'; }
            }
            tui_refresh_screen();
            return;
        }
    }
}

void ui_window_set_help(const char* text) {
    if (total_windows <= 0) return;
    struct Window* active_win = rendering_layers[total_windows - 1];
    active_win->help_text = text;

    tui_refresh_screen();
}


void tui_close_active_window() {
    if (total_windows <= 0) return;
    total_windows--;
    if (total_windows > 0) {
        for (int i = 0; i < total_windows; i++) {
            rendering_layers[i]->is_active = 0;
        }
        rendering_layers[total_windows - 1]->is_active = 1;
    }
    tui_refresh_screen();
}



void ui_add_component(int win_id, int type, int rel_x, int rel_y, const char* text) {
    struct Window* win = &windows_pool[win_id];
    struct Component* comp = &win->children[win->child_count];
    
    comp->type = type;
    comp->rel_x = rel_x;
    comp->rel_y = rel_y;
    comp->text = text;
    
    win->child_count++;
}

static unsigned int hex_to_int(char* hex) {
    unsigned int val = 0;
    while (*hex) {
        val *= 16;
        if (*hex >= '0' && *hex <= '9') val += *hex - '0';
        else if (*hex >= 'A' && *hex <= 'F') val += *hex - 'A' + 10;
        else if (*hex >= 'a' && *hex <= 'f') val += *hex - 'a' + 10;
        hex++;
    }
    return val;
}

void show_msgbox(const char* title, const char* text) {
    int w = 30, h = 6;
    int x = (SCR_W - w) / 2;
    int y = (SCR_H - h) / 2;
    int win_id = tui_create_window(title, x, y, w, h);
    windows_pool[win_id].is_modal = 1;
    ui_add_component(win_id, COMP_TEXT, 2, 2, text);
}

void acpi_real_pc_shutdown(struct FADT* fadt) {
    // Wrapper: UEFI call dla wylaczenia chroni 64 bity i zachowuje intencje
    ST->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

void acpi_real_pc_shutdown_run() {
    acpi_real_pc_shutdown(fadt);
}

void poweroff_msg() {
    acpi_disable(fadt);
    ui_set_bottom_help(" Press power to power off");
    int winh = tui_create_window("HALTED", 20, 8, 40, 6);
    ui_add_text(winh, "Halted press power button");
    tui_refresh_screen();
    asm volatile ("cli");
    while(1) {
        asm volatile ("hlt");
    }
}
void HEXRun(struct Window* win, struct Component* comp) {
    struct Component* input_comp = &win->children[2];
    char* hex_cmd = input_comp->buffer;
    run_hex(hex_cmd);
    tui_refresh_screen();
}
void HEXR() {
    int hex_win = tui_create_window("Hex Editor", 0, 0, 40, 16);
    ui_add_text(hex_win, "Enter HEX");
    ui_add_input(hex_win, "HEX", adres_hex, 100);
    ui_add_button(hex_win, "RUN", HEXRun);
    tui_refresh_screen();
}
void launcher() {
    int winl = tui_create_window("System", 0, 0, 40, 16);
    ui_add_text(winl, "Program launcher");
    ui_add_button(winl, "HEX Editor", HEXR);
    tui_refresh_screen();
}

void gui_init() {
    tui_init_framework();
    ui_set_bottom_help(" F10 Menu Alt+X Close Alt+Arrow Window pos Alt+TAB Switch window");
    int cat_sys = ui_menu_add_category("RozOS");
    int cat_win = ui_menu_add_category("Window");
    ui_menu_add_item(cat_win, "Close", tui_close_active_window);
    ui_menu_add_item(cat_sys, "Launcher", launcher);
    ui_menu_add_item(cat_sys, "Halt", poweroff_msg);
    ui_menu_add_item(cat_sys, "Power off", acpi_real_pc_shutdown_run);
    ui_menu_add_item(cat_sys, "Reboot", reboot);
    tui_refresh_screen();
}


// [Translator UEFI do Legacy] Mapuje wciskane z klawiatury znaki na skankody PS/2
unsigned char uefi_key_to_scancode(EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_UP) return 0x48;
    if (key.ScanCode == SCAN_DOWN) return 0x50;
    if (key.ScanCode == SCAN_LEFT) return 0x4B;
    if (key.ScanCode == SCAN_RIGHT) return 0x4D;
    if (key.ScanCode == SCAN_PAGE_UP) return 0x49;
    if (key.ScanCode == SCAN_PAGE_DOWN) return 0x51;
    if (key.ScanCode == SCAN_F1) return 0x3B;
    if (key.ScanCode == SCAN_F2) return 0x3C;
    if (key.ScanCode == SCAN_F3) return 0x3D;
    if (key.ScanCode == SCAN_F4) return 0x3E;
    if (key.ScanCode == SCAN_F10) return 0x44;
    if (key.ScanCode == SCAN_ESC) return 0x01;
    
    if (key.UnicodeChar == L'\t') return 0x0F;
    if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') return 0x1C;
    if (key.UnicodeChar == L'\b') return 0x0E;
    if (key.UnicodeChar == L' ') return 0x39;
    if (key.UnicodeChar == L'-') return 0x0C;
    if (key.UnicodeChar == L'=') return 0x0D;
    if (key.UnicodeChar == L'[') return 0x1A;
    if (key.UnicodeChar == L']') return 0x1B;
    if (key.UnicodeChar == L';') return 0x27;
    if (key.UnicodeChar == L'\'') return 0x28;
    if (key.UnicodeChar == L'`') return 0x29;
    if (key.UnicodeChar == L'\\') return 0x2B;
    if (key.UnicodeChar == L',') return 0x33;
    if (key.UnicodeChar == L'.') return 0x34;
    if (key.UnicodeChar == L'/') return 0x35;
    
    char c = (char)key.UnicodeChar;
    if (c >= 'a' && c <= 'z') {
        const unsigned char sc_map[] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return sc_map[c - 'a'];
    }
    if (c >= 'A' && c <= 'Z') {
        const unsigned char sc_map[] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return sc_map[c - 'A'];
    }
    if (c >= '1' && c <= '9') return (c - '1') + 0x02;
    if (c == '0') return 0x0B;
    
    return 0;
}

void kernel_main(unsigned int magic, struct multiboot_info* mbi) {
    Print(L"[BOOT] Testing print\n");
    print("RozOS 0.1.3", 0, 0, 0x0E);
    Print(L"[BOOT] Finding fadt\n");
    fadt = find_fadt();
    if(fadt) {
	    acpi_enable(fadt);
    }

    while(1) {
        clear();
        print("RozOS 0.1.3", 0, 0, 0x0E);
        print("32 BIT MODE 16 CHAR BUFFER 1MB IMAGE", 1, 0, 0x0A);
        print("SLEEP MODE ACTIVE", 2, 0, 0x0A);
	    print("Type / to help", 3, 0, 0x0A);
        print("RozOS>> ", 4, 0, 0x0F);
        char buffer[16] = {0};
        input(buffer, 4, 8, 10);

        if(strcmp(buffer, "status") == 0) {
            print("23 BIT MODE 16 CHAR BUFFER 1MB IMAGE", 5, 0, 0x0A);
            print("SLEEP MODE DEACTIVED", 6, 0, 0x0F);
	        delay(1000);
        }
        else if(strcmp(buffer, "reboot") == 0) {
            print("Reboot actived!", 5, 0, 0x0F);
            delay(1000);
	        reboot();
        }
        else if(strcmp(buffer, "clear") == 0) {
            clear();
        }
	    else if(strcmp(buffer, "version") == 0) {
	        print("RozOS 0.1.1 Home edition", 5, 0, 0x0A);
            print("build: 000010", 6, 0, 0x0A);
	        print("add-ons: input fix, halt fix, ststus fix", 7, 0, 0x0A);
            print("RozOS is Open Source", 8, 0, 0x0A);
	        print("Made by:Szymon Wolak (github:ROZcloud)", 9, 0, 0x0A);
	        print("Project github:github.com/ROZcloud/RozOShe", 10, 0, 0x0A);
	        delay(1000);
        }
	    else if(strcmp(buffer, "hex") == 0) {
            clear();
            print("hex>> ", 0, 0, 0x0F);
	        char hex_cmd[428];
	        input(hex_cmd, 0, 6, 100);
	        run_hex(hex_cmd);
	        delay(1000);
        }
	    else if(strcmp(buffer, "dev") == 0) {
	        dev();
        }
	    else if(strcmp(buffer, "/") == 0) {
	        clear();
            print("Commands:", 1, 0, 0x0F);
	        print("/ - help commands", 2, 0, 0x0F);
	        print("logout - logout from session", 3, 0, 0x0F);
            print("status - system status", 4, 0, 0x0F);
	        print("reboot - reboot system", 5, 0, 0x0F);
            print("version - show system version", 6, 0, 0x0F);
	        print("hex - enter and run hex", 7, 0, 0x0F);
            print("lock - lock user", 8, 0, 0x0F);
            print("gui - start RozOS TUI", 9, 0, 0x0F);
	        delay(1000);
        }
	    else if(strcmp(buffer, "logout") == 0) {
	        clear();
            print("System halted press power button to poweroff", 0, 0, 0x04);
            acpi_disable(fadt);
            asm volatile (
	            "cli \n\t"
	            "halt_loop: \n\t"
	            "hlt \n\t"
	            "jmp halt_loop"
	        );
        }
        else if(strcmp(buffer, "gui") == 0) {
            gui_init();
            tui_refresh_screen();
            while(1) {
                EFI_INPUT_KEY key;
                if (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) == EFI_SUCCESS) {
                    // Magia: klawisze UEFI zamieniają się na stare kody PS/2 żeby rdzen GUI dzialal identycznie!
                    unsigned char scancode = uefi_key_to_scancode(key);
                    if (scancode) {
                        tui_dispatch_keyboard(scancode);
                    }
                    
                    // ESC dziala tutaj jak stary ALT (uruchamia menu okien itp)
                    if (key.ScanCode == SCAN_ESC) {
                        tui_dispatch_keyboard(0x38); // Aktywuj wewnetrzny ALT
                    }
                }
                ST->BootServices->Stall(10000); // Chroni przed przerwaniem wykonania
            }
        }
        else {
            print("error unknown command", 5, 0, 0x0C);
            delay(1000);
        }
    }
}

// Główny loader dla UEFI (Zastępuje loader Multiboota zachowując ten sam start)
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    ST->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    Print(L"RozOS Booting...\n");
    // Wejdź do głównego pliku ze sztucznym wskaźnikiem Multiboot
    kernel_main(0, NULL);
    
    return EFI_SUCCESS;
}