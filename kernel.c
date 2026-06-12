#include <efi.h>
#include <efilib.h>

char adres_hex[100];

#define CHAR_TL '+'
#define CHAR_TR '+'
#define CHAR_BL '+'
#define CHAR_BR '+'
#define CHAR_HL '-'
#define CHAR_VL '|'

#define SCR_W 80
#define SCR_H 25
#define MAX_COMP 16
#define MAX_WIN 10
#define MAX_MENU_ITEMS 5
#define MAX_SUB_ITEMS 6

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

static unsigned short vga_shadow_matrix[SCR_W * SCR_H];

extern EFI_SYSTEM_TABLE *ST;
extern EFI_BOOT_SERVICES *BS;

struct Component;
struct Window;

typedef void (*TUI_EventHandler)(struct Window* win, struct Component* comp);
typedef void (*TUI_ConfirmHandler)(int result_yes);
typedef void (*TUI_MenuHandler)(void); 
typedef void (*TUI_FKeyHandler)(void);

typedef enum { COMP_TEXT, COMP_BUTTON, COMP_PROGRESS, COMP_INPUT, COMP_TERMINAL_APP } CompType;

struct Component {
    CompType type;
    const char* text;
    int rel_x, rel_y, w, h, value;
    char* buffer;
    int buffer_len, curr_chars;    
    char term_history[32][50];
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
static struct MainMenuItem menu_bar[MAX_MENU_ITEMS];
static int total_menu_items = 0;
static int menu_active = 0;         
static int menu_selected_main = 0;
static int menu_dropdown_open = 0;
static int menu_selected_sub = 0;
static const char* global_custom_help_text = " F1 Pomoc  F10 Menu  Alt+X Zamknij  Alt+WSAD Ruch Okna  Alt+TAB Przelacz";
static int win_glowny;
static char command_input[16];

static int core_strlen(const char* s) { int l = 0; while(s[l]) l++; return l; }
static int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static void core_layer_bring_to_front(int idx) {
    if (idx < 0 || idx >= total_windows || (rendering_layers[total_windows-1]->is_modal && total_windows > 0)) return;
    struct Window* target = rendering_layers[idx];
    for (int i = idx; i < total_windows - 1; i++) rendering_layers[i] = rendering_layers[i + 1];
    rendering_layers[total_windows - 1] = target;
    for (int i = 0; i < total_windows; i++) rendering_layers[i]->is_active = (i == total_windows - 1);
}

void clear() {
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        vga_shadow_matrix[i] = (COL_DESKTOP << 8) | ' ';
    }
}

void print(const char* str, int line, int col, char color) {
    if (line < 0 || line >= SCR_H || col < 0 || col >= SCR_W) return;
    int offset = line * SCR_W + col;
    for (int i = 0; str[i] != '\0' && (col + i) < SCR_W; i++) {
        vga_shadow_matrix[offset + i] = (color << 8) | (unsigned char)str[i];
    }
}

void tui_refresh_screen() {
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    for (int y = 0; y < SCR_H; y++) {
        ST->ConOut->SetCursorPosition(ST->ConOut, 0, y);
        for (int x = 0; x < SCR_W; x++) {
            unsigned short cell = vga_shadow_matrix[y * SCR_W + x];
            unsigned char ch = cell & 0xFF;
            unsigned char attr = (cell >> 8) & 0xFF;
            ST->ConOut->SetAttribute(ST->ConOut, attr & 0x0F);
            CHAR16 uefi_char[2] = {(CHAR16)ch, 0};
            ST->ConOut->OutputString(ST->ConOut, uefi_char);
        }
    }
    ST->ConOut->EnableCursor(ST->ConOut, TRUE);
}

