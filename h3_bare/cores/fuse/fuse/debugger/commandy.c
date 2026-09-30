/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 25 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"


#include <config.h>

#include <stdio.h>		/* Needed by NetBSD yacc */
#include <stdlib.h>
#include <string.h>

#include "debugger/debugger.h"
#include "debugger/debugger_internals.h"
#include "mempool.h"
#include "ui/ui.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

#define YYDEBUG 1
#define YYERROR_VERBOSE

/* The parser's state and value stacks are automatic arrays of YYINITDEPTH
   entries, so this constant is the size of yyparse()'s frame: 4032 bytes
   at the bison default of 200. debugger_check() calls into here from
   readbyte()/writebyte() and so from the z80 instruction loop, which puts
   that frame on the emulation stack on frontends whose core thread has
   little to spare. Measured depth over the debugger's command set is 8,
   and rises by one per level of parenthesis nesting in a condition
   expression; 64 leaves room for 58 levels before the parser has to grow.
   Growing is not an error and not an alloca: YYSTACK_USE_ALLOCA is unset
   in the generated parser, so the stacks move to malloc() and back on
   YYMAXDEPTH terms, verified here to 512 levels. */
#define YYINITDEPTH 64


#line 104 "y.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif


/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    LOGICAL_OR = 258,              /* LOGICAL_OR  */
    LOGICAL_AND = 259,             /* LOGICAL_AND  */
    COMPARISON = 260,              /* COMPARISON  */
    EQUALITY = 261,                /* EQUALITY  */
    NEGATE = 262,                  /* NEGATE  */
    BASE = 263,                    /* BASE  */
    BREAK = 264,                   /* BREAK  */
    TBREAK = 265,                  /* TBREAK  */
    CLEAR = 266,                   /* CLEAR  */
    COMMANDS = 267,                /* COMMANDS  */
    CONDITION = 268,               /* CONDITION  */
    CONTINUE = 269,                /* CONTINUE  */
    DEBUGGER_DELETE = 270,         /* DEBUGGER_DELETE  */
    DISASSEMBLE = 271,             /* DISASSEMBLE  */
    DEBUGGER_END = 272,            /* DEBUGGER_END  */
    EVENT = 273,                   /* EVENT  */
    EXIT = 274,                    /* EXIT  */
    FINISH = 275,                  /* FINISH  */
    IF = 276,                      /* IF  */
    DEBUGGER_IGNORE = 277,         /* DEBUGGER_IGNORE  */
    NEXT = 278,                    /* NEXT  */
    DEBUGGER_OUT = 279,            /* DEBUGGER_OUT  */
    PORT = 280,                    /* PORT  */
    DEBUGGER_PRINT = 281,          /* DEBUGGER_PRINT  */
    READ = 282,                    /* READ  */
    SET = 283,                     /* SET  */
    STEP = 284,                    /* STEP  */
    TIME = 285,                    /* TIME  */
    WRITE = 286,                   /* WRITE  */
    NUMBER = 287,                  /* NUMBER  */
    STRING = 288,                  /* STRING  */
    VARIABLE = 289,                /* VARIABLE  */
    DEBUGGER_ERROR = 290           /* DEBUGGER_ERROR  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define LOGICAL_OR 258
#define LOGICAL_AND 259
#define COMPARISON 260
#define EQUALITY 261
#define NEGATE 262
#define BASE 263
#define BREAK 264
#define TBREAK 265
#define CLEAR 266
#define COMMANDS 267
#define CONDITION 268
#define CONTINUE 269
#define DEBUGGER_DELETE 270
#define DISASSEMBLE 271
#define DEBUGGER_END 272
#define EVENT 273
#define EXIT 274
#define FINISH 275
#define IF 276
#define DEBUGGER_IGNORE 277
#define NEXT 278
#define DEBUGGER_OUT 279
#define PORT 280
#define DEBUGGER_PRINT 281
#define READ 282
#define SET 283
#define STEP 284
#define TIME 285
#define WRITE 286
#define NUMBER 287
#define STRING 288
#define VARIABLE 289
#define DEBUGGER_ERROR 290

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 58 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"


  int token;

  libspectrum_dword integer;
  char *string;

  debugger_breakpoint_type bptype;
  debugger_breakpoint_life bplife;
  struct { libspectrum_word mask, value; } port;
  struct { int source; int page; int offset; } location;

  debugger_expression* exp;


#line 240 "y.tab.c"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);



/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_LOGICAL_OR = 3,                 /* LOGICAL_OR  */
  YYSYMBOL_LOGICAL_AND = 4,                /* LOGICAL_AND  */
  YYSYMBOL_COMPARISON = 5,                 /* COMPARISON  */
  YYSYMBOL_EQUALITY = 6,                   /* EQUALITY  */
  YYSYMBOL_NEGATE = 7,                     /* NEGATE  */
  YYSYMBOL_BASE = 8,                       /* BASE  */
  YYSYMBOL_BREAK = 9,                      /* BREAK  */
  YYSYMBOL_TBREAK = 10,                    /* TBREAK  */
  YYSYMBOL_CLEAR = 11,                     /* CLEAR  */
  YYSYMBOL_COMMANDS = 12,                  /* COMMANDS  */
  YYSYMBOL_CONDITION = 13,                 /* CONDITION  */
  YYSYMBOL_CONTINUE = 14,                  /* CONTINUE  */
  YYSYMBOL_DEBUGGER_DELETE = 15,           /* DEBUGGER_DELETE  */
  YYSYMBOL_DISASSEMBLE = 16,               /* DISASSEMBLE  */
  YYSYMBOL_DEBUGGER_END = 17,              /* DEBUGGER_END  */
  YYSYMBOL_EVENT = 18,                     /* EVENT  */
  YYSYMBOL_EXIT = 19,                      /* EXIT  */
  YYSYMBOL_FINISH = 20,                    /* FINISH  */
  YYSYMBOL_IF = 21,                        /* IF  */
  YYSYMBOL_DEBUGGER_IGNORE = 22,           /* DEBUGGER_IGNORE  */
  YYSYMBOL_NEXT = 23,                      /* NEXT  */
  YYSYMBOL_DEBUGGER_OUT = 24,              /* DEBUGGER_OUT  */
  YYSYMBOL_PORT = 25,                      /* PORT  */
  YYSYMBOL_DEBUGGER_PRINT = 26,            /* DEBUGGER_PRINT  */
  YYSYMBOL_READ = 27,                      /* READ  */
  YYSYMBOL_SET = 28,                       /* SET  */
  YYSYMBOL_STEP = 29,                      /* STEP  */
  YYSYMBOL_TIME = 30,                      /* TIME  */
  YYSYMBOL_WRITE = 31,                     /* WRITE  */
  YYSYMBOL_NUMBER = 32,                    /* NUMBER  */
  YYSYMBOL_STRING = 33,                    /* STRING  */
  YYSYMBOL_VARIABLE = 34,                  /* VARIABLE  */
  YYSYMBOL_DEBUGGER_ERROR = 35,            /* DEBUGGER_ERROR  */
  YYSYMBOL_36_ = 36,                       /* '|'  */
  YYSYMBOL_37_ = 37,                       /* '^'  */
  YYSYMBOL_38_ = 38,                       /* '&'  */
  YYSYMBOL_39_ = 39,                       /* '+'  */
  YYSYMBOL_40_ = 40,                       /* '-'  */
  YYSYMBOL_41_ = 41,                       /* '*'  */
  YYSYMBOL_42_ = 42,                       /* '/'  */
  YYSYMBOL_43_n_ = 43,                     /* '\n'  */
  YYSYMBOL_44_ = 44,                       /* ':'  */
  YYSYMBOL_45_ = 45,                       /* '('  */
  YYSYMBOL_46_ = 46,                       /* ')'  */
  YYSYMBOL_47_ = 47,                       /* '['  */
  YYSYMBOL_48_ = 48,                       /* ']'  */
  YYSYMBOL_YYACCEPT = 49,                  /* $accept  */
  YYSYMBOL_input = 50,                     /* input  */
  YYSYMBOL_command = 51,                   /* command  */
  YYSYMBOL_breakpointlife = 52,            /* breakpointlife  */
  YYSYMBOL_breakpointtype = 53,            /* breakpointtype  */
  YYSYMBOL_breakpointport = 54,            /* breakpointport  */
  YYSYMBOL_breakpointlocation = 55,        /* breakpointlocation  */
  YYSYMBOL_portbreakpointtype = 56,        /* portbreakpointtype  */
  YYSYMBOL_optionalcondition = 57,         /* optionalcondition  */
  YYSYMBOL_numberorpc = 58,                /* numberorpc  */
  YYSYMBOL_expressionornull = 59,          /* expressionornull  */
  YYSYMBOL_number = 60,                    /* number  */
  YYSYMBOL_expression = 61,                /* expression  */
  YYSYMBOL_debuggercommands = 62,          /* debuggercommands  */
  YYSYMBOL_debuggercommand = 63            /* debuggercommand  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  46
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   265

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  49
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  15
/* YYNRULES -- Number of rules.  */
#define YYNRULES  68
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  128

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   290


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      43,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    38,     2,
      45,    46,    41,    39,     2,    40,     2,    42,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    44,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    47,     2,    48,    37,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    36,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   155,   155,   156,   157,   158,   161,   162,   166,   171,
     175,   179,   183,   184,   185,   188,   189,   190,   191,   192,
     193,   194,   197,   198,   199,   200,   201,   202,   203,   206,
     207,   210,   211,   212,   215,   216,   219,   220,   232,   233,
     236,   237,   240,   241,   244,   245,   248,   251,   254,   257,
     260,   261,   265,   266,   270,   274,   278,   282,   286,   290,
     294,   298,   302,   306,   310,   316,   324,   325,   330
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "LOGICAL_OR",
  "LOGICAL_AND", "COMPARISON", "EQUALITY", "NEGATE", "BASE", "BREAK",
  "TBREAK", "CLEAR", "COMMANDS", "CONDITION", "CONTINUE",
  "DEBUGGER_DELETE", "DISASSEMBLE", "DEBUGGER_END", "EVENT", "EXIT",
  "FINISH", "IF", "DEBUGGER_IGNORE", "NEXT", "DEBUGGER_OUT", "PORT",
  "DEBUGGER_PRINT", "READ", "SET", "STEP", "TIME", "WRITE", "NUMBER",
  "STRING", "VARIABLE", "DEBUGGER_ERROR", "'|'", "'^'", "'&'", "'+'",
  "'-'", "'*'", "'/'", "'\\n'", "':'", "'('", "')'", "'['", "']'",
  "$accept", "input", "command", "breakpointlife", "breakpointtype",
  "breakpointport", "breakpointlocation", "portbreakpointtype",
  "optionalcondition", "numberorpc", "expressionornull", "number",
  "expression", "debuggercommands", "debuggercommand", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-31)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-3)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      77,   -31,   121,   -31,   -31,   121,   121,   -23,   -31,   121,
     121,   121,   -31,   -17,   -31,   121,   121,     0,   -31,     2,
     -31,   234,   121,   -31,   -24,   -31,   121,   121,   121,   121,
     -31,   174,   -31,   -31,   -16,   121,   -31,   -31,   -31,   174,
     121,    -8,   -31,   121,   -18,   121,   -31,   227,     4,   -21,
     -31,   121,   -31,   124,   -31,     6,   -31,   -31,   106,    25,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     121,     7,   -31,   -31,   -31,   -31,     9,   -31,   -31,    31,
     -31,   -31,   121,    23,    32,    23,   -31,   -31,   -31,   -31,
     186,   167,    98,    74,   192,    30,    42,   -19,   -19,   -31,
     -31,     3,   -12,   -31,   121,   -30,    23,    50,   121,   -31,
     142,   -31,   -31,   -31,   -31,   -31,    23,    23,   -31,   121,
     174,   -24,    51,   -31,   -31,   -31,   121,   -31
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     4,     0,    29,    30,    42,     0,     0,    15,    16,
       0,    44,    20,     0,    22,     0,     0,     0,    28,     0,
       3,    31,     0,    47,     0,    49,     0,     0,     0,     0,
       6,    46,    12,    43,     0,    44,    17,    18,    19,    45,
       0,     0,    24,     0,     0,     0,     1,     0,     0,     0,
      32,     0,    33,    42,    54,     0,    52,    53,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    14,    21,    23,    25,     0,    26,     5,     0,
      38,    39,     0,    40,     0,    40,    36,    48,    50,    51,
      65,    64,    60,    59,    63,    62,    61,    55,    56,    57,
      58,     0,     0,    66,     0,     0,    40,    34,     0,     9,
       0,     7,    68,    13,    67,    27,    40,    40,     8,     0,
      41,    48,     0,    10,    11,    35,     0,    37
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -31,   -31,    27,   -31,   -31,   -31,   -31,   -31,    19,    54,
      83,    -2,   -10,   -31,    17
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,    19,    20,    21,    53,   106,    85,    82,   109,    32,
      38,    33,    31,   102,   103
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      30,    39,    46,   116,    34,   113,    80,    36,    37,    35,
      81,   117,    54,    41,    42,    40,    56,    57,    58,    59,
      55,   101,    69,    70,    74,    39,    76,    71,    60,    61,
      62,    63,    43,    44,    45,    62,    63,    79,    73,    87,
     101,    75,   104,    77,   108,    47,   112,    62,    63,    83,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,    64,    65,    66,    67,    68,    69,    70,    66,    67,
      68,    69,    70,    89,    78,   105,   110,    -2,     1,    62,
     107,    67,    68,    69,    70,     2,     3,     4,     5,     6,
       7,     8,     9,    10,   119,   126,    11,    12,   120,    13,
      14,    15,   115,    16,   111,    17,    18,    86,   122,    60,
      61,    62,    63,    67,    68,    69,    70,   125,    72,   114,
      -2,     0,     0,     0,   127,   118,     0,     0,    22,     0,
       0,    22,     0,     0,     0,   123,   124,    67,    68,    69,
      70,     0,    64,    65,    66,    67,    68,    69,    70,    22,
       0,     0,    88,    23,    24,    25,    23,    84,    25,     0,
      26,    27,     0,    26,    27,     0,    28,     0,    29,    28,
       0,    29,    62,    63,    23,   121,    25,    60,    61,    62,
      63,    26,    27,     0,     0,     0,     0,    28,     0,    29,
      61,    62,    63,     0,     0,     0,     0,    62,    63,     0,
       0,     0,     0,    64,    65,    66,    67,    68,    69,    70,
      64,    65,    66,    67,    68,    69,    70,     0,     0,     0,
       0,     0,    64,    65,    66,    67,    68,    69,    70,    65,
      66,    67,    68,    69,    70,     2,     3,     4,     5,     6,
       7,     8,     9,    10,     0,     0,    11,    12,     0,    13,
      14,    15,    48,    16,     0,    17,    18,     0,     0,    49,
       0,    50,     0,     0,    51,    52
};

