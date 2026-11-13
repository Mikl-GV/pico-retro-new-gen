#!/bin/bash
# agat_rename.sh — префикс agat_ для глобальных символов объектов набора
# Agat Emulator. Снимает коллизии с другими ядрами при линковке с
# --allow-multiple-definition (у ядра generic-имена: list_*, tsq_*, memory-,
# sound_*, cpu_*, video_*, fdd_*, init_system_*, load_buf_res, ...).
#
# Схема как в puae_rename.sh: канон строится по ВСЕМ объектам набора, в
# каждом объекте переопределяются только голые имена (без agat_). Host-слой
# (agat_host.o) в набор НЕ входит и держит внешние имена (emu_run_agat,
# ag_get_state, agat_video_expand).
#
# Usage: agat_rename.sh <build/agat_*.o...>
set -u

# Имена, которые обязаны остаться глобальными: libc/newlib и рантайм компилятора.
KEEP="malloc calloc realloc free memalign memcpy memset memmove memcmp strlen strcpy strncpy strcat strcmp strncmp strcasecmp strncasecmp strstr strchr strrchr stpcpy sprintf vsprintf snprintf vsnprintf printf vprintf puts fputc fputs fopen fclose fread fwrite fseek ftell fflush perror atoi atol strtol abort exit rand srand div qsort bsearch toupper tolower isspace isdigit isalpha isalnum __assert_func __aeabi_uidiv __aeabi_idiv __aeabi_uidivmod __aeabi_idivmod __aeabi_lmul __aeabi_llsl __aeabi_llsr __aeabi_lasr __aeabi_uldivmod __aeabi_ldivmod"

DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    agat_*) x=${n#agat_} ;;   # уже с префиксом
    _Z*)    continue ;;       # C++ не используется
    *)      x=$n ;;
  esac
  case " $KEEP " in *" $x "*) continue ;; esac
  case "$CANON " in *" $x "*) continue ;; esac
  CANON="$CANON $x"
done

BATCH=100
for o in "$@"; do
  [ -f "$o" ] || continue
  OBSYM=$(arm-none-eabi-nm "$o" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' ')
  [ -z "$OBSYM" ] && continue

  ARGS=""
  N=0
  for x in $CANON; do
    case " $OBSYM " in
      *" $x "*) ARGS="$ARGS --redefine-sym $x=agat_$x"; N=$((N+1)) ;;
    esac
    if [ "$N" -ge "$BATCH" ]; then
      arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
      ARGS=""; N=0
    fi
  done
  if [ -n "$ARGS" ]; then
    arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
  fi
done
exit 0
