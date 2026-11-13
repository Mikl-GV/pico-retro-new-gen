#include "libretro.h"

#include "libretro-core.h"

// r1283 (bare-metal, NO_LIBCO): без переключения контекста libco.
#include "main.h"
#include "C64.h"
#include "Display.h"
#include "Prefs.h"
#include "SAM.h"
extern C64 *TheC64;
extern void quit_frodo_emu();

int CROP_WIDTH;
int CROP_HEIGHT;
int VIRTUAL_WIDTH ;
int retrow=1024; 
int retroh=1024;

extern int SHIFTON,pauseg,SND ,snd_sampler;
extern short signed int SNDBUF[1024*2];
extern char RPATH[512];
extern char RETRO_DIR[512];

#include "cmdline.c"

extern void update_input(void);
extern void texture_init(void);
extern void texture_uninit(void);
extern void Emu_init();
extern void Emu_uninit();

const char *retro_save_directory;
const char *retro_system_directory;
const char *retro_content_directory;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

static void update_variables(void)
{
   // r1283 (bare-metal): фиксированная геометрия C64 — 384x288 (бордюр VIC-II).
   retrow = WINDOW_WIDTH;
   retroh = WINDOW_HEIGHT;
   CROP_WIDTH = retrow;
   CROP_HEIGHT = (retroh - 80);
   VIRTUAL_WIDTH = retrow;
   texture_init();
}

static void retro_wrap_emulator()
{    
   // r1283 (bare-metal, NO_LIBCO): pre_main() строит TheC64 и грузит ROM,
   // не переключая контекст. Возврат сюда завершает построение эмулятора.
   pre_main(RPATH);
}

void Emu_init(){

#ifdef RETRO_AND
   MOUSEMODE=1;
#endif

   update_variables();

   memset(Key_Sate,0,512);
   memset(Key_Sate2,0,512);

   retro_wrap_emulator();

}

void Emu_uninit(){
	quit_frodo_emu();
   texture_uninit();
}

void retro_shutdown_core(void)
{
   LOGI("SHUTDOWN\n");
	quit_frodo_emu();
   texture_uninit();
   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

void retro_reset(void){

}

void retro_init(void)
{    	
   const char *system_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      // if defined, use the system directory			
      retro_system_directory=system_dir;		
   }		   

   const char *content_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      // if defined, use the system directory			
      retro_content_directory=content_dir;		
   }			

   const char *save_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *save_dir ? save_dir : retro_system_directory;      
   }
   else
   {
      // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
      retro_save_directory=retro_system_directory;
   }

   if(retro_system_directory==NULL)sprintf(RETRO_DIR, "%s\0",".");
   else sprintf(RETRO_DIR, "%s\0", retro_system_directory);

   LOGI("Retro SYSTEM_DIRECTORY %s\n",retro_system_directory);
   LOGI("Retro SAVE_DIRECTORY %s\n",retro_save_directory);
   LOGI("Retro CONTENT_DIRECTORY %s\n",retro_content_directory);

#ifndef RENDER16B
    	enum retro_pixel_format fmt =RETRO_PIXEL_FORMAT_XRGB8888;
#else
    	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
#endif
   
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      LOGI("PIXEL FORMAT is not supported.\n");
   }

	struct retro_input_descriptor inputDescriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" }
	};
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &inputDescriptors);
   texture_init();

}

void retro_deinit(void)
{	 
   Emu_uninit(); 

   LOGI("Retro DeInit\n");
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Frodo";
   info->library_version  = "V4_2";
   info->valid_extensions = "d64|t64|x64|p00|lnx|zip";
   info->need_fullpath    = true;
   info->block_extract = false;

}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   struct retro_game_geometry geom = { retrow, retroh, 1024, 1024,4.0 / 3.0 };
   struct retro_system_timing timing = { 50.0, 44100.0 };

   info->geometry = geom;
   info->timing   = timing;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}
/*
void retro_audiocb(signed short int *sound_buffer,int sndbufsize){
 	int x;
    for(x=0;x<sndbufsize;x++)audio_cb(sound_buffer[x],sound_buffer[x]);	
}
*/

void retro_run(void)
{
   int x;

   bool updated = false;

   // r1285: ядро не построено (init_graphics/new не удались) — кадр не
   // эмулируем. Раньше TheC64->thread_func() шёл по NULL → Data Abort/hang.
   if (!TheC64)
      return;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   if(pauseg==0){
	  
      	if(SND==1)
			for(x=0;x<882;x++)
				audio_cb(SNDBUF[x],SNDBUF[x]);
#ifdef NO_LIBCO
	#ifndef FRODO_SC
		for(x=0;x<312;x++)
	#else
		for(x=0;x<63*312;x++) 
	#endif
		TheC64->thread_func();
#endif

   }   

   video_cb(Retro_Screen,retrow,retroh,retrow<<PIXEL_BYTES);

}
/*
unsigned int lastdown,lastup,lastchar;
static void keyboard_cb(bool down, unsigned keycode,
      uint32_t character, uint16_t mod)
{
   //logging.log(RETRO_LOG_INFO, "Down: %s, Code: %d, Char: %u, Mod: %u.\n",
     //    down ? "yes" : "no", keycode, character, mod);
if(down)lastdown=keycode;
else lastup=keycode;
lastchar=character;
}
*/

bool retro_load_game(const struct retro_game_info *info)
{
   const char *full_path;

   if (!info)
      return false;

/*
   struct retro_keyboard_callback cb = { keyboard_cb };
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cb);
*/
   full_path = info->path ? info->path : "";

   // r1285: путь не должен переполнять RPATH[512] (имя файла с SD бывает
   // длиннее, чем путь, зашитый в Frodo по умолчанию).
   strncpy(RPATH, full_path, sizeof(RPATH) - 1);
   RPATH[sizeof(RPATH) - 1] = 0;

   update_variables();

	memset(Retro_Screen,0,sizeof(Retro_Screen));
	memset(SNDBUF,0,sizeof(SNDBUF));

   Emu_init();

   // r1285: Emu_init мог не построить TheC64 (нет памяти/не открылась
   // поверхность). Тогда сообщать фронтенду об успехе нельзя — иначе он
   // крутит retro_run по NULL.
   if (!TheC64) {
      LOGI("FRODO: init failed (TheC64 is NULL)\n");
      return false;
   }
   return true;
}

void retro_unload_game(void){

   pauseg=0;
   // r1285: освобождаем построенное ядро (C64 + Frodo + поверхность), иначе
   // повторный вход течёт в bump-пуле и оставляет висящий screen.
   quit_frodo_emu();
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data_, size_t size)
{
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