static const yytype_int8 yycheck[] =
{
       2,    11,     0,    33,     6,    17,    27,     9,    10,    32,
      31,    41,    22,    15,    16,    32,    26,    27,    28,    29,
      44,    33,    41,    42,    32,    35,    44,    43,     3,     4,
       5,     6,    32,    33,    34,     5,     6,    33,    40,    33,
      33,    43,    33,    45,    21,    43,    43,     5,     6,    51,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    36,    37,    38,    39,    40,    41,    42,    38,    39,
      40,    41,    42,    48,    47,    44,    44,     0,     1,     5,
      82,    39,    40,    41,    42,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    44,    44,    19,    20,   108,    22,
      23,    24,   104,    26,    85,    28,    29,    53,   110,     3,
       4,     5,     6,    39,    40,    41,    42,   119,    35,   102,
      43,    -1,    -1,    -1,   126,   106,    -1,    -1,     7,    -1,
      -1,     7,    -1,    -1,    -1,   116,   117,    39,    40,    41,
      42,    -1,    36,    37,    38,    39,    40,    41,    42,     7,
      -1,    -1,    46,    32,    33,    34,    32,    33,    34,    -1,
      39,    40,    -1,    39,    40,    -1,    45,    -1,    47,    45,
      -1,    47,     5,     6,    32,    33,    34,     3,     4,     5,
       6,    39,    40,    -1,    -1,    -1,    -1,    45,    -1,    47,
       4,     5,     6,    -1,    -1,    -1,    -1,     5,     6,    -1,
      -1,    -1,    -1,    36,    37,    38,    39,    40,    41,    42,
      36,    37,    38,    39,    40,    41,    42,    -1,    -1,    -1,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    37,
      38,    39,    40,    41,    42,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    -1,    -1,    19,    20,    -1,    22,
      23,    24,    18,    26,    -1,    28,    29,    -1,    -1,    25,
      -1,    27,    -1,    -1,    30,    31
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     1,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    19,    20,    22,    23,    24,    26,    28,    29,    50,
      51,    52,     7,    32,    33,    34,    39,    40,    45,    47,
      60,    61,    58,    60,    60,    32,    60,    60,    59,    61,
      32,    60,    60,    32,    33,    34,     0,    43,    18,    25,
      27,    30,    31,    53,    61,    44,    61,    61,    61,    61,
       3,     4,     5,     6,    36,    37,    38,    39,    40,    41,
      42,    43,    59,    60,    32,    60,    44,    60,    51,    33,
      27,    31,    56,    60,    33,    55,    58,    33,    46,    48,
      61,    61,    61,    61,    61,    61,    61,    61,    61,    61,
      61,    33,    62,    63,    33,    44,    54,    60,    21,    57,
      44,    57,    43,    17,    63,    60,    33,    41,    57,    44,
      61,    33,    60,    57,    57,    60,    44,    60
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    49,    50,    50,    50,    50,    51,    51,    51,    51,
      51,    51,    51,    51,    51,    51,    51,    51,    51,    51,
      51,    51,    51,    51,    51,    51,    51,    51,    51,    52,
      52,    53,    53,    53,    54,    54,    55,    55,    56,    56,
      57,    57,    58,    58,    59,    59,    60,    61,    61,    61,
      61,    61,    61,    61,    61,    61,    61,    61,    61,    61,
      61,    61,    61,    61,    61,    61,    62,    62,    63
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     1,     1,     3,     2,     4,     5,     4,
       6,     6,     2,     5,     3,     1,     1,     2,     2,     2,
       1,     3,     1,     3,     2,     3,     3,     5,     1,     1,
       1,     0,     1,     1,     1,     3,     1,     5,     1,     1,
       0,     2,     0,     1,     0,     1,     1,     1,     3,     1,
       3,     3,     2,     2,     2,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     1,     2,     2
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 6: /* command: BASE number  */
#line 161 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                       { debugger_output_base = (yyvsp[0].integer); }
#line 1390 "y.tab.c"
    break;

  case 7: /* command: breakpointlife breakpointtype breakpointlocation optionalcondition  */
#line 162 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                                              {
             debugger_breakpoint_add_address( (yyvsp[-2].bptype), (yyvsp[-1].location).source, (yyvsp[-1].location).page, (yyvsp[-1].location).offset,
                                              0, (yyvsp[-3].bplife), (yyvsp[0].exp) );
	   }
