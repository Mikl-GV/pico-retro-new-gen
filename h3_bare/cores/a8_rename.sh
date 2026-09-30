#!/bin/bash
# a8_rename.sh — префикс a8_ для всех глобальных символов объектов набора
# atari800 (ядро Atari 8-bit + libretro-обёртка + libretro-common + zlib).
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: cpu, memory, sound, log, input, esc, sio, file_path, ...).
# Идемпотентен: канон по всем объектам (префикс a8_ снимается), в объекте
# переименовываются только голые вхождения (определения И ссылки).
#
# Оптимизация (r0.410): раньше на каждый канон-символ в каждом объекте
# спавнился подпроцесс nm|awk|grep (88×1189 ≈ 104 тыс. запусков, >10 мин).
# Теперь: один nm на объект + поиск по встроенной case-строке — на порядки
# быстрее; objcopy один раз на объект со всеми переименованиями.
#
# KEEP — символы, нужные внешнему миру (диспетчер меню). Сейчас a8 не в
# прошивке, но на этапе 2 сюда добавляются точки входа хоста.
# Usage: a8_rename.sh <build/a8?*.o...>
set -u

KEEP=""

# канон: голая логическая схема имён (с префикса a8_ снятие) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    a8_*) x=${n#a8_};;   # уже с префиксом: известно как a8_<голое>
    *)     x=$n;;
  esac
  case " $KEEP " in *" $x "*) continue;; esac
  case "$CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

for o in "$@"; do
  [ -f "$o" ] || continue
  # все имена в объекте (определения И ссылки) — одной строкой « x1 x2 … »
  OBSYM=$(arm-none-eabi-nm "$o" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' ')
  [ -z "$OBSYM" ] && continue

  ARGS=""
  for x in $CANON; do
    # голое вхождение x есть в объекте? точный токен (с пробелами по краям)
    case " $OBSYM " in
      *" $x "*) ARGS="$ARGS --redefine-sym $x=a8_$x";;
    esac
  done
  if [ -n "$ARGS" ]; then
    arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
  fi
done
exit 0