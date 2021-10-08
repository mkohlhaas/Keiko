#include <SDL2/SDL.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

#define HOR 32
#define VER 16
#define PAD 2
#define VOICES 16
#define DEVICE 0

#define SZ     (HOR * VER * 16)
#define CLIPSZ (HOR * VER) + VER + 1
#define MAXSZ  (HOR * VER)

typedef unsigned char Uint8;

typedef struct
{
  int   w, h, l, f, r;  // w = width, h = height
  Uint8 var[36], data[MAXSZ], lock[MAXSZ], type[MAXSZ];
} Grid;

typedef struct
{
  bool  unsaved;
  char  name[256];
  Grid  grid;
} Document;

typedef struct
{
  int x, y, w, h;
} Rect2d;

typedef struct
{
  int  chn, val, vel, len;
  bool trigger;
} MidiNote;

typedef struct midi_list MidiList;

typedef struct midi_list
{
  MidiNote  note;
  MidiList* next;
} MidiList;

jack_client_t* client;
jack_port_t*   output_port;

Document  doc;
char      clip[CLIPSZ];
MidiList* voices = NULL;
Rect2d    cursor;

int WIDTH  = 8 * HOR + PAD * 8 * 2;
int HEIGHT = 8 * (VER + 2) + PAD * 8 * 2;
int BPM    = 128, DOWN = 0, ZOOM = 2, PAUSE = 0, GUIDES = 1, MODE = 0;

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

SDL_Window*   gWindow   = NULL;
SDL_Renderer* gRenderer = NULL;
SDL_Texture*  gTexture  = NULL;
Uint32*       pixels;

void
signal_handler(int sig)
{
  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
}

int
clamp(int val, int min, int max)
{
  return (val >= min) ? ((val <= max) ? val : max) : min;
}

// is c special character
bool
cisp(char c)
{
  return c == '.' || c == ':' || c == '#' || c == '*';
}

char
cchr(int v, char c)
{
  v = abs(v % 36);
  if (v >= 0 && v <= 9) return '0' + v;
  return (c >= 'A' && c <= 'Z' ? 'A' : 'a') + v - 10;
}

// char to int; 0 <= int <= 35
int
cb36(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  return 0;
}

// lower-case to upper-case
char
cuca(char c)
{
  return c >= 'a' && c <= 'z' ? 'A' + c - 'a' : c;
}

// upper-case to lower-case
char
clca(char c)
{
  return c >= 'A' && c <= 'Z' ? 'a' + c - 'A' : c;
}

char
cinc(char c)
{
  return !cisp(c) ? cchr(cb36(c) + 1, c) : c;
}

char
cdec(char c)
{
  return !cisp(c) ? cchr(cb36(c) - 1, c) : c;
}

int
validposition(Grid* g, int x, int y)
{
  return x >= 0 && x <= g->w - 1 && y >= 0 && y <= g->h - 1;
}

int
validcharacter(char c)
{
  return cb36(c) || c == '0' || cisp(c);
}

// char to midi note?
int
ctbl(char c)
{
  int notes[7] = { 0, 2, 4, 5, 7, 9, 11 };
  if (c >= '0' && c <= '9') return c - '0';
  bool sharp   = c >= 'a' && c <= 'z';
  int  uc      = sharp ? c - 'a' + 'A' : c;
  int  deg     = uc <= 'B' ? 'G' - 'B' + uc - 'A' : uc - 'C';
  return deg / 7 * 12 + notes[deg % 7] + sharp;
}

// string copy; len includes zero-terminal
char*
scpy(char* src, char* dst, int len)
{
  int i = 0;
  while ((dst[i] = src[i]) && i < len - 2) i++;
  dst[i + 1] = '\0';
  return dst;
}

char
getcell(Grid* g, int x, int y)
{
  if (validposition(g, x, y)) return g->data[x + (y * g->w)];
  return '.';
}

void
setcell(Grid* g, int x, int y, char c)
{
  if (validposition(g, x, y) && validcharacter(c))
    g->data[x + (y * g->w)] = c;
}

int
gettype(Grid* g, int x, int y)
{
  if (validposition(g, x, y)) return g->type[x + (y * g->w)];
  return 0;
}

void
settype(Grid* g, int x, int y, int t)
{
  if (validposition(g, x, y))
    g->type[x + (y * g->w)] = t;
}

void
setlock(Grid* g, int x, int y)
{
  if (validposition(g, x, y)) {
    g->lock[x + (y * g->w)] = 1;
    if (!gettype(g, x, y)) settype(g, x, y, 1);
  }
}

void
setport(Grid* g, int x, int y, char c)
{
  setlock(g, x, y);
  settype(g, x, y, 5);
  setcell(g, x, y, c);
}