#line 1399 "y.tab.c"
    break;

  case 8: /* command: breakpointlife PORT portbreakpointtype breakpointport optionalcondition  */
#line 166 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                                                   {
	     int mask = (yyvsp[-1].port).mask;
	     if( mask == 0 ) mask = ( (yyvsp[-1].port).value < 0x100 ? 0x00ff : 0xffff );
	     debugger_breakpoint_add_port( (yyvsp[-2].bptype), (yyvsp[-1].port).value, mask, 0, (yyvsp[-4].bplife), (yyvsp[0].exp) );
           }
#line 1409 "y.tab.c"
    break;

  case 9: /* command: breakpointlife TIME number optionalcondition  */
#line 171 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                        {
	     debugger_breakpoint_add_time( DEBUGGER_BREAKPOINT_TYPE_TIME,
					   (yyvsp[-1].integer), 0, (yyvsp[-3].bplife), (yyvsp[0].exp) );
	   }
#line 1418 "y.tab.c"
    break;

  case 10: /* command: breakpointlife EVENT STRING ':' STRING optionalcondition  */
#line 175 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                                    {
	     debugger_breakpoint_add_event( DEBUGGER_BREAKPOINT_TYPE_EVENT,
					    (yyvsp[-3].string), (yyvsp[-1].string), 0, (yyvsp[-5].bplife), (yyvsp[0].exp) );
	   }
