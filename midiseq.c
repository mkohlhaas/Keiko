/*
    Copyright (C) 2004 Ian Esten

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <jack/jack.h>
#include <jack/midiport.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

jack_client_t *client;
jack_port_t *output_port;

unsigned char *note_frqs;
jack_nframes_t *note_starts;
jack_nframes_t *note_lengths;
jack_nframes_t num_notes;
jack_nframes_t loop_nsamp;
jack_nframes_t loop_index;

static void signal_handler(int sig) {
  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

static void usage() {
  fprintf(stderr,
          "usage: jack_midiseq name nsamp [startindex note nsamp] "
          "...... [startindex note nsamp]\n");
  fprintf(stderr, "eg: jack_midiseq Sequencer 24000 0 60 8000 12000 63 8000\n");
  fprintf(stderr,
          "will play a 1/2 sec loop (if srate is 48khz) with a c4 note "
          "at the start of the loop\n");
  fprintf(stderr,
          "that lasts for 8000 samples, then a d4# that starts at 1/4 "
          "sec that lasts for 8000 samples\n");
}

static void note_on_off(jack_nframes_t nframes, int i, int j, bool on) {
  void *port_buf = jack_port_get_buffer(output_port, nframes);
  jack_midi_clear_buffer(port_buf);
  unsigned char *buffer = jack_midi_event_reserve(port_buf, i, 3);
  buffer[2] = 64; /* velocity */
  buffer[1] = note_frqs[j];
  buffer[0] = on ? 0x80 : 0x90;
}

static void note_on(jack_nframes_t nframes, int i, int j) {
  note_on_off(nframes, i, j, true);
}

static void note_off(jack_nframes_t nframes, int i, int j) {
  note_on_off(nframes, i, j, false);
}

static void loop_notes(jack_nframes_t nframes, int i) {
  for (int j = 0; j < num_notes; j++) {
    if (note_starts[j] == loop_index) {
      note_on(nframes, i, j);
    } else if (note_starts[j] + note_lengths[j] == loop_index) {
      note_off(nframes, i, j);
    }
  }
  loop_index = loop_index + 1 >= loop_nsamp ? 0 : loop_index + 1;
}

static int process(jack_nframes_t nframes, void *arg) {
  for (int i = 0; i < nframes; i++) {
    loop_notes(nframes, i);
  }
  return 0;
}
int main(int narg, char **args) {
  int i;
  jack_nframes_t nframes;
  if ((narg < 6) || ((narg - 3) % 3 != 0)) {
    usage();
    exit(1);
  }
  if ((client = jack_client_open(args[1], JackNullOption, NULL)) == 0) {
    fprintf(stderr, "JACK server not running?\n");
    return 1;
  }
  jack_set_process_callback(client, process, 0);
  output_port = jack_port_register(client, "midi-out", JACK_DEFAULT_MIDI_TYPE,
                                   JackPortIsOutput, 0);
  nframes = jack_get_buffer_size(client);
  printf("Number of frames: %d\n", nframes);
  loop_index = 0;
  num_notes = (narg - 3) / 3;
  note_frqs = malloc(num_notes * sizeof(unsigned char));
  note_starts = malloc(num_notes * sizeof(jack_nframes_t));
  note_lengths = malloc(num_notes * sizeof(jack_nframes_t));
  loop_nsamp = atoi(args[2]);
  for (i = 0; i < num_notes; i++) {
    note_starts[i] = atoi(args[3 + 3 * i]);
    note_frqs[i] = atoi(args[4 + 3 * i]);
    note_lengths[i] = atoi(args[5 + 3 * i]);
  }

  if (jack_activate(client)) {
    fprintf(stderr, "cannot activate client");
    return 1;
  }

  /* install a signal handler to properly quit jack client */
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  /* run until interrupted */
  sleep(-1);
}