int
getport(Grid* g, int x, int y, int l)
{
  if (l) {
    setlock(g, x, y);
    settype(g, x, y, 4);
  } else {
    settype(g, x, y, 2);
  }
  return getcell(g, x, y);
}

int
getbang(Grid* g, int x, int y)
{
  return getcell(g, x - 1, y) == '*' ||
         getcell(g, x + 1, y) == '*' ||
         getcell(g, x, y - 1) == '*' ||
         getcell(g, x, y + 1) == '*';
}

size_t
get_list_length()
{
  size_t    n    = 0;
  MidiList* list = voices;

  while (list->next) {
    n++;
    list = list->next;
  }
  return n;
}

int
process(jack_nframes_t nframes, void* arg)
{
  MidiList*         note_to_delete;
  jack_midi_data_t* buffer;
  void*             port_buf = jack_port_get_buffer(output_port, 1);
  jack_midi_clear_buffer(port_buf);

  MidiList* iter = voices;
  while (iter->next) {
    MidiNote* n = &iter->next->note;
    if (n->trigger) {
      n->trigger = false;
      buffer     = jack_midi_event_reserve(port_buf, 0, 3);
      buffer[0]  = 0x90 + n->chn;
      buffer[1]  = n->val;
      buffer[2]  = n->vel;
    } else {
      n->len -= nframes;
      if (n->len < 0) {
        buffer         = jack_midi_event_reserve(port_buf, 0, 3);
        buffer[0]      = 0x80 + n->chn;
        buffer[1]      = n->val;
        buffer[2]      = 0;
        note_to_delete = iter->next;
        iter->next     = iter->next->next;
        free(note_to_delete);
        continue;
      }
    }
    iter = iter->next;
  }
  return 0;
}

void
sendmidi(int chn, int val, int vel, int len)
{
  MidiList* ml     = malloc(sizeof(MidiList));
  ml->next         = voices->next;
  voices->next     = ml;
  ml->note.chn     = chn;
  ml->note.val     = val;
  ml->note.vel     = vel * 3;
  ml->note.len     = len * 60 / (float)BPM * jack_get_sample_rate(client);
  // fprintf(stderr, "Note length: %d\n", ml->note.len);
  ml->note.trigger = true;
}