#line 1427 "y.tab.c"
    break;

  case 11: /* command: breakpointlife EVENT STRING ':' '*' optionalcondition  */
#line 179 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                                 {
	     debugger_breakpoint_add_event( DEBUGGER_BREAKPOINT_TYPE_EVENT,
					    (yyvsp[-3].string), "*", 0, (yyvsp[-5].bplife), (yyvsp[0].exp) );
	   }
#line 1436 "y.tab.c"
    break;

  case 12: /* command: CLEAR numberorpc  */
#line 183 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                            { debugger_breakpoint_clear( (yyvsp[0].integer) ); }
#line 1442 "y.tab.c"
    break;

  case 13: /* command: COMMANDS number '\n' debuggercommands DEBUGGER_END  */
#line 184 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                              { debugger_breakpoint_set_commands( (yyvsp[-3].integer), (yyvsp[-1].string) ); }
#line 1448 "y.tab.c"
    break;

  case 14: /* command: CONDITION NUMBER expressionornull  */
#line 185 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                             {
	     debugger_breakpoint_set_condition( (yyvsp[-1].integer), (yyvsp[0].exp) );
           }
#line 1456 "y.tab.c"
    break;

  case 15: /* command: CONTINUE  */
#line 188 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                    { debugger_run(); }
#line 1462 "y.tab.c"
    break;

  case 16: /* command: DEBUGGER_DELETE  */
