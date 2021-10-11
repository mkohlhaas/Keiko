#include "keiko.h"

// ==============================================================================  
// ============================== Helper Functions ==============================  
// ==============================================================================  

int
clamp(int val, int min, int max)
{
  return (val >= min) ? ((val <= max) ? val : max) : min;
}

// is c special character?
bool
cisp(char c)
{
  return c == '.' || c == ':' || c == '#' || c == '*';
}

// int 'v' to char
// result has same case as 'c'
char
cchr(int v, char c)
{
  v = abs(v % N_VARS);
  if (v >= 0 && v <= 9) return '0' + v;
  return (c >= 'A' && c <= 'Z' ? 'A' : 'a') + v - 10;
}

// char to 0 <= int <= 35
int
cb36(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  return 0;
}

// to upper-case
char
cuca(char c)
{
  return c >= 'a' && c <= 'z' ? 'A' + c - 'a' : c;
}

// to lower-case
char
clca(char c)
{
  return c >= 'A' && c <= 'Z' ? 'a' + c - 'A' : c;
}

char
cinc(char c)
{
  return cisp(c) ? c : cchr(cb36(c) + 1, c);
}

char
cdec(char c)
{
  return cisp(c) ? c : cchr(cb36(c) - 1, c);
}

bool
valid_position(Grid* g, int x, int y)
{
  return x >= 0 && x <= g->width - 1 && y >= 0 && y <= g->height - 1;
}

bool
valid_character(char c)
{
  return cb36(c) || c == '0' || cisp(c);
}

