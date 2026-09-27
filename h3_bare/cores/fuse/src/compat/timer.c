// Compatibility timer functions

#include <libretro.h>
#include <externs.h>

/* Virtual sleep, in milliseconds, accumulated by compat_timer_sleep().
   fuse/timer/timer.c throttles the emulation to real time with

     while( 1 ) {
       current_time = timer_get_time();
       if( current_time - start_time < 0 ) timer_sleep( TEN_MS );
       else break;
     }

   which assumes the clock keeps moving while the process sleeps. Here it
   does not: the clock is total_time_ms, and the only thing that advances it
   is retro_run(), which is the caller of that loop. Sleeping for real is not
   an option either - a libretro core hands control back to the frontend once
   per frame and the frontend does the pacing - so compat_timer_sleep() has
   always been a no-op, which leaves the loop unable to terminate. It is only
   reachable when sound is off, since timer_frame() returns early otherwise,
   but when it is reached the core spins inside event_do_events() forever and
   the frontend has no way out of it.

   Count the sleep instead. The loop then makes progress and leaves after one
   pass, and start_time, which is only ever set to "ten milliseconds from
   now", stays in step with the clock it is compared against. */
static double slept_ms;

double compat_timer_get_time( void )
{
   return (total_time_ms + slept_ms) / 1000.0;
}

void compat_timer_sleep(int ms)
{
   slept_ms += ms;
}