#line 189 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                           { debugger_breakpoint_remove_all(); }
#line 1468 "y.tab.c"
    break;

  case 17: /* command: DEBUGGER_DELETE number  */
#line 190 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                  { debugger_breakpoint_remove( (yyvsp[0].integer) ); }
#line 1474 "y.tab.c"
    break;

  case 18: /* command: DISASSEMBLE number  */
#line 191 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                              { ui_debugger_disassemble( (yyvsp[0].integer) ); }
#line 1480 "y.tab.c"
    break;

  case 19: /* command: EXIT expressionornull  */
#line 192 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                 { debugger_exit_emulator( (yyvsp[0].exp) ); }
#line 1486 "y.tab.c"
    break;

  case 20: /* command: FINISH  */
#line 193 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                    { debugger_breakpoint_exit(); }
#line 1492 "y.tab.c"
    break;

  case 21: /* command: DEBUGGER_IGNORE NUMBER number  */
#line 194 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                         {
	     debugger_breakpoint_ignore( (yyvsp[-1].integer), (yyvsp[0].integer) );
	   }
#line 1500 "y.tab.c"
    break;

  case 22: /* command: NEXT  */
#line 197 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                    { debugger_next(); }
#line 1506 "y.tab.c"
    break;

  case 23: /* command: DEBUGGER_OUT number NUMBER  */
