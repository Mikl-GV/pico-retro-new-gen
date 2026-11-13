#include <ctype.h>

//Args for experimental_cmdline
#define CMD_ARGMAX   64
#define CMD_ARGLEN   1024

static char ARGUV[CMD_ARGMAX][CMD_ARGLEN];
static unsigned int ARGUC = 0;

// Args for Core
static char XARGV[CMD_ARGMAX][CMD_ARGLEN];
static const char* xargv_cmd[CMD_ARGMAX];
int PARAMCOUNT = 0;

extern int  skel_main(int argc, char *argv[]);
void parse_cmdline(const char *argv);

// r1285: состояние argv сбрасывается на КАЖДЫЙ вход в ядро. Раньше
// PARAMCOUNT обнулялся только при первом Add_Option за всю работу образа
// (static first), а ARGUC не обнулялся никогда: повторный вход в C64 из меню
// добавлял по 2 опции и список рос (в логе — 040/041, 042/043 …). На 64-й
// опции sprintf писал за границу XARGV — порча BSS и зависание.
void Add_Option(const char* option)
{
   if (PARAMCOUNT >= CMD_ARGMAX)
      return;
   if (!option)
      option = "";
   strncpy(XARGV[PARAMCOUNT], option, CMD_ARGLEN - 1);
   XARGV[PARAMCOUNT][CMD_ARGLEN - 1] = 0;
   PARAMCOUNT++;
}

// Одно слово в ARGUV с гарантированным нулём и проверкой границ.
static void add_word(const char *start, const char *end)
{
   if (ARGUC >= CMD_ARGMAX)
      return;
   unsigned int n = 0;
   while (start < end && n < (CMD_ARGLEN - 1))
      ARGUV[ARGUC][n++] = (unsigned char)*start++;
   ARGUV[ARGUC][n] = 0;
   ARGUC++;
}

static int pre_main_busy = 0;   // защита от повторного входа (r1285)

int pre_main(const char *argv)
{
   int i;
   bool Only1Arg;

   // Повторный вход означал бы рекурсию/порчу памяти: не плодим опции.
   if (pre_main_busy)
      return 0;
   pre_main_busy = 1;

   ARGUC = 0;
   PARAMCOUNT = 0;
   for (i = 0; i < CMD_ARGMAX; i++)
      xargv_cmd[i] = NULL;

   parse_cmdline(argv);

   Only1Arg = (ARGUC == 0 || strcmp(ARGUV[0], "skelsdl") != 0) ? 1 : 0;

   Add_Option("skelsdl");

   if (Only1Arg)
      Add_Option(RPATH);
   else
      for (i = 0; i < (int)ARGUC; i++)
         Add_Option(ARGUV[i]);

   for (i = 0; i < PARAMCOUNT; i++)
   {
      xargv_cmd[i] = (char *)(XARGV[i]);
      LOGI("%2d  %s\n", i, XARGV[i]);
   }

   skel_main(PARAMCOUNT, (char **)xargv_cmd);

   if (PARAMCOUNT >= 2)
      xargv_cmd[PARAMCOUNT - 2] = NULL;

   pre_main_busy = 0;
   return 0;
}

void parse_cmdline(const char *argv)
{
   char *p, *p2, *start_of_word;
   int c, c2;
   static char buffer[512 * 4];
   enum states { DULL, IN_WORD, IN_STRING } state = DULL;

   if (!argv)
      return;

   strncpy(buffer, argv, sizeof(buffer) - 2);
   buffer[sizeof(buffer) - 2] = 0;
   strcat(buffer, " ");

   for (p = buffer; *p != '\0'; p++)
   {
      c = (unsigned char) *p; /* convert to unsigned char for is* functions */
      switch (state)
      {
         case DULL: /* not in a word, not in a double quoted string */
            if (isspace(c)) /* still not in a word, so ignore this char */
               continue;
            /* not a space -- if it's a double quote we go to IN_STRING, else to IN_WORD */
            if (c == '"')
            {
               state = IN_STRING;
               start_of_word = p + 1; /* word starts at *next* char, not this one */
               continue;
            }
            state = IN_WORD;
            start_of_word = p; /* word starts here */
            continue;
         case IN_STRING:
            /* we're in a double quoted string, so keep going until we hit a close " */
            if (c == '"')
            {
               /* word goes from start_of_word to p-1 */
               add_word(start_of_word, p);
               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_STRING or we handled the end above */
         case IN_WORD:
            /* we're in a word, so keep going until we get to a space */
            if (isspace(c))
            {
               /* word goes from start_of_word to p-1 */
               add_word(start_of_word, p);
               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_WORD or we handled the end above */
      }
   }
}
