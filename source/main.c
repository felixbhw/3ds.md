//---------------------------------------------------------------------------------
// main.c
// A basic markdown note-taking 3DS app using Citro2D for graphics and libctru.
//---------------------------------------------------------------------------------

#include <citro2d.h>
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

//---------------------------------------------------------------------------------
// Definitions and globals
//---------------------------------------------------------------------------------

// Application modes
typedef enum {
    MODE_MENU,       // Main menu: New Note or View Notes
    MODE_NOTE_LIST,  // List of existing notes
    MODE_VIEW_NOTE,  // Viewing a note's content
    MODE_EDIT_NOTE   // Editing note content
} AppMode;

// Structure for note storage
#define MAX_NOTES 10
#define NOTE_CONTENT_LEN 1024
#define TITLE_LEN 32
#define NOTES_DIR "sdmc:/3ds.md/"

// UI Colors
#define COLOR_BG    C2D_Color32(0x18, 0x18, 0x18, 0xFF)  // Dark gray background
#define COLOR_TEXT  C2D_Color32(0xE0, 0xE0, 0xE0, 0xFF)  // Light gray text
#define COLOR_HIGHLIGHT C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)  // White highlight
#define COLOR_TITLE C2D_Color32(0xA0, 0xA0, 0xA0, 0xFF)  // Medium gray title

typedef struct {
    char title[TITLE_LEN];
    char content[NOTE_CONTENT_LEN];
} Note;

static Note notes[MAX_NOTES];
static int note_count = 0;
static int selectedMenu = 0;
static int selectedNote = -1;
static AppMode mode = MODE_MENU;

// For editing note content
static char currentNoteContent[NOTE_CONTENT_LEN];
static char currentNoteTitle[TITLE_LEN];

// Global text resources
static C2D_TextBuf g_staticBuf;

//---------------------------------------------------------------------------------
// Function prototypes
//---------------------------------------------------------------------------------
static void load_notes(void);
static void save_note(const char* title, const char* content);
static void initText(void);
static void exitText(void);
static void safe_string_copy(char* dest, const char* src, size_t dest_size);
static void append_to_note(Note* note, const char* new_content);

//---------------------------------------------------------------------------------
// Helper functions
//---------------------------------------------------------------------------------
static void safe_string_copy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    size_t src_len = strlen(src);
    size_t copy_len = src_len < (dest_size - 1) ? src_len : (dest_size - 1);
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

static void append_to_note(Note* note, const char* new_content) {
    if (!note || !new_content) return;
    
    size_t current_len = strlen(note->content);
    size_t new_len = strlen(new_content);
    
    // Check if we have room for new content + newline + null terminator
    if (current_len + new_len + 2 >= NOTE_CONTENT_LEN) return;
    
    // If this isn't the first line, add a newline
    if (current_len > 0) {
        note->content[current_len] = '\n';
        current_len++;
    }
    
    // Append the new content
    safe_string_copy(note->content + current_len, new_content, NOTE_CONTENT_LEN - current_len);
}

//---------------------------------------------------------------------------------
// Text initialization and cleanup
//---------------------------------------------------------------------------------
static void initText(void) {
    g_staticBuf = C2D_TextBufNew(4096);
}

static void exitText(void) {
    C2D_TextBufDelete(g_staticBuf);
}

//---------------------------------------------------------------------------------
// File operations
//---------------------------------------------------------------------------------
static void ensure_notes_directory(void) {
    DIR* dir = opendir(NOTES_DIR);
    if (!dir) {
        mkdir(NOTES_DIR, 0777);
    } else {
        closedir(dir);
    }
}

static void load_notes(void) {
    ensure_notes_directory();
    note_count = 0;
    
    DIR* dir = opendir(NOTES_DIR);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && note_count < MAX_NOTES) {
        if (entry->d_type == DT_REG) {  // Regular file
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s%s", NOTES_DIR, entry->d_name);
            
            FILE* file = fopen(filepath, "rb");
            if (file) {
                // Copy filename (without extension) as title
                safe_string_copy(notes[note_count].title, entry->d_name, TITLE_LEN);
                
                // Read content
                size_t len = fread(notes[note_count].content, 1, NOTE_CONTENT_LEN - 1, file);
                notes[note_count].content[len] = '\0';
                
                note_count++;
                fclose(file);
            }
        }
    }
    closedir(dir);
}

static void save_note(const char* title, const char* content) {
    ensure_notes_directory();
    
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", NOTES_DIR, title);
    
    FILE* file = fopen(filepath, "wb");
    if (file) {
        size_t len = strlen(content);
        if (len > 0) {
            fwrite(content, 1, len, file);
        }
        fclose(file);
    }
}