void
initmidi()
{
  if ((client = jack_client_open("Keiko", JackNullOption, NULL)) == 0) {
    fprintf(stderr, "JACK server not running?\n");
    exit(1);
  }
  jack_set_process_callback(client, process, 0);
  output_port = jack_port_register(client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  if (jack_activate(client)) {
    fprintf(stderr, "cannot activate client");
    exit(1);
  }
  voices       = malloc(sizeof(MidiList));
  voices->next = NULL;
  voices->note = (MidiNote){};
}

void
opa(Grid* g, int x, int y, char c)
{
  char a = getport(g, x - 1, y, 0);
  char b = getport(g, x + 1, y, 1);
  setport(g, x, y + 1, cchr(cb36(a) + cb36(b), b));
  (void)c;
}

void
opb(Grid* g, int x, int y, char c)
{
  char a = getport(g, x - 1, y, 0);
  char b = getport(g, x + 1, y, 1);
  setport(g, x, y + 1, cchr(cb36(a) - cb36(b), b));
  (void)c;
}

void
opc(Grid* g, int x, int y, char c)
{
  char rate  = getport(g, x - 1, y, 0);
  char mod   = getport(g, x + 1, y, 1);
  int  mod_  = cb36(mod);
  int  rate_ = cb36(rate);
  if (!rate_) rate_ = 1;
  if (!mod_)  mod_  = 8;
  setport(g, x, y + 1, cchr(g->f / rate_ % mod_, mod));
  (void)c;
}

void
opd(Grid* g, int x, int y, char c)
{
  char rate  = getport(g, x - 1, y, 0);
  char mod   = getport(g, x + 1, y, 1);
  int  rate_ = cb36(rate);
  int  mod_  = cb36(mod);
  if (!rate_) rate_ = 1;
  if (!mod_)  mod_  = 8;
  setport(g, x, y + 1, g->f % (rate_ * mod_) == 0 ? '*' : '.');
  (void)c;
}

void
ope(Grid* g, int x, int y, char c)
{
  if (x >= g->w - 1 || getcell(g, x + 1, y) != '.')
    setcell(g, x, y, '*');
  else {
    setcell(g, x,     y, '.');
    setport(g, x + 1, y,  c);
    settype(g, x + 1, y,  0);
  }
  settype(g, x, y, 0);
}

void
opf(Grid* g, int x, int y, char c)
{
  char a = getport(g, x - 1, y, 0);
  char b = getport(g, x + 1, y, 1);
  setport(g, x, y + 1, a == b ? '*' : '.');
  (void)c;
}

void
opg(Grid* g, int x, int y, char c)
{
  char px     = getport(g, x - 3, y, 0);
  char py     = getport(g, x - 2, y, 0);
  char len    = getport(g, x - 1, y, 0);
  int len_    = cb36(len);
  if (!len_) len_ = 1;
  for (int i = 0; i < len_; ++i)
    setport(g, x + i + cb36(px), y + 1 + cb36(py), getport(g, x + 1 + i, y, 1));
  (void)c;
}

void
oph(Grid* g, int x, int y, char c)
{
  getport(g, x, y + 1, 1);
  (void)c;
}

void
opi(Grid* g, int x, int y, char c)
{
  char rate = getport(g, x - 1, y    , 0);
  char mod  = getport(g, x + 1, y    , 1);
  char val  = getport(g, x    , y + 1, 1);
  int rate_ = cb36(rate);
  int mod_  = cb36(mod);
  if (!rate_) rate_ = 1;
  if (!mod_)  mod_  = 36;
  setport(g, x, y + 1, cchr((cb36(val) + rate_) % mod_, mod));
  (void)c;
}

void
opj(Grid* g, int x, int y, char c)
{
  int i;
  char link = getport(g, x, y - 1, 0);
  if (link != c) {
    for (i = 1; y + i < 256; ++i)
      if (getcell(g, x, y + i) != c) break;
    setport(g, x, y + i, link);
  }
}

void
opk(Grid* g, int x, int y, char c)
{
  char len    = getport(g, x - 1, y, 0);
  int i, len_ = cb36(len);
  if (!len_) len_ = 1;
  for (i = 0; i < len_; ++i) {
    char key = getport(g, x + 1 + i, y, 1);
    if (key != '.') setport(g, x + 1 + i, y + 1, g->var[cb36(key)]);
  }
  (void)c;
}

void
opl(Grid* g, int x, int y, char c)
{
  char a = getport(g, x - 1, y, 0);
  char b = getport(g, x + 1, y, 1);
  setport(g, x, y + 1, cb36(a) < cb36(b) ? a : b);
  (void)c;
}

void
opm(Grid* g, int x, int y, char c)
{
  char a = getport(g, x - 1, y, 0);
  char b = getport(g, x + 1, y, 1);
  setport(g, x, y + 1, cchr(cb36(a) * cb36(b), b));
  (void)c;
}

void
opn(Grid* g, int x, int y, char c)
{
  if (y <= 0 || getcell(g, x, y - 1) != '.')
    setcell(g, x, y, '*');
  else {
    setcell(g, x, y    , '.');
    setport(g, x, y - 1,  c);
    settype(g, x, y - 1,  0);
  }
  settype(g, x, y, 0);
}

void
opo(Grid* g, int x, int y, char c)
{
  char px = getport(g, x - 2, y, 0);
  char py = getport(g, x - 1, y, 0);
  setport(g, x, y + 1, getport(g, x + 1 + cb36(px), y + cb36(py), 1));
  (void)c;
}

void
opp(Grid* g, int x, int y, char c)
{
  char key = getport(g, x - 2, y, 0);
  char len = getport(g, x - 1, y, 0);
  char val = getport(g, x + 1, y, 1);
  int len_ = cb36(len);
  if (!len_) len_ = 1;
  for (int i = 0; i < len_; ++i)
    setlock(g, x + i, y + 1);
  setport(g, x + (cb36(key) % len_), y + 1, val);
  (void)c;
}

void
opq(Grid* g, int x, int y, char c)
{
  char px  = getport(g, x - 3, y, 0);
  char py  = getport(g, x - 2, y, 0);
  char len = getport(g, x - 1, y, 0);
  int len_ = cb36(len);
  if (!len_) len_ = 1;
  for (int i = 0; i < len_; ++i)
    setport(g, x + 1 - len_ + i, y + 1, getport(g, x + 1 + cb36(px) + i, y + cb36(py), 1));
  (void)c;
}

void
opr(Grid* g, int x, int y, char c)
{
  char min         = getport(g, x - 1, y, 0);
  char max         = getport(g, x + 1, y, 1);
  int min_         = cb36(min);
  int max_         = cb36(max);
  unsigned int key = (g->r + y * g->w + x) ^ (g->f << 16);
  if (!max_)        max_ = 36;
  if (min_ == max_) min_ = max_ - 1;
  key = (key ^ 61U) ^ (key >> 16);
  key =  key + (key << 3);
  key =  key ^ (key >> 4);
  key =  key * 0x27d4eb2d;
  key =  key ^ (key >> 15);
  setport(g, x, y + 1, cchr(key % (max_ - min_) + min_, max));
  (void)c;
}

void
ops(Grid* g, int x, int y, char c)
{
  if (y >= g->h - 1 || getcell(g, x, y + 1) != '.')
    setcell(g, x, y, '*');
  else {
    setcell(g, x, y    , '.');
    setport(g, x, y + 1,  c);
    settype(g, x, y + 1,  0);
  }
  settype(g, x, y, 0);
}

void
opt(Grid* g, int x, int y, char c)
{
  char key = getport(g, x - 2, y, 0);
  char len = getport(g, x - 1, y, 0);
  int len_ = cb36(len);
  if (!len_) len_ = 1;
  for (int i = 0; i < len_; ++i)
    setlock(g, x + 1 + i, y);
  setport(g, x, y + 1, getport(g, x + 1 + (cb36(key) % len_), y, 1));
  (void)c;
}

void
opu(Grid* g, int x, int y, char c)
{
  char step = getport(g, x - 1, y, 0);
  char max  = getport(g, x + 1, y, 1);
  int step_ = cb36(step);
  int max_  = cb36(max);
  if (!step_) step_ = 1;
  if (!max_)  max_  = 8;
  int bucket = (step_ * (g->f + max_ - 1)) % max_ + step_;
  setport(g, x, y + 1, bucket >= max_ ? '*' : '.');
  (void)c;
}

void
opv(Grid* g, int x, int y, char c)
{
  char w = getport(g, x - 1, y, 0);
  char r = getport(g, x + 1, y, 1);
  if      (w != '.')             g->var[cb36(w)] = r;
  else if (w == '.' && r != '.') setport(g, x, y + 1, g->var[cb36(r)]);
  (void)c;
}

void
opw(Grid* g, int x, int y, char c)
{
  if (x <= 0 || getcell(g, x - 1, y) != '.')
    setcell(g, x, y, '*');
  else {
    setcell(g, x    , y, '.');
    setport(g, x - 1, y,  c);
    settype(g, x - 1, y,  0);
  }
  settype(g, x, y, 0);
}

void
opx(Grid* g, int x, int y, char c)
{
  char px  = getport(g, x - 2, y, 0);
  char py  = getport(g, x - 1, y, 0);
  char val = getport(g, x + 1, y, 1);
  setport(g, x + cb36(px), y + cb36(py) + 1, val);
  (void)c;
}

void
opy(Grid* g, int x, int y, char c)
{
  int i;
  char link = getport(g, x - 1, y, 0);
  if (link != c) {
    for (i = 1; x + i < 256; ++i)
      if (getcell(g, x + i, y) != c) break;
    setport(g, x + i, y, link);
  }
}

void
opz(Grid* g, int x, int y, char c)
{
  char rate    = getport(g, x - 1, y, 0);
  char target  = getport(g, x + 1, y, 1);
  char val     = getport(g, x, y + 1, 1);
  int  rate_   = cb36(rate);
  int  target_ = cb36(target);
  int  val_    = cb36(val);
  if (!rate_) rate_ = 1;
  int mod = val_ <= target_ - rate_ ?  rate_ : 
            val_ >= target_ + rate_ ? -rate_ : target_ - val_;
  setport(g, x, y + 1, cchr(val_ + mod, target));
  (void)c;
}

void
opcomment(Grid* g, int x, int y)
{
  for (int i = 1; x + i < 256; ++i) {
    setlock(g, x + i, y);
    if (getcell(g, x + i, y) == '#') break;
  }
  settype(g, x, y, 1);
}

void
opmidi(Grid* g, int x, int y)
{
  int chn = cb36(getport(g, x + 1, y, 1));
  if (chn == '.') return;
  int oct = cb36(getport(g, x + 2, y, 1));
  if (oct == '.') return;
  int nte = getport(g, x + 3, y, 1);
  if (cisp(nte)) return;
  int vel = getport(g, x + 4, y, 1);
  if (vel == '.') vel = 'z';
  int len = getport(g, x + 5, y, 1);
  if (getbang(g, x, y)) {
    sendmidi(clamp(chn, 0, VOICES - 1),
             12 * oct + ctbl(nte),
             clamp(cb36(vel), 0, 36),
             clamp(cb36(len), 1, 36));
    settype(g, x, y, 3);
  } else {
    settype(g, x, y, 2);
  }
}

void
operate(Grid* g, int x, int y, char c)
{
  settype(g, x, y, 3);
  if      (clca(c) == 'a') opa(g, x, y, c);
  else if (clca(c) == 'b') opb(g, x, y, c);
  else if (clca(c) == 'c') opc(g, x, y, c);
  else if (clca(c) == 'd') opd(g, x, y, c);
  else if (clca(c) == 'e') ope(g, x, y, c);
  else if (clca(c) == 'f') opf(g, x, y, c);
  else if (clca(c) == 'g') opg(g, x, y, c);
  else if (clca(c) == 'h') oph(g, x, y, c);
  else if (clca(c) == 'i') opi(g, x, y, c);
  else if (clca(c) == 'j') opj(g, x, y, c);
  else if (clca(c) == 'k') opk(g, x, y, c);
  else if (clca(c) == 'l') opl(g, x, y, c);
  else if (clca(c) == 'm') opm(g, x, y, c);
  else if (clca(c) == 'n') opn(g, x, y, c);
  else if (clca(c) == 'o') opo(g, x, y, c);
  else if (clca(c) == 'p') opp(g, x, y, c);
  else if (clca(c) == 'q') opq(g, x, y, c);
  else if (clca(c) == 'r') opr(g, x, y, c);
  else if (clca(c) == 's') ops(g, x, y, c);
  else if (clca(c) == 't') opt(g, x, y, c);
  else if (clca(c) == 'u') opu(g, x, y, c);
  else if (clca(c) == 'v') opv(g, x, y, c);
  else if (clca(c) == 'w') opw(g, x, y, c);
  else if (clca(c) == 'x') opx(g, x, y, c);
  else if (clca(c) == 'y') opy(g, x, y, c);
  else if (clca(c) == 'z') opz(g, x, y, c);
  else if (clca(c) == '*') setcell(g, x, y, '.');
  else if (clca(c) == '#') opcomment(g, x, y);
  else if (clca(c) == ':') opmidi(g, x, y);
  else                     printf("Unknown operator[%d,%d]: %c\n", x, y, c);
}

void
initgridframe(Grid* g)
{
  for (int i = 0; i < g->l; ++i) {
    g->lock[i] = 0;
    g->type[i] = 0;
  }
  for (int i = 0; i < 36; ++i)
    g->var[i] = '.';
}

int
rungrid(Grid* g)
{
  initgridframe(g);
  for (int i = 0; i < g->l; ++i) {
    char c = g->data[i];
    int  x = i % g->w;
    int  y = i / g->w;
    if (c == '.' || g->lock[i])                    continue;
    if (c >= '0' && c <= '9')                      continue;
    if (c >= 'a' && c <= 'z' && !getbang(g, x, y)) continue;
    operate(g, x, y, c);
  }
  g->f++;
  return 1;
}

void
initgrid(Grid* g, int w, int h)
{
  g->w = w;
  g->h = h;
  g->l = w * h;
  g->f = 0;
  g->r = 1;
  for (int i = 0; i < g->l; ++i)
    setcell(g, i % g->w, i / g->w, '.');
  initgridframe(g);
}

int
getfont(int x, int y, char c, int type, int sel)
{
  if (c >= 'A' && c <= 'Z')                        return c - 'A' + 36;
  if (c >= 'a' && c <= 'z')                        return c - 'a' + 10;
  if (c >= '0' && c <= '9')                        return c - '0';
  if (c == '*')                                    return 62;
  if (c == '#')                                    return 63;
  if (c == ':')                                    return 65;
  if (cursor.x == x && cursor.y == y)              return 66;
  if (GUIDES) {
    if (x % 8 == 0 && y % 8 == 0)                  return 68;
    if (sel || type || (x % 2 == 0 && y % 2 == 0)) return 64;
  }
  return 70;
}

void
setpixel(Uint32* dst, int x, int y, int color)
{
  if (x >= 0 && x < WIDTH - 8 && y >= 0 && y < HEIGHT - 8)
    dst[(y + PAD * 8) * WIDTH + (x + PAD * 8)] = theme[color];
}

void
drawicon(Uint32* dst, int x, int y, Uint8* icon, int fg, int bg)
{
  for (int v = 0; v < 8; v++)
    for (int h = 0; h < 8; h++) {
      int clr = (icon[v] >> (7 - h)) & 0x1;
      setpixel(dst, x + h, y + v, clr == 1 ? fg : bg);
    }
}

void
drawui(Uint32* dst)
{
  int n = get_list_length(), bottom = VER * 8 + 8;
  // ---------- cursor -------------------
  drawicon(dst, 0 * 8, bottom, font[cursor.x % 36], 1                                   , 0);
  drawicon(dst, 1 * 8, bottom, font[68]           , 1                                   , 0);
  drawicon(dst, 2 * 8, bottom, font[cursor.y % 36], 1                                   , 0);
  drawicon(dst, 3 * 8, bottom, icons[2]           , cursor.w > 1 || cursor.h > 1 ? 4 : 3, 0);
  // ---------- frame --------------------
  drawicon(dst, 5 * 8, bottom, font[(doc.grid.f / 1296) % 36], 1                                , 0);
  drawicon(dst, 6 * 8, bottom, font[(doc.grid.f / 36) % 36]  , 1                                , 0);
  drawicon(dst, 7 * 8, bottom, font[ doc.grid.f % 36]        , 1                                , 0);
  drawicon(dst, 8 * 8, bottom, icons[PAUSE ? 1 : 0]          , (doc.grid.f - 1) % 8 == 0 ? 2 : 3, 0);
  // ---------- speed --------------------
  drawicon(dst, 10 * 8, bottom, font[(BPM / 100) % 10], 1, 0);
  drawicon(dst, 11 * 8, bottom, font[(BPM / 10) % 10] , 1, 0);
  drawicon(dst, 12 * 8, bottom, font[ BPM % 10]       , 1, 0);
  // ---------- io -----------------------
  drawicon(dst, 13 * 8, bottom, n > 0 ? icons[2 + clamp(n, 0, 6)] : font[70], 2, 0);
  // ---------- generics -----------------
  drawicon(dst, 15 * 8       , bottom, icons[GUIDES ? 10 : 9], GUIDES      ? 1 : 2, 0);
  drawicon(dst, (HOR - 1) * 8, bottom, icons[11]             , doc.unsaved ? 2 : 3, 0);
}

void
redraw(Uint32* dst)
{
  Rect2d* r = &cursor;
  for (int y = 0; y < VER; ++y) {
    for (int x = 0; x < HOR; ++x) {
      int    sel     = x < r->x + r->w && 
                       x >= r->x       && 
                       y < r->y + r->h && 
                       y >= r->y;
      int    t       = gettype(&doc.grid, x, y);
      Uint8* letter  = font[getfont(x, y, getcell(&doc.grid, x, y), t, sel)];
      int fg = 0, bg = 0;
      if ((sel && !MODE) || (sel && MODE && doc.grid.f % 2)) {
        fg = 0; bg = 4;
      } else {
        if      (t == 1) fg = 3;
        else if (t == 2) fg = 1;
        else if (t == 3) bg = 1;
        else if (t == 4) fg = 2;
        else if (t == 5) bg = 2;
        else             fg = 3;
      }
      drawicon(dst, x * 8, y * 8, letter, fg, bg);
    }
  }
  drawui(dst);
  SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(Uint32));
  SDL_RenderClear(gRenderer);
  SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
  SDL_RenderPresent(gRenderer);
}

