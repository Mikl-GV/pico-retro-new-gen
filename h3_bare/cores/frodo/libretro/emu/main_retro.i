/*
 *  main_retro.i — точка входа libretro-ядра: pre_main() строит argv и зовёт
 *  skel_main(); здесь живут Frodo::ReadyToRun() (создание C64, вшитые ROM,
 *  автозапуск образа) и quit_frodo_emu() (освобождение ядра).
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 */

#include "Version.h"

#include "core-log.h"

extern int init_graphics(void);

Frodo *the_app;

// Global variables
char Frodo::prefs_path[256] = "";

char Frodo::device_path[256] = "";

/*
 *  Create application object and start it
 */

int skel_main(int argc, char **argv)
{

	timeval tv;
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	LOGI("%s by Christian Bauer\n", VERSION_STRING);
	if (!init_graphics())
		return 0;
	fflush(stdout);

	the_app = new Frodo();
	the_app->ArgvReceived(argc, argv);
	the_app->ReadyToRun();
	return 0;
}

void quit_frodo_emu(){
	LOGI("delete c64&app\n");
	if (TheC64) { delete TheC64; TheC64 = NULL; }
	if (the_app) { delete the_app; the_app = NULL; }
}

/*
 *  Constructor: Initialize member variables
 */

Frodo::Frodo()
{
	TheC64 = NULL;
}


/*
 *  Process command line arguments
 */

void Frodo::ArgvReceived(int argc, char **argv)
{
/*
	if (argc == 2)
		strncpy(prefs_path, argv[1], 255);
*/

	if (argc == 2){
			strncpy(device_path, argv[1], 255);
	}	

}


/*
 *  Arguments processed, run emulation
 */

void Frodo::ReadyToRun(void)
{
	strcpy(AppDirPath, "/");

	// r1283 (bare-metal): .frodorc на SD не читаем — настройки по умолчанию.
	// Вшитые ROM и диск, заданный host-слоем через device_path, первичнее.
	prefs_path[0] = 0;
	strcpy(prefs_path, "/.frodorc");

	LOGI("pref:(%s) dev:(%s)\n",prefs_path,device_path);

	ThePrefs.Load(prefs_path);

	ThePrefs.set_drive8(device_path,0);

	// Create and start C64
	TheC64 = new C64;

	load_rom_files();

	// r1287: образ из меню. Привод (d64/t64/p00) уже смонтирован конструктором
	// IEC — ThePrefs.DrivePath[0] задан host-слоем до new C64. Команду KERNAL
	// подаём отложенно (AUTBOOT_DELAY), иначе она теряется при инициализации.
	// .prg диском не является: его кладём прямо в RAM и даём RUN или SYS.
	if (device_path[0]) {
		extern void kbd_autoboot_schedule(const char *cmd);
		extern void kbd_prg_schedule(const char *path);
		const char *dot = strrchr(device_path, '.');
		int is_prg = dot && ((dot[1] | 0x20) == 'p') && ((dot[2] | 0x20) == 'r')
		             && ((dot[3] | 0x20) == 'g') && dot[4] == 0;
		if (is_prg)
			kbd_prg_schedule(device_path);
		else
			kbd_autoboot_schedule("\rLOAD\":*\",8,1:\rRUN\r");
	}

	TheC64->Run();

}



/*
 *  Run preferences editor
 */

bool Frodo::RunPrefsEditor(void)
{
	Prefs *prefs = new Prefs(ThePrefs);
	bool result = prefs->ShowEditor(false, prefs_path);
	if (result) {
		TheC64->NewPrefs(prefs);
		ThePrefs = *prefs;
	}
	delete prefs;
	return result;
}


/*
 *  Determine whether path name refers to a directory
 */

bool IsDirectory(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);

}