//---------------------------------------------------------------------------------
// Main function
//---------------------------------------------------------------------------------
int main(void) {
    // Initialize services
    gfxInitDefault();
    romfsInit();
    
    // Initialize graphics
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    
    // Initialize text resources
    initText();
    if (!g_staticBuf) {
        goto cleanup;
    }
    
    // Create render targets for both screens
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!top || !bottom) {
        goto cleanup;
    }
    
    // Load existing notes
    load_notes();
    
    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        
        if (mode == MODE_MENU && (kDown & KEY_START))
            break;
            
        //-------------- Menu mode input --------------
        if (mode == MODE_MENU) {
            if (kDown & KEY_UP) {
                selectedMenu = (selectedMenu - 1 + 2) % 2;
            }
            if (kDown & KEY_DOWN) {
                selectedMenu = (selectedMenu + 1) % 2;
            }
            if (kDown & KEY_A) {
                if (selectedMenu == 0) {
                    // New Note
                    memset(currentNoteContent, 0, sizeof(currentNoteContent));
                    memset(currentNoteTitle, 0, sizeof(currentNoteTitle));
                    
                    // Get note title first
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, TITLE_LEN-1);
                    swkbdSetHintText(&swkbd, "Enter note title");
                    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
                    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
                    SwkbdButton button = swkbdInputText(&swkbd, currentNoteTitle, sizeof(currentNoteTitle));
                    
                    if (button == SWKBD_BUTTON_RIGHT && strlen(currentNoteTitle) > 0 && note_count < MAX_NOTES) {
                        // Create new note
                        safe_string_copy(notes[note_count].title, currentNoteTitle, TITLE_LEN);
                        notes[note_count].content[0] = '\0';  // Empty content
                        save_note(currentNoteTitle, "");  // Save empty note
                        
                        // Switch to view mode for the new note
                        selectedNote = note_count;
                        note_count++;
                        mode = MODE_VIEW_NOTE;
                    }
                } else {
                    // View Notes
                    if (note_count > 0) {
                        mode = MODE_NOTE_LIST;
                        selectedNote = 0;
                    }
                }
            }
        }
        //-------------- Note List mode input --------------
        else if (mode == MODE_NOTE_LIST) {
            if (kDown & KEY_B) {
                mode = MODE_MENU;
            }
            if (kDown & KEY_UP) {
                selectedNote = (selectedNote - 1 + note_count) % note_count;
            }
            if (kDown & KEY_DOWN) {
                selectedNote = (selectedNote + 1) % note_count;
            }
            if (kDown & KEY_A && selectedNote >= 0) {
                mode = MODE_VIEW_NOTE;
            }
        }
        //-------------- View Note mode input --------------
        else if (mode == MODE_VIEW_NOTE) {
            if (kDown & KEY_B) {
                mode = MODE_NOTE_LIST;
                if (selectedNote >= note_count) {  // If we were viewing a new note
                    mode = MODE_MENU;
                }
            }
            if (kDown & KEY_A) {
                // Add line to note
                SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, NOTE_CONTENT_LEN-1);
                swkbdSetHintText(&swkbd, "Add a line to note");
                swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
                swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Add", true);
                SwkbdButton button = swkbdInputText(&swkbd, currentNoteContent, sizeof(currentNoteContent));
                
                if (button == SWKBD_BUTTON_RIGHT) {
                    append_to_note(&notes[selectedNote], currentNoteContent);
                    save_note(notes[selectedNote].title, notes[selectedNote].content);
                }
            }
        }
        
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        
        // Draw top screen
        C2D_TargetClear(top, COLOR_BG);
        C2D_SceneBegin(top);
        
        C2D_Text text;
        C2D_TextBufClear(g_staticBuf);
        
        // Always show title
        C2D_TextParse(&text, g_staticBuf, "3ds.md");
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter, 200.0f, 20.0f, 0.5f, 1.0f, 1.0f, COLOR_TITLE);
        
        // Show note title and content if viewing a note
        if (mode == MODE_VIEW_NOTE && selectedNote >= 0) {
            // Draw note title
            C2D_TextParse(&text, g_staticBuf, notes[selectedNote].title);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor, 20.0f, 50.0f, 0.5f, 0.85f, 0.85f, COLOR_HIGHLIGHT);
            
            // Draw note content
            C2D_TextParse(&text, g_staticBuf, notes[selectedNote].content);
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor, 20.0f, 80.0f, 0.5f, 0.75f, 0.75f, COLOR_TEXT);
        }
        
        // Draw bottom screen
        C2D_TargetClear(bottom, COLOR_BG);
        C2D_SceneBegin(bottom);
        
        C2D_TextBufClear(g_staticBuf);
        
        if (mode == MODE_MENU) {
            // Draw main menu options
            const char* options[] = {"New Note", "View Notes"};
            for (int i = 0; i < 2; i++) {
                C2D_TextParse(&text, g_staticBuf, options[i]);
                C2D_TextOptimize(&text);
                float y = 100.0f + i * 40.0f;  // Increased spacing between options
                u32 color = (selectedMenu == i) ? COLOR_HIGHLIGHT : COLOR_TEXT;
                C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter, 160.0f, y, 0.5f, 1.0f, 1.0f, color);
            }
        }
        else if (mode == MODE_NOTE_LIST) {
            // Draw note list
            for (int i = 0; i < note_count; i++) {
                C2D_TextParse(&text, g_staticBuf, notes[i].title);
                C2D_TextOptimize(&text);
                float y = 20.0f + i * 30.0f;  // Increased spacing between notes
                u32 color = (selectedNote == i) ? COLOR_HIGHLIGHT : COLOR_TEXT;
                C2D_DrawText(&text, C2D_WithColor, 20.0f, y, 0.5f, 0.75f, 0.75f, color);
            }
            
            // Draw instructions
            C2D_TextParse(&text, g_staticBuf, "A: View  B: Back");
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter, 160.0f, 220.0f, 0.5f, 0.75f, 0.75f, COLOR_TEXT);
        }
        else if (mode == MODE_VIEW_NOTE) {
            // Draw view controls
            C2D_TextParse(&text, g_staticBuf, "A: Add Line  B: Back");
            C2D_TextOptimize(&text);
            C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter, 160.0f, 220.0f, 0.5f, 0.75f, 0.75f, COLOR_TEXT);
        }
        
        C3D_FrameEnd(0);
    }
    
cleanup:
    // Cleanup resources
    exitText();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}