#include <SDL2/SDL.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

// ==============================================================================  
// ============================== Data Definitions ==============================  
// ==============================================================================  

#define HOR     35
#define VER     25
#define PAD      2
#define VOICES  16
#define DEVICE   0

#define SZ     (HOR * VER * 16)
#define CLIPSZ (HOR * VER) + VER + 1
#define MAXSZ  (HOR * VER)

typedef unsigned char Uint8;
typedef unsigned int  Uint;

typedef enum cell_type { NoOp, Comment, LeftInput, Operator, RightInput, Output, Selected, } Type;

#define N_VARS  36

typedef struct
{
  int    width;
  int    height;
  int    length;
  int    frame;
  int    random;       // seed value for random number generator; default = 1
  Uint8  vars[N_VARS];
  Uint8  data[MAXSZ];
  bool   lock[MAXSZ];  // true = deactivate cell = cell does not contain an operator; false = cell contains a value
  Type   type[MAXSZ];  // determines color representation
} Grid;

#define FILE_NAME_SIZE    256
#define FILE_NAME_DEFAULT "untitled.orca"

typedef struct
{
  bool  unsaved;
  char  name[FILE_NAME_SIZE];
  Grid  grid;
} Document;

typedef struct
{
  int x, y;
  int w, h; // width, height
} Rect;

typedef struct
{
  int  channel;
  int  value;
  int  velocity;
  int  length;
  bool trigger;
} MidiNote;

typedef struct midi_list MidiList;

typedef struct midi_list
{
  MidiNote  note;
  MidiList* next;
} MidiList;

// ==============================================================================  
// ============================== Global Variables ==============================  
// ==============================================================================  

jack_client_t* client;
jack_port_t*   output_port;

Document  doc;
char      clip[CLIPSZ];
MidiList* voices;
Rect      cursor;

int WIDTH  = 8 * HOR + PAD * 8 * 2;
int HEIGHT = 8 * (VER + 2) + PAD * 8 * 2;
int BPM    = 120, DOWN = 0, ZOOM = 2, PAUSE = 0, GUIDES = 1, MODE = 0;  // GUIDES = UI grid (dots), MODE = input mode

Uint32 theme[] = { 0x000000, 0xFFFFFF, 0x72DEC2, 0x666666, 0xffb545 };

Uint8 icons[][8] = {
  { 0x00, 0x00, 0x10, 0x38, 0x7c, 0x38, 0x10, 0x00 }, /* play */
  { 0x00, 0x00, 0x48, 0x24, 0x12, 0x24, 0x48, 0x00 }, /* next */
  { 0x00, 0x00, 0x66, 0x42, 0x00, 0x42, 0x66, 0x00 }, /* skip */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00 }, /* midi:1 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x3e, 0x00 }, /* midi:2 */
  { 0x00, 0x00, 0x00, 0x00, 0x3e, 0x3e, 0x3e, 0x00 }, /* midi:3 */
  { 0x00, 0x00, 0x00, 0x3e, 0x3e, 0x3e, 0x3e, 0x00 }, /* midi:4 */
  { 0x00, 0x00, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x00 }, /* midi:5 */
  { 0x00, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x00 }, /* midi:6 */
  { 0x00, 0x00, 0x00, 0x82, 0x44, 0x38, 0x00, 0x00 }, /* eye open */
  { 0x00, 0x38, 0x44, 0x92, 0x28, 0x10, 0x00, 0x00 }, /* eye closed */
  { 0x10, 0x54, 0x28, 0xc6, 0x28, 0x54, 0x10, 0x00 }  /* unsaved */
};