int
error(char* msg, const char* err)
{
  printf("Error %s: %s\n", msg, err);
  return 0;
}

void
makedoc(Document* d, char* name)
{
  initgrid(&d->grid, HOR, VER);
  d->unsaved = false;
  scpy(name, d->name, 256);
  redraw(pixels);
  printf("Made: %s\n", name);
}

int
opendoc(Document* d, char* name)
{
  char c;
  int x = 0, y = 0;
  FILE* f = fopen(name, "r");
  if (!f) return error("Load", "Invalid input file");
  initgrid(&d->grid, HOR, VER);
  while ((c = fgetc(f)) != EOF && d->grid.l <= MAXSZ) {
    if (c == '\n') {
      x = 0;
      y++;
    } else {
      setcell(&d->grid, x, y, c);
      x++;
    }
  }
  d->unsaved = false;
  scpy(name, d->name, 256);
  redraw(pixels);
  printf("Opened: %s\n", name);
  return 1;
}

void
savedoc(Document* d, char* name)
{
  FILE* f = fopen(name, "w");
  for (int y = 0; y < d->grid.h; ++y) {
    for (int x = 0; x < d->grid.w; ++x)
      fputc(getcell(&d->grid, x, y), f);
    fputc('\n', f);
  }
  fclose(f);
  d->unsaved = false;
  scpy(name, d->name, 256);
  redraw(pixels);
  printf("Saved: %s\n", name);
}

