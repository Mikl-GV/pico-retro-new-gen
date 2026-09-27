/* options_enumerate.c: map string-valued settings onto their enum values
   Copyright (c) 2001-2013 Philip Kendall

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

/* Extracted verbatim from fuse/ui/widget/options.c (generated there by
   options.pl from options.dat) when the widget UI was removed: these
   option_enumerate_*() mappings are consumed by the emulation proper -
   sound.c for the speaker type and AY stereo separation, the disk
   peripherals and +3 for the drive types, uimedia.c for disk merging -
   and are the only part of that generated file the core ever executed.
   The combo tables give the canonical spelling of each string setting;
   the last argument of each mapping is the default enum value when the
   setting is unset or not recognised. */

#include <config.h>

#include <string.h>

#include "settings.h"

static int
option_enumerate_combo( const char * const *options, char *value, int def ) {
  int i;
  if( value != NULL ) {
    for( i = 0; options[i] != NULL; i++) {
      if( !strcmp( value, options[ i ] ) )
        return i;
    }
  }
  return def;
}

static const char * const widget_stereo_ay_combo[] = {
  "None",
  "ACB",
  "ABC",
  NULL
};

int
option_enumerate_sound_stereo_ay( void ) {
  return option_enumerate_combo( widget_stereo_ay_combo,
				 settings_current.stereo_ay,
				 0 );
}

static const char * const widget_speaker_type_combo[] = {
  "TV speaker",
  "Beeper",
  "Unfiltered",
  NULL
};

int
option_enumerate_sound_speaker_type( void ) {
  return option_enumerate_combo( widget_speaker_type_combo,
				 settings_current.speaker_type,
				 0 );
}

static const char * const widget_drive_plus3a_type_combo[] = {
  "Single-sided 40 track",
  "Double-sided 40 track",
  "Single-sided 80 track",
  "Double-sided 80 track",
  NULL
};

int
option_enumerate_diskoptions_drive_plus3a_type( void ) {
  return option_enumerate_combo( widget_drive_plus3a_type_combo,
				 settings_current.drive_plus3a_type,
				 0 );
}

static const char * const widget_drive_plus3b_type_combo[] = {
  "Disabled",
  "Single-sided 40 track",
  "Double-sided 40 track",
  "Single-sided 80 track",
  "Double-sided 80 track",
  NULL
};

int
option_enumerate_diskoptions_drive_plus3b_type( void ) {
  return option_enumerate_combo( widget_drive_plus3b_type_combo,
				 settings_current.drive_plus3b_type,
				 4 );
}

#define widget_drive_beta128a_type_combo widget_drive_plus3a_type_combo

int
option_enumerate_diskoptions_drive_beta128a_type( void ) {
  return option_enumerate_combo( widget_drive_beta128a_type_combo,
				 settings_current.drive_beta128a_type,
				 3 );
}

#define widget_drive_beta128b_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_beta128b_type( void ) {
  return option_enumerate_combo( widget_drive_beta128b_type_combo,
				 settings_current.drive_beta128b_type,
				 4 );
}

#define widget_drive_beta128c_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_beta128c_type( void ) {
  return option_enumerate_combo( widget_drive_beta128c_type_combo,
				 settings_current.drive_beta128c_type,
				 4 );
}

#define widget_drive_beta128d_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_beta128d_type( void ) {
  return option_enumerate_combo( widget_drive_beta128d_type_combo,
				 settings_current.drive_beta128d_type,
				 4 );
}

#define widget_drive_plusd1_type_combo widget_drive_plus3a_type_combo

int
option_enumerate_diskoptions_drive_plusd1_type( void ) {
  return option_enumerate_combo( widget_drive_plusd1_type_combo,
				 settings_current.drive_plusd1_type,
				 3 );
}

#define widget_drive_plusd2_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_plusd2_type( void ) {
  return option_enumerate_combo( widget_drive_plusd2_type_combo,
				 settings_current.drive_plusd2_type,
				 4 );
}

#define widget_drive_didaktik80a_type_combo widget_drive_plus3a_type_combo

int
option_enumerate_diskoptions_drive_didaktik80a_type( void ) {
  return option_enumerate_combo( widget_drive_didaktik80a_type_combo,
				 settings_current.drive_didaktik80a_type,
				 3 );
}

#define widget_drive_didaktik80b_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_didaktik80b_type( void ) {
  return option_enumerate_combo( widget_drive_didaktik80b_type_combo,
				 settings_current.drive_didaktik80b_type,
				 4 );
}

#define widget_drive_disciple1_type_combo widget_drive_plus3a_type_combo

int
option_enumerate_diskoptions_drive_disciple1_type( void ) {
  return option_enumerate_combo( widget_drive_disciple1_type_combo,
				 settings_current.drive_disciple1_type,
				 3 );
}

#define widget_drive_disciple2_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_disciple2_type( void ) {
  return option_enumerate_combo( widget_drive_disciple2_type_combo,
				 settings_current.drive_disciple2_type,
				 4 );
}

#define widget_drive_opus1_type_combo widget_drive_plus3a_type_combo

int
option_enumerate_diskoptions_drive_opus1_type( void ) {
  return option_enumerate_combo( widget_drive_opus1_type_combo,
				 settings_current.drive_opus1_type,
				 0 );
}

#define widget_drive_opus2_type_combo widget_drive_plus3b_type_combo

int
option_enumerate_diskoptions_drive_opus2_type( void ) {
  return option_enumerate_combo( widget_drive_opus2_type_combo,
				 settings_current.drive_opus2_type,
				 1 );
}

static const char * const widget_disk_try_merge_combo[] = {
  "Never",
  "With single-sided drives",
  "Always",
  NULL
};

int
option_enumerate_diskoptions_disk_try_merge( void ) {
  return option_enumerate_combo( widget_disk_try_merge_combo,
				 settings_current.disk_try_merge,
				 1 );
}