void tui_render_all_layers() {
    clear();
    for (int c = 0; c < SCR_W; c++) vga_shadow_matrix[0 * SCR_W + c] = (COL_MENU_BAR << 8) | ' ';
    for (int i = 0; i < total_menu_items; i++) {
        char attr = (menu_active && menu_selected_main == i) ? COL_MENU_SEL : COL_MENU_BAR;
        int xp = menu_bar[i].x_pos;
        vga_shadow_matrix[0 * SCR_W + xp] = (attr << 8) | ' ';
        int j = 0;
        for (; menu_bar[i].name[j]; j++) vga_shadow_matrix[0 * SCR_W + (xp + 1 + j)] = (attr << 8) | menu_bar[i].name[j];
        vga_shadow_matrix[0 * SCR_W + (xp + 1 + j)] = (attr << 8) | ' ';
    }

    const char* help_to_display = global_custom_help_text;
    if (total_windows > 0 && rendering_layers[total_windows - 1]->help_text) {
        help_to_display = rendering_layers[total_windows - 1]->help_text;
    }
    int row_offset = (SCR_H - 1) * SCR_W;
    for (int c = 0; c < SCR_W; c++) vga_shadow_matrix[row_offset + c] = (COL_MENU_BAR << 8) | ' ';
    for (int i = 0; help_to_display[i] != 0 && i < SCR_W; i++) vga_shadow_matrix[row_offset + i] = (COL_MENU_BAR << 8) | help_to_display[i];

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
        for (int c = 1; c < win->w - 1; c++) vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + c)] = (h_col << 8) | ' ';
        vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 2)] = (h_col << 8) | '[';
        vga_shadow_matrix[(win->y + 1) * SCR_W + (win->x + 3)] = win->is_permanent ? ((h_col << 8) | '-') : ((0x4F << 8) | 'X');
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
            if (comp->type == COMP_TEXT) {
                for (int j = 0; comp->text[j] && (cx + j) < SCR_W; j++) vga_shadow_matrix[cy * SCR_W + (cx + j)] = (w_col << 8) | comp->text[j];
            } else if (comp->type == COMP_BUTTON) {
                print(comp->text, cy, cx, dynamic_attr);
            } else if (comp->type == COMP_INPUT) {
                int lbl_l = core_strlen(comp->text);
                for (int j = 0; j < lbl_l && (cx + j) < SCR_W; j++) vga_shadow_matrix[cy * SCR_W + (cx + j)] = (w_col << 8) | comp->text[j];
                char input_attr = has_focus ? 0x4F : COL_INPUT_BOX;
                for (int b = 0; b < comp->w && (cx + lbl_l + b) < SCR_W; b++) {
                    unsigned char ch = (comp->buffer && comp->buffer[b] != '\0') ? comp->buffer[b] : ' ';
                    vga_shadow_matrix[cy * SCR_W + (cx + lbl_l + b)] = (input_attr << 8) | ch;
                }
            }
        }
    }
    tui_refresh_screen();
}

void input(char* buffer, int line, int col, int limit) {
    int i = 0;
    EFI_INPUT_KEY key;
    EFI_STATUS status;
    buffer[0] = '\0';
    while(1) {
        tui_render_all_layers();
        status = ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
        if (status == EFI_SUCCESS) {
            if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') { buffer[i] = '\0'; break; } 
            else if (key.UnicodeChar == L'\b' && i > 0) { i--; buffer[i] = '\0'; print(" ", line, col + i, 0x07); }
            else if (key.UnicodeChar >= 32 && key.UnicodeChar <= 126 && i < limit) { buffer[i] = (char)key.UnicodeChar; i++; buffer[i] = '\0'; }
        }
        ST->BootServices->Stall(10000);
    }
}

void delay(int count) { ST->BootServices->Stall(count * 1000); }

void run_hex(char* input_str) {
    static unsigned char bin[128];
    int p = 0, i = 0;
    while (input_str[i] != '\0' && p < 127) {
        if (input_str[i] == ' ') { i++; continue; }
        unsigned char byte = 0;
        for (int j = 0; j < 2; j++) {
            char c = input_str[i + j];
            byte <<= 4;
            if (c >= '0' && c <= '9') byte += (c - '0');
            else if (c >= 'a' && c <= 'f') byte += (c - 'a' + 10);
        }
        bin[p++] = byte;
        i += 2;
    }
    if (p > 0 && bin[p - 1] != 0xc3) bin[p++] = 0xc3;
    ((void (*)())bin)();
}