// char to midi note?
int
ctbl(char c)
{
  int notes[7] = { 0, 2, 4, 5, 7, 9, 11 };
  if (c >= '0' && c <= '9') return c - '0';
  bool sharp = c >= 'a' && c <= 'z';
  int  uc    = sharp ? c - 'a' + 'A' : c;
  int  deg   = uc <= 'B' ? 'G' - 'B' + uc - 'A' : uc - 'C';
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
get_cell(Grid* g, int x, int y)
{
  if (valid_position(g, x, y)) return g->data[x + (y * g->width)];
  return '.';
}

void
set_cell(Grid* g, int x, int y, char c)
{
  if (valid_position(g, x, y) && valid_character(c))
    g->data[x + (y * g->width)] = c;
}

Type
get_type(Grid* g, int x, int y)
{
  if (valid_position(g, x, y))
    return g->type[x + (y * g->width)];
  return NoOp;
}

// set cell's coloring
void
set_type(Grid* g, int x, int y, int type)
{
  if (valid_position(g, x, y))
    g->type[x + (y * g->width)] = type;
}

// deactivate cell (cell contains number/value but not operator)
void
set_lock(Grid* g, int x, int y)
{
  if (valid_position(g, x, y)) {
    g->lock[x + (y * g->width)] = true;
    if (get_type(g, x, y) != NoOp)
        set_type(g, x, y, Comment);
  }
}

// set operator's output
void
set_port(Grid* g, int x, int y, char c)
{
  set_lock(g, x, y);          // output will not turn into an operator
  set_type(g, x, y, Output);
  set_cell(g, x, y, c);
}

// get operator's input
int
get_port(Grid* g, int x, int y, bool lock)
{
  if (lock) {
    set_lock(g, x, y);              // right-hand side of operator cannot be an operator
    set_type(g, x, y, RightInput);
  } else
    set_type(g, x, y, LeftInput);
  return get_cell(g, x, y);
}

bool
get_bang(Grid* g, int x, int y)
{
  return get_cell(g, x - 1, y    ) == '*' ||
         get_cell(g, x + 1, y    ) == '*' ||
         get_cell(g, x    , y - 1) == '*' ||
         get_cell(g, x    , y + 1) == '*';
}

size_t
get_list_length()
{
  size_t    n    = 0;
  MidiList* list = voices;
  while (list) { n++; list = list->next; }
  return n;
}

bool
error(char* msg, const char* err)
{
  printf("Error %s: %s\n", msg, err);
  return false;
}

// ==================================================================  
// ============================== MIDI ==============================  
// ==================================================================  

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
      buffer[0]  = 0x90 + n->channel;
      buffer[1]  = n->value;
      buffer[2]  = n->velocity;
    } else {
      n->length -= nframes;
      if (n->length < 0) {
        buffer         = jack_midi_event_reserve(port_buf, 0, 3);
        buffer[0]      = 0x80 + n->channel;
        buffer[1]      = n->value;
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
send_midi(int channel, int value, int velocity, int length)
{
  MidiList* ml      = malloc(sizeof *ml);
  ml->next          = voices->next;
  voices->next      = ml;
  ml->note.channel  = channel;
  ml->note.value    = value;
  ml->note.velocity = velocity * 3;
  ml->note.length   = length * 60 / (float)BPM * jack_get_sample_rate(client);
  ml->note.trigger  = true;
}

bool
init_midi()
{
  if ((client = jack_client_open("Keiko", JackNullOption, NULL)) == 0)
    return error("Jack", "JACK server not running?\n");
  jack_set_process_callback(client, process, 0);
  output_port = jack_port_register(client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  if (jack_activate(client))
    return error("Jack", "cannot activate client");
  voices       = malloc(sizeof *voices);
  voices->next = NULL;
  voices->note = (MidiNote){};
  return true;
}

// =======================================================================  
// ============================== Operators ==============================  
// =======================================================================  

void
frame()
{
  run_grid(&doc.grid);
  redraw(pixels);
}

void
run_grid(Grid* g)
{
  init_grid_frame(g);
  for (int i = 0; i < g->length; i++) {
    char c = g->data[i];
    int  x = i % g->width;
    int  y = i / g->width;
    if      (c == '.')                                   continue;
    else if (g->lock[i])                                 continue;
    else if (c >= '0' && c <= '9')                       continue;
    else if (c >= 'a' && c <= 'z' && !get_bang(g, x, y)) continue;
    else                                                 operate(g, x, y, c);
  }
  print_type_grid(g);
  g->frame++;
}

void
init_grid_frame(Grid* g)
{
  memset(g->lock, false, MAXSZ * sizeof *g->lock);
  memset(g->type, NoOp,  MAXSZ * sizeof *g->type);
  memset(g->vars, '.',  N_VARS * sizeof *g->vars);
}

void
operate(Grid* g, int x, int y, char c)
{
  set_type(g, x, y, Operator);
  char op = clca(c);
  if      (op == 'a') op_a(g, x, y);           // add(a b)             Outputs sum of inputs.
  else if (op == 'b') op_b(g, x, y);           // subtract(a b)        Outputs difference of inputs.
  else if (op == 'c') op_c(g, x, y);           // clock(rate mod)      Outputs modulo of frame.
  else if (op == 'd') op_d(g, x, y);           // delay(rate mod)      Bangs on modulo of frame.
  else if (op == 'e') op_e(g, x, y, c);        // east                 Moves eastward, or bangs.
  else if (op == 'f') op_f(g, x, y);           // if(a b)              Bangs if inputs are equal.
  else if (op == 'g') op_g(g, x, y);           // generator(x y len)   Writes operands with offset.
  else if (op == 'h') op_h(g, x, y);           // halt                 Halts southward operand.
  else if (op == 'i') op_i(g, x, y);           // increment(step mod)  Increments southward operand.
  else if (op == 'j') op_j(g, x, y, c);        // jumper(val)          Outputs northward operand.
  else if (op == 'k') op_k(g, x, y);           // konkat(len)          Reads multiple variables.
  else if (op == 'l') op_l(g, x, y);           // less(a b)            Outputs smallest of inputs.
  else if (op == 'm') op_m(g, x, y);           // multiply(a b)        Outputs product of inputs.
  else if (op == 'n') op_n(g, x, y, c);        // north                Moves Northward, or bangs.
  else if (op == 'o') op_o(g, x, y);           // read(x y read)       Reads operand with offset.
  else if (op == 'p') op_p(g, x, y);           // push(len key val)    Writes eastward operand.
  else if (op == 'q') op_q(g, x, y);           // query(x y len)       Reads operands with offset.
  else if (op == 'r') op_r(g, x, y);           // random(min max)      Outputs random value.
  else if (op == 's') op_s(g, x, y, c);        // south                Moves southward, or bangs.
  else if (op == 't') op_t(g, x, y);           // track(key len val)   Reads eastward operand.
  else if (op == 'u') op_u(g, x, y);           // uclid(step max)      Bangs on Euclidean rhythm.
  else if (op == 'v') op_v(g, x, y);           // variable(write read) Reads and writes variable.
  else if (op == 'w') op_w(g, x, y, c);        // west                 Moves westward, or bangs.
  else if (op == 'x') op_x(g, x, y);           // write(x y val)       Writes operand with offset.
  else if (op == 'y') op_y(g, x, y, c);        // jymper(val)          Outputs westward operand.
  else if (op == 'z') op_z(g, x, y);           // lerp(rate target)    Transitions operand to input.
  else if (op == '*') set_cell(g, x, y, '.');  // bang                 Bangs neighboring operands.
  else if (op == '#') op_comment(g, x, y);     // comment              Halts a line.
  else if (op == ':') op_midi(g, x, y);        // midi                 Sends a MIDI note.
  else                     printf("Unknown operator[%d,%d]: %c\n", x, y, c);
}

// add(a b); Outputs sum of inputs.
void
op_a(Grid* g, int x, int y)
{
  char a = get_port(g, x - 1, y, false);
  char b = get_port(g, x + 1, y, true);
  set_port(g, x, y + 1, cchr(cb36(a) + cb36(b), b));
}

// subtract(a b); Outputs difference of inputs.
void
op_b(Grid* g, int x, int y)
{
  char a = get_port(g, x - 1, y, false);
  char b = get_port(g, x + 1, y, true);
  set_port(g, x, y + 1, cchr(cb36(a) - cb36(b), b));
}

// clock(rate mod); Outputs modulo of frame.
void
op_c(Grid* g, int x, int y)
{
  char rate  = get_port(g, x - 1, y, false);
  char mod   = get_port(g, x + 1, y, true);
  int  mod_  = cb36(mod);  if (!mod_)  mod_  = 8;
  int  rate_ = cb36(rate); if (!rate_) rate_ = 1;
  set_port(g, x, y + 1, cchr(g->frame / rate_ % mod_, mod));
}

// delay(rate mod); Bangs on modulo of frame.
void
op_d(Grid* g, int x, int y)
{
  char rate  = get_port(g, x - 1, y, false);
  char mod   = get_port(g, x + 1, y, true);
  int  rate_ = cb36(rate); if (!rate_) rate_ = 1;
  int  mod_  = cb36(mod);  if (!mod_)  mod_  = 8;
  set_port(g, x, y + 1, g->frame % (rate_ * mod_) == 0 ? '*' : '.');
}

// east; Moves eastward, or bangs.
void
op_e(Grid* g, int x, int y, char c)
{
  if (x >= g->width - 1 || get_cell(g, x + 1, y) != '.')
    set_cell(g, x, y, '*');
  else {
    set_cell(g, x    , y, '.');
    set_port(g, x + 1, y,  c);
    set_type(g, x + 1, y,  NoOp);
  }
  set_type(g, x, y, NoOp);
}

// if(a b); Bangs if inputs are equal.
void
op_f(Grid* g, int x, int y)
{
  char a = get_port(g, x - 1, y, false);
  char b = get_port(g, x + 1, y, true);
  set_port(g, x, y + 1, a == b ? '*' : '.');
}

// generator(x y len); Writes operands with offset.
void
op_g(Grid* g, int x, int y)
{
  char px   = get_port(g, x - 3, y, false);
  char py   = get_port(g, x - 2, y, false);
  char len  = get_port(g, x - 1, y, false);
  int  len_ = cb36(len); if (!len_) len_ = 1;
  for (int i = 0; i < len_; i++)
    set_port(g, x + i + cb36(px), y + 1 + cb36(py), get_port(g, x + 1 + i, y, true));
}

// halt; Halts southward operand.
void
op_h(Grid* g, int x, int y)
{
  get_port(g, x, y + 1, true);
}

// increment(step mod); Increments southward operand.
void
op_i(Grid* g, int x, int y)
{
  char rate  = get_port(g, x - 1, y    , false);
  char mod   = get_port(g, x + 1, y    , true);
  char val   = get_port(g, x    , y + 1, true);
  int  rate_ = cb36(rate); if (!rate_) rate_ = 1;
  int  mod_  = cb36(mod);  if (!mod_)  mod_  = N_VARS;
  set_port(g, x, y + 1, cchr((cb36(val) + rate_) % mod_, mod));
}

// jumper(val); Outputs northward operand.
void
op_j(Grid* g, int x, int y, char c)
{
  char link = get_port(g, x, y - 1, false);
  if (link != c) {
    int i;
    for (i = 1; y + i < g->height; i++)
      if (get_cell(g, x, y + i) != c) break;
    set_port(g, x, y + i, link);
  }
}

// konkat(len); Reads multiple variables.
void
op_k(Grid* g, int x, int y)
{
  char len  = get_port(g, x - 1, y, false);
  int  len_ = cb36(len); if (!len_) len_ = 1;
  for (int i = 0; i < len_; i++) {
    char key =      get_port(g, x + 1 + i, y    , true);
    if (key != '.') set_port(g, x + 1 + i, y + 1, g->vars[cb36(key)]);
  }
}

// less(a b); Outputs smallest of inputs.
void
op_l(Grid* g, int x, int y)
{
  char a = get_port(g, x - 1, y, false);
  char b = get_port(g, x + 1, y, true);
  set_port(g, x, y + 1, cb36(a) < cb36(b) ? a : b);
}

// multiply(a b); Outputs product of inputs.
void
op_m(Grid* g, int x, int y)
{
  char a = get_port(g, x - 1, y, false);
  char b = get_port(g, x + 1, y, true);
  set_port(g, x, y + 1, cchr(cb36(a) * cb36(b), b));
}

// north; Moves Northward, or bangs.
void
op_n(Grid* g, int x, int y, char c)
{
  if (y <= 0 || get_cell(g, x, y - 1) != '.')
    set_cell(g, x, y    , '*');
  else {
    set_cell(g, x, y    , '.');
    set_port(g, x, y - 1,  c);
    set_type(g, x, y - 1,  NoOp);
  }
  set_type(g, x, y, NoOp);
}

// read(x y read); Reads operand with offset.
void
op_o(Grid* g, int x, int y)
{
  char px = get_port(g, x - 2, y, false);
  char py = get_port(g, x - 1, y, false);
  set_port(g, x, y + 1, get_port(g, x + 1 + cb36(px), y + cb36(py), true));
}

// push(len key val); Writes eastward operand.
void
op_p(Grid* g, int x, int y)
{
  char key  = get_port(g, x - 2, y, false);
  char len  = get_port(g, x - 1, y, false);
  char val  = get_port(g, x + 1, y, true);
  int  len_ = cb36(len); if (!len_) len_ = 1;
  for (int i = 0; i < len_; i++)
    set_lock(g, x + i, y + 1);                      // can only be values not operators
  set_port(g, x + (cb36(key) % len_), y + 1, val);
}

// query(x y len); Reads operands with offset.
void
op_q(Grid* g, int x, int y)
{
  char px   = get_port(g, x - 3, y, false);
  char py   = get_port(g, x - 2, y, false);
  char len  = get_port(g, x - 1, y, false);
  int  len_ = cb36(len); if (!len_) len_ = 1;
  for (int i = 0; i < len_; i++)
    set_port(g, x + 1 - len_ + i, y + 1, get_port(g, x + 1 + cb36(px) + i, y + cb36(py), true));
}

// random(min max); Outputs random value.
void
op_r(Grid* g, int x, int y)
{
  char min  = get_port(g, x - 1, y, false);
  char max  = get_port(g, x + 1, y, true);
  int  max_ = cb36(max); if (!max_)        max_ = N_VARS;
  int  min_ = cb36(min); if (min_ == max_) min_ = max_ - 1;
  Uint key  = (g->random + y * g->width + x) ^ (g->frame << 16);
  key = (key ^ 61U) ^ (key >> 16);
  key =  key + (key << 3);
  key =  key ^ (key >> 4);
  key =  key * 0x27d4eb2d;
  key =  key ^ (key >> 15);
  set_port(g, x, y + 1, cchr(key % (max_ - min_) + min_, max));
}

// south; Moves southward, or bangs.
void
op_s(Grid* g, int x, int y, char c)
{
  if (y >= g->height - 1 || get_cell(g, x, y + 1) != '.')
    set_cell(g, x, y    , '*');
  else {
    set_cell(g, x, y    , '.');
    set_port(g, x, y + 1,  c);
    set_type(g, x, y + 1,  NoOp);
  }
  set_type(g, x, y, NoOp);
}

// track(key len val); Reads eastward operand.
void
op_t(Grid* g, int x, int y)
{
  char key  = get_port(g, x - 2, y, false);
  char len  = get_port(g, x - 1, y, false);
  int  len_ = cb36(len); if (!len_) len_ = 1;
  for (int i = 0; i < len_; i++)
    set_lock(g, x + 1 + i, y);  // can only be values not operators
  set_port(g, x, y + 1, get_port(g, x + 1 + (cb36(key) % len_), y, true));
}

// uclid(step max); Bangs on Euclidean rhythm.
void
op_u(Grid* g, int x, int y)
{
  char step   = get_port(g, x - 1, y, false);
  char max    = get_port(g, x + 1, y, true);
  int  step_  = cb36(step); if (!step_) step_ = 1;
  int  max_   = cb36(max);  if (!max_)  max_  = 8;
  int  bucket = (step_ * (g->frame + max_ - 1)) % max_ + step_;
  set_port(g, x, y + 1, bucket >= max_ ? '*' : '.');
}

// variable(write read); Reads and writes variable.
void
op_v(Grid* g, int x, int y)
{
  char w = get_port(g, x - 1, y, false);
  char r = get_port(g, x + 1, y, true);
  if      (w != '.')             g->vars[cb36(w)] = r;
  else if (w == '.' && r != '.') set_port(g, x, y + 1, g->vars[cb36(r)]);
}

// west; Moves westward, or bangs.
void
op_w(Grid* g, int x, int y, char c)
{
  if (x <= 0 || get_cell(g, x - 1, y) != '.')
    set_cell(g, x    , y, '*');
  else {
    set_cell(g, x    , y, '.');
    set_port(g, x - 1, y,  c);
    set_type(g, x - 1, y,  NoOp);
  }
  set_type(g, x, y, NoOp);
}

// write(x y val); Writes operand with offset.
void
op_x(Grid* g, int x, int y)
{
  char px  = get_port(g, x - 2, y, false);
  char py  = get_port(g, x - 1, y, false);
  char val = get_port(g, x + 1, y, true);
  set_port(g, x + cb36(px), y + cb36(py) + 1, val);
}

// jymper(val); Outputs westward operand.
void
op_y(Grid* g, int x, int y, char c)
{
  int i;
  char link = get_port(g, x - 1, y, false);
  if (link != c) {
    for (i = 1; x + i < g->width; i++)
      if (get_cell(g, x + i, y) != c) break;
    set_port(g, x + i, y, link);
  }
}

// lerp(rate target); Transitions operand to input.
void
op_z(Grid* g, int x, int y)
{
  char rate    = get_port(g, x - 1, y    , false);
  char target  = get_port(g, x + 1, y    , true);
  char val     = get_port(g, x    , y + 1, true);
  int  rate_   = cb36(rate); if (!rate_) rate_ = 1;
  int  target_ = cb36(target);
  int  val_    = cb36(val);
  int  mod     = val_ <= target_ - rate_ ?  rate_ : 
                 val_ >= target_ + rate_ ? -rate_ : target_ - val_;
  set_port(g, x, y + 1, cchr(val_ + mod, target));
}

// comment; Halts a line.
void
op_comment(Grid* g, int x, int y)
{
  for (int i = 1; x + i < g->width; i++) {
    set_lock(g, x + i, y);  // deactivate cells
    if (get_cell(g, x + i, y) == '#') break;
  }
  set_type(g, x, y, Comment);
}

// midi; Sends a MIDI note.
void
op_midi(Grid* g, int x, int y)
{
  int channel  = cb36(get_port(g, x + 1, y, true)); if (channel     == '.') return;
  int octave   = cb36(get_port(g, x + 2, y, true)); if (octave      == '.') return;
  int note     =      get_port(g, x + 3, y, true);  if (cisp(note))         return;
  int velocity =      get_port(g, x + 4, y, true);  if (velocity    == '.') velocity = 'z';
  int length   =      get_port(g, x + 5, y, true);
  if (get_bang(g, x, y)) {
    send_midi(clamp(channel, 0, VOICES - 1),
              12 * octave + ctbl(note),
              clamp(cb36(velocity), 0, N_VARS),
              clamp(cb36(length),   1, N_VARS));
    set_type(g, x, y, Operator);
  } else
    set_type(g, x, y, LeftInput);
}

// =======================================================================
// ============================== Debugging ==============================
// =======================================================================

void
print_data_grid(Grid* g)
{
  for   (int y = 0; y < g->height; y++) {
    for (int x = 0; x < g->width;  x++)
      printf("%c", get_cell(g, x, y));
    putchar('\n');
  }
  printf("========================================\n");
}

void
print_lock_grid(Grid* g)
{
  for   (int y = 0; y < g->height; y++) {
    for (int x = 0; x < g->width;  x++)
      printf("%c", g->lock[x + y * g->width] ? '*' : '.');
    putchar('\n');
  }
  printf("========================================\n");
}

void
print_type_grid(Grid* g)
{
  for   (int y = 0; y < g->height; y++) {
    for (int x = 0; x < g->width;  x++)
      printf("%d", g->type[x + y * g->width]);
    putchar('\n');
  }
  printf("========================================\n");
}

// =====================================================================
// ============================== UI ===================================
// =====================================================================

int
get_font(int x, int y, char c, int type, int sel)
{
  if (c >= 'A' && c <= 'Z')                        return c - 'A' + N_VARS;
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
set_pixel(Uint32* dst, int x, int y, int color)
{
  if (x >= 0 && x < WIDTH - 8 && y >= 0 && y < HEIGHT - 8)
    dst[(y + PAD * 8) * WIDTH + (x + PAD * 8)] = theme[color];
}

void
draw_icon(Uint32* dst, int x, int y, Uint8* icon, int fg, int bg)
{
  for   (int v = 0; v < 8; v++)
    for (int h = 0; h < 8; h++) {
      int clr = (icon[v] >> (7 - h)) & 0x1;
      set_pixel(dst, x + h, y + v, clr == 1 ? fg : bg);
    }
}

void
draw_ui(Uint32* dst)
{
  int n = get_list_length(), bottom = VER * 8 + 8;
  // ---------- cursor -------------------
  draw_icon(dst,  0 * 8, bottom, font[cursor.x % N_VARS], 1                                   , 0);
  draw_icon(dst,  1 * 8, bottom, font[68]               , 1                                   , 0);
  draw_icon(dst,  2 * 8, bottom, font[cursor.y % N_VARS], 1                                   , 0);
  draw_icon(dst,  3 * 8, bottom, icons[2]               , cursor.w > 1 || cursor.h > 1 ? 4 : 3, 0);
  // ---------- frame --------------------
  draw_icon(dst,  5 * 8, bottom, font[(doc.grid.frame / 1296)   % N_VARS] , 1                                    , 0);
  draw_icon(dst,  6 * 8, bottom, font[(doc.grid.frame / N_VARS) % N_VARS] , 1                                    , 0);
  draw_icon(dst,  7 * 8, bottom, font[ doc.grid.frame % N_VARS]           , 1                                    , 0);
  draw_icon(dst,  8 * 8, bottom, icons[PAUSE ? 1 : 0]                     , (doc.grid.frame - 1) % 8 == 0 ? 2 : 3, 0);
  // ---------- speed --------------------
  draw_icon(dst, 10 * 8, bottom, font[(BPM / 100) % 10], 1, 0);
  draw_icon(dst, 11 * 8, bottom, font[(BPM / 10) % 10] , 1, 0);
  draw_icon(dst, 12 * 8, bottom, font[ BPM % 10]       , 1, 0);
  // ---------- io -----------------------
  draw_icon(dst, 13 * 8, bottom, n > 0 ? icons[2 + clamp(n, 0, 6)] : font[70], 2, 0);
  // ---------- generics -----------------
  draw_icon(dst, 15 * 8       , bottom, icons[GUIDES ? 10 : 9], GUIDES      ? 1 : 2, 0);
  draw_icon(dst, (HOR - 1) * 8, bottom, icons[11]             , doc.unsaved ? 2 : 3, 0);
}

void
redraw(Uint32* dst)
{
  Rect* r = &cursor;
  for   (int y = 0; y < VER; y++) {
    for (int x = 0; x < HOR; x++) {
      bool   sel    = x < r->x + r->w && 
                      x >= r->x       && 
                      y < r->y + r->h && 
                      y >= r->y;
      Type   type   = get_type(&doc.grid, x, y);
      Uint8* letter = font[get_font(x, y, get_cell(&doc.grid, x, y), type, sel)];
      int    fg     = 0;
      int    bg     = 0;
      if ((sel && !MODE) || (sel && MODE && doc.grid.frame % 2)) { fg = 0; bg = 4; }
      else if (type == Comment)    fg = 3;
      else if (type == LeftInput)  fg = 1;
      else if (type == Operator)   bg = 1;
      else if (type == RightInput) fg = 2;
      else if (type == Output)     bg = 2;
      else                         fg = 3;
      draw_icon(dst, x * 8, y * 8, letter, fg, bg);
    }
  }
  draw_ui(dst);
  SDL_UpdateTexture (gTexture, NULL, dst, WIDTH * sizeof(Uint32));
  SDL_RenderClear   (gRenderer);
  SDL_RenderCopy    (gRenderer, gTexture, NULL, NULL);
  SDL_RenderPresent (gRenderer);
}

// =======================================================================
// ============================== Documents ==============================
// =======================================================================

void
init_grid(Grid* g, int w, int h)
{
  g->width  = w;
  g->height = h;
  g->length = w * h;
  g->frame  = 0;
  g->random = 1;
  memset(g->data, '.', MAXSZ * sizeof *g->data);
  init_grid_frame(g);
}

void
make_doc(Document* d, char* name)
{
  init_grid(&d->grid, HOR, VER);
  d->unsaved = false;
  scpy(name, d->name, FILE_NAME_SIZE);
  redraw(pixels);
  printf("Made: %s\n", name);
}

bool
open_doc(Document* d, char* name)
{
  char c;
  int x = 0, y = 0;
  FILE* f = fopen(name, "r");
  if (!f) return error("Load", "Invalid input file");
  init_grid(&d->grid, HOR, VER);
  while ((c = fgetc(f)) != EOF && d->grid.length <= MAXSZ) {
    if   (c == '\n') { x = 0; y++; }
    else             { set_cell(&d->grid, x, y, c); x++; }
  }
  d->unsaved = false;
  scpy(name, d->name, FILE_NAME_SIZE);
  redraw(pixels);
  printf("Opened: %s\n", name);
  return true;
}

void
save_doc(Document* d, char* name)
{
  FILE* f = fopen(name, "w");
  for   (int y = 0; y < d->grid.height; y++) {
    for (int x = 0; x < d->grid.width;  x++)
      fputc(get_cell(&d->grid, x, y), f);
    fputc('\n', f);
  }
  fclose(f);
  d->unsaved = false;
  scpy(name, d->name, FILE_NAME_SIZE);
  redraw(pixels);
  printf("Saved: %s\n", name);
}

void
transform(Rect* r, char (*fn)(char))
{
  for   (int y = 0; y < r->h; y++)
    for (int x = 0; x < r->w; x++) {
      int x_ = r->x + x;
      int y_ = r->y + y;
      set_cell(&doc.grid, x_, y_, fn(get_cell(&doc.grid, x_, y_)));
    }
  redraw(pixels);
}

void
set_option(int* i, int v)
{
  *i = v;
  redraw(pixels);
}

void
select1(int x, int y, int w, int h)
{
  Rect r;
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
reset()
{
  MODE   = 0;
  GUIDES = 1;
  select1(cursor.x, cursor.y, 1, 1);
}

void
comment(Rect* r)
{
  char c = get_cell(&doc.grid, r->x, r->y) == '#' ? '.' : '#';
  for (int y = 0; y < r->h; y++) {
    set_cell(&doc.grid, r->x           , r->y + y, c);
    set_cell(&doc.grid, r->x + r->w - 1, r->y + y, c);
  }
  doc.unsaved = true;
  redraw(pixels);
}

void
insert(char c)
{
  for   (int x = 0; x < cursor.w; x++)
    for (int y = 0; y < cursor.h; y++)
      set_cell(&doc.grid, cursor.x + x, cursor.y + y, c);
  if (MODE) move(1, 0, 0);
  doc.unsaved = true;
  redraw(pixels);
}

void
select_option(int option)
{
  if      (option == 3)       select1(cursor.x, cursor.y, 1, 1);
  else if (option == 8)       { PAUSE = 1; frame(); }
  else if (option == 15)      set_option(&GUIDES, !GUIDES);
  else if (option == HOR - 1) save_doc(&doc, doc.name);
}

void
copy_clip(Rect* r, char* c)
{
  int i = 0;
  for   (int y = 0; y < r->h; y++) {
    for (int x = 0; x < r->w; x++)
      c[i++] = get_cell(&doc.grid, r->x + x, r->y + y);
    c[i++] = '\n';
  }
  c[i] = '\0';
  redraw(pixels);
}

void
cut_clip(Rect* r, char* c)
{
  copy_clip(r, c);
  insert('.');
}

void
paste_clip(Rect* r, char* c, int insert)
{
  char ch;
  int i = 0;
  int x = r->x;
  int y = r->y;
  while ((ch = c[i++])) {
    if   (ch == '\n') { x = r->x; y++; }
    else              { set_cell(&doc.grid, x, y, insert && ch == '.' ? get_cell(&doc.grid, x, y) : ch); x++; }
  }
  doc.unsaved = true;
  redraw(pixels);
}

void
move_clip(Rect* r, char* c, int x, int y, int skip)
{
  copy_clip(r, c);
  insert('.');
  move(x, y, skip);
  paste_clip(r, c, 0);
}

void
do_mouse(SDL_Event* event)
{
  int cx = event->motion.x / ZOOM / 8 - PAD;
  int cy = event->motion.y / ZOOM / 8 - PAD;
  switch (event->type) {
    case SDL_MOUSEBUTTONUP:
      DOWN = 0;
      break;
    case SDL_MOUSEBUTTONDOWN:
      if   (cy == VER + 1)   select_option(cx);
      else                 { select1(cx, cy, 1, 1); DOWN = 1; }
      break;
    case SDL_MOUSEMOTION:
      if (DOWN) select1(cursor.x, cursor.y, cx + 1 - cursor.x, cy + 1 - cursor.y);
      break;
  }
}

void
do_key(SDL_Event* event)
{
  int shift = SDL_GetModState() & KMOD_LSHIFT || SDL_GetModState() & KMOD_RSHIFT;
  int ctrl  = SDL_GetModState() & KMOD_LCTRL  || SDL_GetModState() & KMOD_RCTRL;
  int alt   = SDL_GetModState() & KMOD_LALT   || SDL_GetModState() & KMOD_RALT;
  if (ctrl) {
    if      (event->key.keysym.sym == SDLK_n)            make_doc(&doc, "untitled.orca");
    else if (event->key.keysym.sym == SDLK_r)            open_doc(&doc, doc.name);
    else if (event->key.keysym.sym == SDLK_s)            save_doc(&doc, doc.name);
    else if (event->key.keysym.sym == SDLK_h)            set_option(&GUIDES, !GUIDES);
    else if (event->key.keysym.sym == SDLK_i)            set_option(&MODE, !MODE);
    else if (event->key.keysym.sym == SDLK_a)            select1(0, 0, doc.grid.width, doc.grid.height);
    else if (event->key.keysym.sym == SDLK_x)            cut_clip(&cursor, clip);
    else if (event->key.keysym.sym == SDLK_c)            copy_clip(&cursor, clip);
    else if (event->key.keysym.sym == SDLK_v)            paste_clip(&cursor, clip, shift);
    else if (event->key.keysym.sym == SDLK_u)            transform(&cursor, cuca);
    else if (event->key.keysym.sym == SDLK_l)            transform(&cursor, clca);
    else if (event->key.keysym.sym == SDLK_LEFTBRACKET)  transform(&cursor, cinc);
    else if (event->key.keysym.sym == SDLK_RIGHTBRACKET) transform(&cursor, cdec);
    else if (event->key.keysym.sym == SDLK_UP)           move_clip(&cursor, clip,  0, -1, alt);
    else if (event->key.keysym.sym == SDLK_DOWN)         move_clip(&cursor, clip,  0,  1, alt);
    else if (event->key.keysym.sym == SDLK_LEFT)         move_clip(&cursor, clip, -1,  0, alt);
    else if (event->key.keysym.sym == SDLK_RIGHT)        move_clip(&cursor, clip,  1,  0, alt);
    else if (event->key.keysym.sym == SDLK_SLASH)        comment(&cursor);
    else if (event->key.keysym.sym == SDLK_q)            quit();
  } else {
    if 	    (event->key.keysym.sym == SDLK_ESCAPE)       reset();
    else if (event->key.keysym.sym == SDLK_PAGEUP)       set_option(&BPM, BPM + 1);
    else if (event->key.keysym.sym == SDLK_PAGEDOWN)     set_option(&BPM, BPM - 1);
    else if (event->key.keysym.sym == SDLK_UP)           shift ? scale( 0, -1, alt) : move( 0, -1, alt);
    else if (event->key.keysym.sym == SDLK_DOWN)         shift ? scale( 0,  1, alt) : move( 0,  1, alt);
    else if (event->key.keysym.sym == SDLK_LEFT)         shift ? scale(-1,  0, alt) : move(-1,  0, alt);
    else if (event->key.keysym.sym == SDLK_RIGHT)        shift ? scale( 1,  0, alt) : move( 1,  0, alt);
    else if (event->key.keysym.sym == SDLK_SPACE)        { if (!MODE) set_option(&PAUSE, !PAUSE); }
    else if (event->key.keysym.sym == SDLK_BACKSPACE)    { insert('.'); if (MODE) move(-2, 0, alt); }
  }
}

void
do_text(SDL_Event* event)
{
  for (int i = 0; i < SDL_TEXTINPUTEVENT_TEXT_SIZE; i++) {
    char c = event->text.text[i];
    if (c < ' ' || c > '~') break;
    insert(c);
  }
}

bool
create_window()
{
  return gWindow = SDL_CreateWindow("Keiko", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH * ZOOM, HEIGHT * ZOOM, SDL_WINDOW_SHOWN);
}

bool
create_renderer()
{
  return gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
}

bool
create_texture()
{
  return gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);
}

bool
create_pixelbufer()
{
  return pixels = calloc(WIDTH * HEIGHT, sizeof *pixels);
}

bool
create_ui()
{
  if (!create_window())     return error("Window",   SDL_GetError());
  if (!create_renderer())   return error("Renderer", SDL_GetError());
  if (!create_texture())    return error("Texture",  SDL_GetError());
  if (!create_pixelbufer()) return error("Pixels",   "Failed to allocate memory");
  return true;
}

bool
init()
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return error("Init", SDL_GetError());
  if (!create_ui())                 return error("Init", "UI creation failed");
  init_midi();
  return true;
}

void
quit()
{
  free(pixels);
  SDL_DestroyTexture(gTexture);
  SDL_DestroyRenderer(gRenderer);
  SDL_DestroyWindow(gWindow);
  SDL_Quit();
  jack_client_close(client);
  exit(0);
}

// ==================================================================  
// ============================== Main ==============================  
// ==================================================================  

int
main(int argc, char* argv[])
{
  Uint8 tick = 0;
  if (!init()) return error("Init", "Failure");
  if (argc > 1) {
    if (!open_doc(&doc, argv[1])) make_doc(&doc, argv[1]);
  } else make_doc(&doc, "untitled.orca");

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
      else if (event.type == SDL_MOUSEBUTTONUP)   do_mouse(&event);
      else if (event.type == SDL_MOUSEBUTTONDOWN) do_mouse(&event);
      else if (event.type == SDL_MOUSEMOTION)     do_mouse(&event);
      else if (event.type == SDL_KEYDOWN)         do_key(&event);
      else if (event.type == SDL_TEXTINPUT)       do_text(&event);
      else if (event.type == SDL_WINDOWEVENT)     { if (event.window.event == SDL_WINDOWEVENT_EXPOSED) redraw(pixels); }
    }
  }
}