#line 198 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                      { debugger_port_write( (yyvsp[-1].integer), (yyvsp[0].integer) ); }
#line 1512 "y.tab.c"
    break;

  case 24: /* command: DEBUGGER_PRINT number  */
#line 199 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                 { printf( "0x%x\n", (yyvsp[0].integer) ); }
#line 1518 "y.tab.c"
    break;

  case 25: /* command: SET NUMBER number  */
#line 200 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                             { debugger_poke( (yyvsp[-1].integer), (yyvsp[0].integer) ); }
#line 1524 "y.tab.c"
    break;

  case 26: /* command: SET VARIABLE number  */
#line 201 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                               { debugger_variable_set( (yyvsp[-1].string), (yyvsp[0].integer) ); }
#line 1530 "y.tab.c"
    break;

  case 27: /* command: SET STRING ':' STRING number  */
#line 202 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        { debugger_system_variable_set( (yyvsp[-3].string), (yyvsp[-1].string), (yyvsp[0].integer) ); }
#line 1536 "y.tab.c"
    break;

  case 28: /* command: STEP  */
#line 203 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                    { debugger_step(); }
#line 1542 "y.tab.c"
    break;

  case 29: /* breakpointlife: BREAK  */
#line 206 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                         { (yyval.bplife) = DEBUGGER_BREAKPOINT_LIFE_PERMANENT; }
#line 1548 "y.tab.c"
    break;

  case 30: /* breakpointlife: TBREAK  */
#line 207 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                         { (yyval.bplife) = DEBUGGER_BREAKPOINT_LIFE_ONESHOT; }
#line 1554 "y.tab.c"
    break;

  case 31: /* breakpointtype: %empty  */