Uint8 font[][8] = { 
  { 0x00, 0x3c, 0x46, 0x4a, 0x52, 0x62, 0x3c, 0x00 }, /* 0 */
  { 0x00, 0x18, 0x28, 0x08, 0x08, 0x08, 0x3e, 0x00 }, /* 1 */
  { 0x00, 0x3c, 0x42, 0x02, 0x3c, 0x40, 0x7e, 0x00 }, /* 2 */
  { 0x00, 0x3c, 0x42, 0x1c, 0x02, 0x42, 0x3c, 0x00 }, /* 3 */
  { 0x00, 0x08, 0x18, 0x28, 0x48, 0x7e, 0x08, 0x00 }, /* 4 */
  { 0x00, 0x7e, 0x40, 0x7c, 0x02, 0x42, 0x3c, 0x00 }, /* 5 */
  { 0x00, 0x3c, 0x40, 0x7c, 0x42, 0x42, 0x3c, 0x00 }, /* 6 */
  { 0x00, 0x7e, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 }, /* 7 */
  { 0x00, 0x3c, 0x42, 0x3c, 0x42, 0x42, 0x3c, 0x00 }, /* 8 */
  { 0x00, 0x3c, 0x42, 0x42, 0x3e, 0x02, 0x3c, 0x00 }, /* 9 */
  { 0x00, 0x00, 0x3c, 0x02, 0x3e, 0x42, 0x3e, 0x00 }, /* a */
  { 0x00, 0x20, 0x20, 0x3c, 0x22, 0x22, 0x3c, 0x00 }, /* b */
  { 0x00, 0x00, 0x1c, 0x20, 0x20, 0x20, 0x1c, 0x00 }, /* c */
  { 0x00, 0x04, 0x04, 0x3c, 0x44, 0x44, 0x3c, 0x00 }, /* d */
  { 0x00, 0x00, 0x38, 0x44, 0x78, 0x40, 0x3c, 0x00 }, /* e */
  { 0x00, 0x0c, 0x10, 0x18, 0x10, 0x10, 0x10, 0x00 }, /* f */
  { 0x00, 0x00, 0x38, 0x44, 0x44, 0x3c, 0x04, 0x38 }, /* g */
  { 0x00, 0x40, 0x40, 0x78, 0x44, 0x44, 0x44, 0x00 }, /* h */
  { 0x00, 0x08, 0x00, 0x18, 0x08, 0x08, 0x1c, 0x00 }, /* i */
  { 0x00, 0x08, 0x00, 0x08, 0x08, 0x08, 0x48, 0x30 }, /* j */
  { 0x00, 0x20, 0x20, 0x28, 0x30, 0x28, 0x24, 0x00 }, /* k */
  { 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00 }, /* l */
  { 0x00, 0x00, 0x34, 0x2a, 0x2a, 0x2a, 0x2a, 0x00 }, /* m */
  { 0x00, 0x00, 0x78, 0x44, 0x44, 0x44, 0x44, 0x00 }, /* n */
  { 0x00, 0x00, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* o */
  { 0x00, 0x00, 0x38, 0x44, 0x44, 0x78, 0x40, 0x40 }, /* p */
  { 0x00, 0x00, 0x38, 0x44, 0x44, 0x3c, 0x04, 0x06 }, /* q */
  { 0x00, 0x00, 0x2c, 0x30, 0x20, 0x20, 0x20, 0x00 }, /* r */
  { 0x00, 0x00, 0x38, 0x40, 0x38, 0x04, 0x78, 0x00 }, /* s */
  { 0x00, 0x10, 0x38, 0x10, 0x10, 0x10, 0x0c, 0x00 }, /* t */
  { 0x00, 0x00, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* u */
  { 0x00, 0x00, 0x44, 0x44, 0x28, 0x38, 0x10, 0x00 }, /* v */
  { 0x00, 0x00, 0x44, 0x54, 0x54, 0x54, 0x28, 0x00 }, /* w */
  { 0x00, 0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00 }, /* x */
  { 0x00, 0x00, 0x44, 0x44, 0x44, 0x3c, 0x04, 0x38 }, /* y */
  { 0x00, 0x00, 0x7c, 0x08, 0x10, 0x20, 0x7c, 0x00 }, /* z */
  { 0x00, 0x3c, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x00 }, /* A */
  { 0x00, 0x7c, 0x42, 0x7c, 0x42, 0x42, 0x7c, 0x00 }, /* B */
  { 0x00, 0x3c, 0x42, 0x40, 0x40, 0x42, 0x3c, 0x00 }, /* C */
  { 0x00, 0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00 }, /* D */
  { 0x00, 0x7e, 0x40, 0x7c, 0x40, 0x40, 0x7e, 0x00 }, /* E */
  { 0x00, 0x7e, 0x40, 0x40, 0x7c, 0x40, 0x40, 0x00 }, /* F */
  { 0x00, 0x3c, 0x42, 0x40, 0x4e, 0x42, 0x3c, 0x00 }, /* G */
  { 0x00, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x00 }, /* H */
  { 0x00, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x3e, 0x00 }, /* I */
  { 0x00, 0x02, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x00 }, /* J */
  { 0x00, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00 }, /* K */
  { 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7e, 0x00 }, /* L */
  { 0x00, 0x42, 0x66, 0x5a, 0x42, 0x42, 0x42, 0x00 }, /* M */
  { 0x00, 0x42, 0x62, 0x52, 0x4a, 0x46, 0x42, 0x00 }, /* N */
  { 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00 }, /* O */
  { 0x00, 0x7c, 0x42, 0x42, 0x7c, 0x40, 0x40, 0x00 }, /* P */
  { 0x00, 0x3c, 0x42, 0x42, 0x52, 0x4a, 0x3c, 0x02 }, /* Q */
  { 0x00, 0x7c, 0x42, 0x42, 0x7c, 0x44, 0x42, 0x00 }, /* R */
  { 0x00, 0x3c, 0x40, 0x3c, 0x02, 0x42, 0x3c, 0x00 }, /* S */
  { 0x00, 0x7e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 }, /* T */
  { 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00 }, /* U */
  { 0x00, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00 }, /* V */
  { 0x00, 0x42, 0x42, 0x42, 0x5a, 0x66, 0x42, 0x00 }, /* W */
  { 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00 }, /* X */
  { 0x00, 0x82, 0x44, 0x28, 0x10, 0x10, 0x10, 0x00 }, /* Y */
  { 0x00, 0x7e, 0x04, 0x08, 0x10, 0x20, 0x7e, 0x00 }, /* Z */
  { 0x00, 0x5a, 0x24, 0x5a, 0x5a, 0x24, 0x5a, 0x00 }, /* * */
  { 0x00, 0x00, 0x24, 0x7e, 0x24, 0x7e, 0x24, 0x00 }, /* # */
  { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00 }, /* . */
  { 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00 }, /* : */
  { 0x00, 0x00, 0x66, 0x5a, 0x24, 0x5a, 0x66, 0x00 }, /* @ */
  { 0x00, 0x00, 0x00, 0x32, 0x42, 0x4c, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

SDL_Window*   gWindow;
SDL_Renderer* gRenderer;
SDL_Texture*  gTexture;
Uint32*       pixels;

// ==============================================================================  
// ============================== Helper Functions ==============================  
// ==============================================================================  

int    clamp(int val, int min, int max);
bool   cisp(char c);
char   cchr(int v, char c);
int    cb36(char c);
char   cuca(char c);
char   clca(char c);
char   cinc(char c);
char   cdec(char c);
bool   valid_position(Grid* g, int x, int y);
bool   valid_character(char c);
int    ctbl(char c);
char*  scpy(char* src, char* dst, int len);
char   get_cell(Grid* g, int x, int y);
void   set_cell(Grid* g, int x, int y, char c);
Type   get_type(Grid* g, int x, int y);
void   set_type(Grid* g, int x, int y, Type type);
void   set_lock(Grid* g, int x, int y);
void   set_port(Grid* g, int x, int y, char c);
int    get_port(Grid* g, int x, int y, bool lock);
bool   bangged(Grid* g, int x, int y);
size_t get_list_length();
bool   error(char* msg, const char* err);

// ==================================================================
// ============================== MIDI ==============================
// ==================================================================

int  process(jack_nframes_t nframes, void* arg);
void send_midi(int channel, int value, int velocity, int length);
bool init_midi();

// =======================================================================
// ============================== Operators ==============================
// =======================================================================

void operate(Grid* g, int x, int y, char c);
void run_grid(Grid* g);
void init_grid_frame(Grid* g);
void init_grid(Grid* g, int w, int h);
void op_a(Grid* g, int x, int y);
void op_b(Grid* g, int x, int y);
void op_c(Grid* g, int x, int y);
void op_d(Grid* g, int x, int y);
void op_e(Grid* g, int x, int y, char c);
void op_f(Grid* g, int x, int y);
void op_g(Grid* g, int x, int y);
void op_h(Grid* g, int x, int y);
void op_i(Grid* g, int x, int y);
void op_j(Grid* g, int x, int y, char c);
void op_k(Grid* g, int x, int y);
void op_l(Grid* g, int x, int y);
void op_m(Grid* g, int x, int y);
void op_n(Grid* g, int x, int y, char c);
void op_o(Grid* g, int x, int y);
void op_p(Grid* g, int x, int y);
void op_q(Grid* g, int x, int y);
void op_r(Grid* g, int x, int y);
void op_s(Grid* g, int x, int y, char c);
void op_t(Grid* g, int x, int y);
void op_u(Grid* g, int x, int y);
void op_v(Grid* g, int x, int y);
void op_w(Grid* g, int x, int y, char c);
void op_x(Grid* g, int x, int y);
void op_y(Grid* g, int x, int y, char c);
void op_z(Grid* g, int x, int y);
void op_comment(Grid* g, int x, int y);
void op_midi(Grid* g, int x, int y);

// =======================================================================
// ============================== Debugging ==============================
// =======================================================================

void print_data_grid(Grid* g);
void print_lock_grid(Grid* g);
void print_type_grid(Grid* g);

// =====================================================================
// ============================== UI ===================================
// =====================================================================

int  get_font(int x, int y, char c, int type, int sel);
void set_pixel(Uint32* dst, int x, int y, int color);
void draw_icon(Uint32* dst, int x, int y, Uint8* icon, int fg, int bg);
void draw_ui(Uint32* dst);
void redraw(Uint32* dst);

// =======================================================================
// ============================== Documents ==============================
// =======================================================================

void make_doc(Document* d, char* name);
bool open_doc(Document* d, char* name);
void save_doc(Document* d, char* name);
void transform(Rect* r, char (*fn)(char));
void set_option(int* i, int v);
void select1(int x, int y, int w, int h);
void scale(int w, int h, bool skip);
void move(int x, int y, bool skip);
void reset();
void comment(Rect* r);
void insert(char c);
void frame();
void select_option(int option);
void copy_clip(Rect* r, char* c);
void cut_clip(Rect* r, char* c);
void paste_clip(Rect* r, char* c, bool insert);
void move_clip(Rect* r, char* c, int x, int y, bool skip);

// ==========================================================================  
// ============================== Input & Init ==============================  
// ==========================================================================  

void do_mouse(SDL_Event* event);
void do_key(SDL_Event* event);
void do_text(SDL_Event* event);
bool create_window();
bool create_renderer();
bool create_texture();
bool create_pixelbufer();
bool create_ui();
bool init();
void quit();