void
transform(Rect2d* r, char (*fn)(char))
{
  for (int y = 0; y < r->h; ++y)
    for (int x = 0; x < r->w; ++x)
      setcell(&doc.grid,
              r->x + x,
              r->y + y,
              fn(getcell(&doc.grid, r->x + x, r->y + y)));
  redraw(pixels);
}

void
setoption(int* i, int v)
{
  *i = v;
  redraw(pixels);
}

void
select1(int x, int y, int w, int h)
{
  Rect2d r;
  r.x = clamp(x, 0, HOR - 1);
  r.y = clamp(y, 0, VER - 1);
  r.w = clamp(w, 1, HOR - x);
  r.h = clamp(h, 1, VER - y);
  if (r.x != cursor.x || 
      r.y != cursor.y || 
      r.w != cursor.w ||
      r.h != cursor.h) {
	cursor = r;
    redraw(pixels);
  }
}

void
scale(int w, int h, int skip)
{
  select1(cursor.x,
          cursor.y,
          cursor.w + (w * (skip ? 4 : 1)),
          cursor.h + (h * (skip ? 4 : 1)));
}

void
move(int x, int y, int skip)
{
  select1(cursor.x + (x * (skip ? 4 : 1)),
          cursor.y + (y * (skip ? 4 : 1)),
          cursor.w,
          cursor.h);
}