#line 210 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                              { (yyval.bptype) = DEBUGGER_BREAKPOINT_TYPE_EXECUTE; }
#line 1560 "y.tab.c"
    break;

  case 32: /* breakpointtype: READ  */
#line 211 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                              { (yyval.bptype) = DEBUGGER_BREAKPOINT_TYPE_READ; }
#line 1566 "y.tab.c"
    break;

  case 33: /* breakpointtype: WRITE  */
#line 212 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                              { (yyval.bptype) = DEBUGGER_BREAKPOINT_TYPE_WRITE; }
#line 1572 "y.tab.c"
    break;

  case 34: /* breakpointport: number  */
#line 215 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                         { (yyval.port).mask = 0; (yyval.port).value = (yyvsp[0].integer); }
#line 1578 "y.tab.c"
    break;

  case 35: /* breakpointport: number ':' number  */
#line 216 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                    { (yyval.port).mask = (yyvsp[-2].integer); (yyval.port).value = (yyvsp[0].integer); }
#line 1584 "y.tab.c"
    break;

  case 36: /* breakpointlocation: numberorpc  */
#line 219 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                 { (yyval.location).source = memory_source_any; (yyval.location).offset = (yyvsp[0].integer); }
#line 1590 "y.tab.c"
    break;

  case 37: /* breakpointlocation: STRING ':' number ':' number  */
#line 220 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                   {
                        (yyval.location).source = memory_source_find( (yyvsp[-4].string) );
                        if( (yyval.location).source == -1 ) {
                          char buffer[80];
                          snprintf( buffer, 80, "unknown memory source \"%s\"", (yyvsp[-4].string) );
                          yyerror( buffer );
                          YYERROR;
                        }
                        (yyval.location).page = (yyvsp[-2].integer);
                        (yyval.location).offset = (yyvsp[0].integer);
                      }
#line 1606 "y.tab.c"
    break;

  case 38: /* portbreakpointtype: READ  */
#line 232 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                            { (yyval.bptype) = DEBUGGER_BREAKPOINT_TYPE_PORT_READ; }
#line 1612 "y.tab.c"
    break;

  case 39: /* portbreakpointtype: WRITE  */
#line 233 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                            { (yyval.bptype) = DEBUGGER_BREAKPOINT_TYPE_PORT_WRITE; }
#line 1618 "y.tab.c"
    break;

  case 40: /* optionalcondition: %empty  */
#line 236 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                   { (yyval.exp) = NULL; }
#line 1624 "y.tab.c"
    break;

  case 41: /* optionalcondition: IF expression  */
#line 237 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                   { (yyval.exp) = (yyvsp[0].exp); }
#line 1630 "y.tab.c"
    break;

  case 42: /* numberorpc: %empty  */
#line 240 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                          { (yyval.integer) = PC; }
#line 1636 "y.tab.c"
    break;

  case 43: /* numberorpc: number  */
#line 241 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                          { (yyval.integer) = (yyvsp[0].integer); }
#line 1642 "y.tab.c"
    break;

  case 44: /* expressionornull: %empty  */
#line 244 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                { (yyval.exp) = NULL; }
#line 1648 "y.tab.c"
    break;

  case 45: /* expressionornull: expression  */
#line 245 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                { (yyval.exp) = (yyvsp[0].exp); }
#line 1654 "y.tab.c"
    break;

  case 46: /* number: expression  */
#line 248 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                     { (yyval.integer) = debugger_expression_evaluate( (yyvsp[0].exp) ); }
#line 1660 "y.tab.c"
    break;

  case 47: /* expression: NUMBER  */
#line 251 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                     { (yyval.exp) = debugger_expression_new_number( (yyvsp[0].integer), debugger_memory_pool );
		       if( !(yyval.exp) ) YYABORT;
		     }
#line 1668 "y.tab.c"
    break;

  case 48: /* expression: STRING ':' STRING  */
#line 254 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                { (yyval.exp) = debugger_expression_new_system_variable( (yyvsp[-2].string), (yyvsp[0].string), debugger_memory_pool );
                                  if( !(yyval.exp) ) YYABORT;
                                }
#line 1676 "y.tab.c"
    break;

  case 49: /* expression: VARIABLE  */
