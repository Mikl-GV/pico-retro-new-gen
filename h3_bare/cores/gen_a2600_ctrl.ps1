$ErrorActionPreference = 'Stop'
$def = 'emulator-refs\stella2014-libretro\stella\src\emucore\DefProps.hxx'
$out = 'h3_bare\cores\a2600_ctrl.c'

$typeMap = @{
  'PADDLES'       = 1
  'PADDLES_IAXIS' = 2
  'PADDLES_IDIR'  = 3
  'PADDLES_IAXDR' = 4
  'KEYBOARD'      = 5
  'KEYPAD'        = 5
}

$rows = New-Object System.Collections.Generic.List[string]
$count = 0
foreach ($line in [IO.File]::ReadLines((Resolve-Path $def))) {
  if ($line -notmatch '^\s*\{\s*"') { continue }
  $m = [regex]::Matches($line, '"([^"]*)"')
  if ($m.Count -lt 21) { continue }
  $md5 = $m[0].Groups[1].Value
  if ($md5.Length -ne 32) { continue }
  $l = $m[12].Groups[1].Value
  $r = $m[13].Groups[1].Value
  $sw = $m[14].Groups[1].Value
  $lc = 0
  if ($typeMap.ContainsKey($l)) { $lc = $typeMap[$l] }
  $rc = 0
  if ($typeMap.ContainsKey($r)) { $rc = $typeMap[$r] }
  if (($lc -eq 0) -and ($rc -eq 0)) { continue }
  $swv = 0
  if ($sw -eq 'YES') { $swv = 1 }
  $bytes = @()
  for ($i = 0; $i -lt 32; $i += 2) { $bytes += ('0x' + $md5.Substring($i, 2)) }
  $rows.Add('  { { ' + ($bytes -join ',') + ' }, ' + $lc + ', ' + $rc + ', ' + $swv + ' },')
  $count++
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// a2600_ctrl.c - A2600 controller type table (MD5 -> Controller.Left/Right).')
[void]$sb.AppendLine('// Generated from Stella 2014 DefProps.hxx; only non-joystick entries')
[void]$sb.AppendLine('// (PADDLES* / KEYBOARD). Regenerate with build/gen_a2600_ctrl.ps1.')
[void]$sb.AppendLine('#include <string.h>')
[void]$sb.AppendLine('#include "a2600_ctrl.h"')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('typedef struct {')
[void]$sb.AppendLine('    unsigned char md5[16];')
[void]$sb.AppendLine('    unsigned char l, r, swap;')
[void]$sb.AppendLine('} a26_ctrl_rec_t;')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const a26_ctrl_rec_t a26_ctrl_tbl[] = {')
foreach ($row in $rows) { [void]$sb.AppendLine($row) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static int hexval(char c) {')
[void]$sb.AppendLine('    if (c >= ''0'' && c <= ''9'') return c - ''0'';')
[void]$sb.AppendLine('    if (c >= ''a'' && c <= ''f'') return c - ''a'' + 10;')
[void]$sb.AppendLine('    if (c >= ''A'' && c <= ''F'') return c - ''A'' + 10;')
[void]$sb.AppendLine('    return -1;')
[void]$sb.AppendLine('}')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static int contains_ci(const char* s, const char* sub) {')
[void]$sb.AppendLine('    if (!s || !sub) return 0;')
[void]$sb.AppendLine('    size_t n = strlen(sub);')
[void]$sb.AppendLine('    if (!n) return 0;')
[void]$sb.AppendLine('    for (; *s; s++) {')
[void]$sb.AppendLine('        size_t i = 0;')
[void]$sb.AppendLine('        while (i < n && s[i]) {')
[void]$sb.AppendLine('            char a = s[i], b = sub[i];')
[void]$sb.AppendLine('            if (a >= ''a'' && a <= ''z'') a = (char)(a - 32);')
[void]$sb.AppendLine('            if (b >= ''a'' && b <= ''z'') b = (char)(b - 32);')
[void]$sb.AppendLine('            if (a != b) break;')
[void]$sb.AppendLine('            i++;')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('        if (i == n) return 1;')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('    return 0;')
[void]$sb.AppendLine('}')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('// Known paddle titles missing in the 2014 property database')
[void]$sb.AppendLine('// (the DB leaves Controller.* empty for them).')
[void]$sb.AppendLine('static const char* const a26_paddle_names[] = {')
[void]$sb.AppendLine('    "PONG", "PINBALL", "BREAKOUT", "WARLORDS", "KABOOM",')
[void]$sb.AppendLine('    "CIRCUS ATARI", "NIGHT DRIVER", "STREET RACER",')
[void]$sb.AppendLine('    "DEMONS TO DIAMONDS", "PADDLE"')
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('int a2600_ctrl_lookup(const char* md5hex, const char* rom_name,')
[void]$sb.AppendLine('                     int* left, int* right, int* swap) {')
[void]$sb.AppendLine('    *left = A26C_JOY;')
[void]$sb.AppendLine('    *right = A26C_JOY;')
[void]$sb.AppendLine('    *swap = 0;')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('    if (md5hex && strlen(md5hex) == 32) {')
[void]$sb.AppendLine('        unsigned char md5[16];')
[void]$sb.AppendLine('        int ok = 1;')
[void]$sb.AppendLine('        for (int i = 0; i < 16; i++) {')
[void]$sb.AppendLine('            int hi = hexval(md5hex[i * 2]);')
[void]$sb.AppendLine('            int lo = hexval(md5hex[i * 2 + 1]);')
[void]$sb.AppendLine('            if (hi < 0 || lo < 0) { ok = 0; break; }')
[void]$sb.AppendLine('            md5[i] = (unsigned char)(hi * 16 + lo);')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('        if (ok) {')
[void]$sb.AppendLine('            for (unsigned i = 0; i < sizeof(a26_ctrl_tbl) / sizeof(a26_ctrl_tbl[0]); i++) {')
[void]$sb.AppendLine('                if (memcmp(a26_ctrl_tbl[i].md5, md5, 16) == 0) {')
[void]$sb.AppendLine('                    *left = a26_ctrl_tbl[i].l;')
[void]$sb.AppendLine('                    *right = a26_ctrl_tbl[i].r;')
[void]$sb.AppendLine('                    *swap = a26_ctrl_tbl[i].swap;')
[void]$sb.AppendLine('                    return 1;')
[void]$sb.AppendLine('                }')
[void]$sb.AppendLine('            }')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('    for (unsigned i = 0; i < sizeof(a26_paddle_names) / sizeof(a26_paddle_names[0]); i++) {')
[void]$sb.AppendLine('        if (contains_ci(rom_name, a26_paddle_names[i])) {')
[void]$sb.AppendLine('            *left = A26C_PADDLES;')
[void]$sb.AppendLine('            return 1;')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('    return 0;')
[void]$sb.AppendLine('}')

[IO.File]::WriteAllText((Resolve-Path -LiteralPath (Split-Path -Parent $out)).Path + '\' + (Split-Path -Leaf $out), $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("rows={0}" -f $count)