void
reset(void)
{
  MODE   = 0;
  GUIDES = 1;
  select1(cursor.x, cursor.y, 1, 1);
}

void
comment(Rect2d* r)
{
  char c = getcell(&doc.grid, r->x, r->y) == '#' ? '.' : '#';
  for (int y = 0; y < r->h; ++y) {
    setcell(&doc.grid, r->x, r->y + y, c);
    setcell(&doc.grid, r->x + r->w - 1, r->y + y, c);
  }
  doc.unsaved = true;
  redraw(pixels);
}

void
insert(char c)
{
  for (int x = 0; x < cursor.w; ++x)
    for (int y = 0; y < cursor.h; ++y)
      setcell(&doc.grid, cursor.x + x, cursor.y + y, c);
  if (MODE) move(1, 0, 0);
  doc.unsaved = true;
  redraw(pixels);
}

void
frame(void)
{
  rungrid(&doc.grid);
  redraw(pixels);
}

void
selectoption(int option)
{
  if      (option == 3      ) select1(cursor.x, cursor.y, 1, 1);
  else if (option == 8      ) { PAUSE = 1; frame(); }
  else if (option == 15     ) setoption(&GUIDES, !GUIDES);
  else if (option == HOR - 1) savedoc(&doc, doc.name);
}

void
quit()
{
  free(pixels);
  SDL_DestroyTexture(gTexture);   gTexture  = NULL;
  SDL_DestroyRenderer(gRenderer); gRenderer = NULL;
  SDL_DestroyWindow(gWindow);     gWindow   = NULL;
  SDL_Quit();
  exit(0);
}