#line 257 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                       { (yyval.exp) = debugger_expression_new_variable( (yyvsp[0].string), debugger_memory_pool );
			 if( !(yyval.exp) ) YYABORT;
		       }
#line 1684 "y.tab.c"
    break;

  case 50: /* expression: '(' expression ')'  */
#line 260 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                 { (yyval.exp) = (yyvsp[-1].exp); }
#line 1690 "y.tab.c"
    break;

  case 51: /* expression: '[' expression ']'  */
#line 261 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                 {
                (yyval.exp) = debugger_expression_new_unaryop( DEBUGGER_TOKEN_DEREFERENCE, (yyvsp[-1].exp), debugger_memory_pool );
                if( !(yyval.exp) ) YYABORT;
              }
#line 1699 "y.tab.c"
    break;

  case 52: /* expression: '+' expression  */
#line 265 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                          { (yyval.exp) = (yyvsp[0].exp); }
#line 1705 "y.tab.c"
    break;

  case 53: /* expression: '-' expression  */
#line 266 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                          {
	        (yyval.exp) = debugger_expression_new_unaryop( '-', (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1714 "y.tab.c"
    break;

  case 54: /* expression: NEGATE expression  */
#line 270 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                {
	        (yyval.exp) = debugger_expression_new_unaryop( (yyvsp[-1].token), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1723 "y.tab.c"
    break;

  case 55: /* expression: expression '+' expression  */
#line 274 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '+', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1732 "y.tab.c"
    break;

  case 56: /* expression: expression '-' expression  */
#line 278 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '-', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1741 "y.tab.c"
    break;

  case 57: /* expression: expression '*' expression  */
#line 282 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '*', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1750 "y.tab.c"
    break;

  case 58: /* expression: expression '/' expression  */
#line 286 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '/', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1759 "y.tab.c"
    break;

  case 59: /* expression: expression EQUALITY expression  */
#line 290 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                             {
	        (yyval.exp) = debugger_expression_new_binaryop( (yyvsp[-1].token), (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1768 "y.tab.c"
    break;

  case 60: /* expression: expression COMPARISON expression  */
#line 294 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                               {
	        (yyval.exp) = debugger_expression_new_binaryop( (yyvsp[-1].token), (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1777 "y.tab.c"
    break;

  case 61: /* expression: expression '&' expression  */
#line 298 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '&', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1786 "y.tab.c"
    break;

  case 62: /* expression: expression '^' expression  */
#line 302 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '^', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1795 "y.tab.c"
    break;

  case 63: /* expression: expression '|' expression  */
#line 306 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                        {
	        (yyval.exp) = debugger_expression_new_binaryop( '|', (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1804 "y.tab.c"
    break;

  case 64: /* expression: expression LOGICAL_AND expression  */
#line 310 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                {
	        (yyval.exp) = debugger_expression_new_binaryop(
		  DEBUGGER_TOKEN_LOGICAL_AND, (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool
                );
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1815 "y.tab.c"
    break;

  case 65: /* expression: expression LOGICAL_OR expression  */
#line 316 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                               {
	        (yyval.exp) = debugger_expression_new_binaryop(
		  DEBUGGER_TOKEN_LOGICAL_OR, (yyvsp[-2].exp), (yyvsp[0].exp), debugger_memory_pool
		);
		if( !(yyval.exp) ) YYABORT;
	      }
#line 1826 "y.tab.c"
    break;

  case 66: /* debuggercommands: debuggercommand  */
#line 324 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                    { (yyval.string) = (yyvsp[0].string); }
#line 1832 "y.tab.c"
    break;

  case 67: /* debuggercommands: debuggercommands debuggercommand  */
#line 325 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"
                                                     {
                      (yyval.string) = fuse_mempool_new( debugger_memory_pool, char, strlen( (yyvsp[-1].string) ) + strlen( (yyvsp[0].string) ) + 2 );
                      sprintf( (yyval.string), "%s\n%s", (yyvsp[-1].string), (yyvsp[0].string) );
                    }
#line 1841 "y.tab.c"
    break;


#line 1845 "y.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 332 "/home/mikl/pico-retro-new-gen/h3_bare/cores/fuse/fuse/debugger/commandy.y"