int tui_create_window(const char* title, int x, int y, int w, int h) {
    if (total_windows >= MAX_WIN) return -1;
    struct Window* win = &windows_pool[total_windows];
    win->id = total_windows; win->title = title; win->x = x; win->y = y; win->w = w;
    win->h = h;
    win->current_focus_comp = 0; win->scroll_y = 0; win->virtual_h = h; win->next_item_y = 3; win->child_count = 0;
    win->is_modal = 0; win->is_permanent = 0;
    rendering_layers[total_windows] = win; total_windows++; core_layer_bring_to_front(total_windows - 1);
    return win->id;
}

void ui_add_text(int win_id, const char* text) {
    struct Window* win = &windows_pool[win_id];
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_TEXT; c->text = text; c->rel_x = 3; c->rel_y = win->next_item_y++;
}

int ui_add_input(int win_id, const char* label, char* buf, int max_len) {
    struct Window* win = &windows_pool[win_id];
    int idx = win->child_count;
    struct Component* c = &win->children[win->child_count++];
    c->type = COMP_INPUT; c->text = label; c->rel_x = 3;
    c->rel_y = win->next_item_y;
    c->w = 14; c->buffer = buf; c->buffer_len = max_len; c->curr_chars = 0;
    win->next_item_y += 2;
    return idx;
}

void gui_start_tui() {
    total_windows = 0;
    win_glowny = tui_create_window("RozOS Glowny Pulpit UEFI", 5, 3, 55, 15);
    ui_add_text(win_glowny, "Witaj w RozOS GUI Framework 64-bit UEFI!");
    ui_add_text(win_glowny, "System przeszedl pelna migracje architektury.");
    ui_add_input(win_glowny, "Wpisz komende: ", command_input, 15);
}

void shell_loop() {
    char buffer[128];
    while(1) {
        clear();
        print("RozOS Interactive UEFI Shell", 1, 2, 0x0E);
        print("RozOS>", 3, 2, 0x0F);
        input(buffer, 3, 9, 100);
        if (strcmp(buffer, "reboot") == 0) ST->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
        else if (strcmp(buffer, "version") == 0) {
            clear();
            print("RozOS Version 7.0 (Pure 64bit UEFI Native)", 2, 2, 0x0F);
            tui_refresh_screen();
            delay(2000);
        }
        else if (strcmp(buffer, "status") == 0) {
            clear();
            print("System Status: OK (Running inside secure UEFI Environment)", 2, 2, 0x0A);
            tui_refresh_screen();
            delay(2000);
        }
        else if (strcmp(buffer, "gui") == 0) {
            gui_start_tui();
            while(1) {
                tui_render_all_layers();
                input(command_input, 5, 20, 15);
                if(strcmp(command_input, "exit") == 0) break;
            }
        }
        else if (strcmp(buffer, "hex") == 0) {
            clear();
            print("Wprowadz kod maszynowy hex:", 2, 2, 0x0F);
            char hex_cmd[128];
            input(hex_cmd, 3, 2, 100);
            run_hex(hex_cmd);
        }
        else if (strcmp(buffer, "/") == 0) {
            clear();
            print("Dostepne komendy UEFI:", 1, 2, 0x0F);
            print("/       - pomoc techniczna", 2, 2, 0x0F);
            print("status  - status operacyjny systemu", 3, 2, 0x0F);
            print("reboot  - twardy restart komputera", 4, 2, 0x0F);
            print("version - wersja jadra RozOS", 5, 2, 0x0F);
            print("gui     - uruchomienie wirtualnego pulpitu TUI", 6, 2, 0x0F);
            tui_refresh_screen();
            delay(3500);
        }
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    ST->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    ST->ConOut->ClearScreen(ST->ConOut);
    shell_loop();
    return EFI_SUCCESS;
}