void
copyclip(Rect2d* r, char* c)
{
  int i = 0;
  for (int y = 0; y < r->h; ++y) {
    for (int x = 0; x < r->w; ++x)
      c[i++] = getcell(&doc.grid, r->x + x, r->y + y);
    c[i++] = '\n';
  }
  c[i] = '\0';
  redraw(pixels);
}

void
cutclip(Rect2d* r, char* c)
{
  copyclip(r, c);
  insert('.');
}

void
pasteclip(Rect2d* r, char* c, int insert)
{
  char ch;
  int i = 0, x = r->x, y = r->y;
  while ((ch = c[i++])) {
    if (ch == '\n') {
      x = r->x;
      y++;
    } else {
      setcell(&doc.grid, x, y, insert && ch == '.' ? getcell(&doc.grid, x, y) : ch);
      x++;
    }
  }
  doc.unsaved = true;
  redraw(pixels);
}

void
moveclip(Rect2d* r, char* c, int x, int y, int skip)
{
  copyclip(r, c);
  insert('.');
  move(x, y, skip);
  pasteclip(r, c, 0);
}

void
domouse(SDL_Event* event)
{
  int cx = event->motion.x / ZOOM / 8 - PAD;
  int cy = event->motion.y / ZOOM / 8 - PAD;
  switch (event->type) {
    case SDL_MOUSEBUTTONUP:
      DOWN = 0;
      break;
    case SDL_MOUSEBUTTONDOWN:
      if   (cy == VER + 1)   selectoption(cx);
      else                 { select1(cx, cy, 1, 1); DOWN = 1; }
      break;
    case SDL_MOUSEMOTION:
      if (DOWN)
        select1(cursor.x, cursor.y, cx + 1 - cursor.x, cy + 1 - cursor.y);
      break;
  }
}

void
dokey(SDL_Event* event)
{
  int shift = SDL_GetModState() & KMOD_LSHIFT || SDL_GetModState() & KMOD_RSHIFT;
  int ctrl  = SDL_GetModState() & KMOD_LCTRL  || SDL_GetModState() & KMOD_RCTRL;
  int alt   = SDL_GetModState() & KMOD_LALT   || SDL_GetModState() & KMOD_RALT;
  if (ctrl) {
    if      (event->key.keysym.sym == SDLK_n)            makedoc(&doc, "untitled.orca");
    else if (event->key.keysym.sym == SDLK_r)            opendoc(&doc, doc.name);
    else if (event->key.keysym.sym == SDLK_s)            savedoc(&doc, doc.name);
    else if (event->key.keysym.sym == SDLK_h)            setoption(&GUIDES, !GUIDES);
    else if (event->key.keysym.sym == SDLK_i)            setoption(&MODE, !MODE);
    else if (event->key.keysym.sym == SDLK_a)            select1(0, 0, doc.grid.w, doc.grid.h);
    else if (event->key.keysym.sym == SDLK_x)            cutclip(&cursor, clip);
    else if (event->key.keysym.sym == SDLK_c)            copyclip(&cursor, clip);
    else if (event->key.keysym.sym == SDLK_v)            pasteclip(&cursor, clip, shift);
    else if (event->key.keysym.sym == SDLK_u)            transform(&cursor, cuca);
    else if (event->key.keysym.sym == SDLK_l)            transform(&cursor, clca);
    else if (event->key.keysym.sym == SDLK_LEFTBRACKET)  transform(&cursor, cinc);
    else if (event->key.keysym.sym == SDLK_RIGHTBRACKET) transform(&cursor, cdec);
    else if (event->key.keysym.sym == SDLK_UP)           moveclip(&cursor, clip,  0, -1, alt);
    else if (event->key.keysym.sym == SDLK_DOWN)         moveclip(&cursor, clip,  0,  1, alt);
    else if (event->key.keysym.sym == SDLK_LEFT)         moveclip(&cursor, clip, -1,  0, alt);
    else if (event->key.keysym.sym == SDLK_RIGHT)        moveclip(&cursor, clip,  1,  0, alt);
    else if (event->key.keysym.sym == SDLK_SLASH)        comment(&cursor);
  } else {
    if 	    (event->key.keysym.sym == SDLK_ESCAPE)       reset();
    else if (event->key.keysym.sym == SDLK_PAGEUP)       setoption(&BPM, BPM + 1);
    else if (event->key.keysym.sym == SDLK_PAGEDOWN)     setoption(&BPM, BPM - 1);
    else if (event->key.keysym.sym == SDLK_UP)           shift ? scale(0, -1, alt) : move(0, -1, alt);
    else if (event->key.keysym.sym == SDLK_DOWN)         shift ? scale(0, 1, alt) : move(0, 1, alt);
    else if (event->key.keysym.sym == SDLK_LEFT)         shift ? scale(-1, 0, alt) : move(-1, 0, alt);
    else if (event->key.keysym.sym == SDLK_RIGHT)        shift ? scale(1, 0, alt) : move(1, 0, alt);
    else if (event->key.keysym.sym == SDLK_SPACE)        { if (!MODE) setoption(&PAUSE, !PAUSE); }
    else if (event->key.keysym.sym == SDLK_BACKSPACE)    { insert('.'); if (MODE) move(-2, 0, alt); }
  }
}

void
dotext(SDL_Event* event)
{
  for (int i = 0; i < SDL_TEXTINPUTEVENT_TEXT_SIZE; ++i) {
    char c = event->text.text[i];
    if (c < ' ' || c > '~') break;
    insert(c);
  }
}

int
init()
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return error("Init", SDL_GetError());

  gWindow       =  SDL_CreateWindow("Orca", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH * ZOOM, HEIGHT * ZOOM, SDL_WINDOW_SHOWN);
  if (gWindow   == NULL) return error("Window", SDL_GetError());
  gRenderer     =  SDL_CreateRenderer(gWindow, -1, 0);
  if (gRenderer == NULL) return error("Renderer", SDL_GetError());
  gTexture      =  SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);
  if (gTexture  == NULL) return error("Texture", SDL_GetError());
  pixels        =  (Uint32*)malloc(WIDTH * HEIGHT * sizeof(Uint32));
  if (pixels    == NULL) return error("Pixels", "Failed to allocate memory");

  for (int i = 0; i < HEIGHT; i++)
    for (int j = 0; j < WIDTH; j++)
      pixels[i * WIDTH + j] = theme[0];

  initmidi();
  return 1;
}

int
main(int argc, char* argv[])
{
  Uint8 tick = 0;
  if (!init()) return error("Init", "Failure");
  if (argc > 1) {
    if (!opendoc(&doc, argv[1])) makedoc(&doc, argv[1]);
  } else makedoc(&doc, "untitled.orca");

  while (true) {
    SDL_Event event;
    double elapsed, start = SDL_GetPerformanceCounter();
    if (!PAUSE) {
      if   (tick > 3) { frame(); tick = 0; } 
      else            { tick++; }
    }
    elapsed = (SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency() * 1000.0f;
    SDL_Delay(clamp(16.666f - elapsed, 0, 1000));
    while (SDL_PollEvent(&event) != 0) {
      if      (event.type == SDL_QUIT)            quit();
      else if (event.type == SDL_MOUSEBUTTONUP)   domouse(&event);
      else if (event.type == SDL_MOUSEBUTTONDOWN) domouse(&event);
      else if (event.type == SDL_MOUSEMOTION)     domouse(&event);
      else if (event.type == SDL_KEYDOWN)         dokey(&event);
      else if (event.type == SDL_TEXTINPUT)       dotext(&event);
      else if (event.type == SDL_WINDOWEVENT)     { if (event.window.event == SDL_WINDOWEVENT_EXPOSED) redraw(pixels); }
    }
  }
  quit();
}
