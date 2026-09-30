# FB Neo — каталог ROM-сетов по системам

Сгенерировано из папки `roms/fbneo` (375 сетов) сверкой с драйверами прошивки
и каталогом MAME/FBNeo (mame2003-plus).

Всего: **375** сетов. Имеют драйвер в прошивке: **2** (r0.367). Без драйвера: **76**.

_Обновление r0.388:_ из fbneo вынесены поддерживаемые сеты:
- **Sega System 16/18** → `/roms/segasys` (shinobi, goldnaxe, altbeast, fantzone, astorm, shdancer…);
- **Cave (68K)** → `/roms/cave` (donpachi, ddonpach, esprade, guwange, feversos и др.).
В fbneo остались ONLY сети, для которых драйверов в нашей прошивке НЕТ (чужие платы).
Ниже — чем их реально запускать вне прошивки.

## Играбельно сейчас (драйвер в прошивке)

| Игра | Название | Год | Производитель |
|---|---|---|---|
| batrider | Armed Police Batrider (Japan, version B) | 1998 | Raizing / Eighting |
| bgaregga | bgaregga | ? | ? |

## Чем запускать остаток (вне прошивки)

Все оставшиеся в `/roms/fbneo` сеты — чужие платы, поддержанные только в
ПОЛНОМ эмуляторе (драйверы в нашу прошивку пока не вендорены, см. ARCADES-PLAN).
Кандидаты по категориям из каталога ниже:

| Категория (платы) | Эмулятор / ядро для запуска сегодня | Примечание для портирования к нам |
|---|---|---|
| Konami (39) | MAME (fbneo **full**), mame2003-plus | в основном 6809/6502/custom → нужны новые CPU (V6) |
| Sega (30, кроме System 16/18) | MAME / MAME 2003-plus; System 32/16→FBNeo full | System 32 — 68K, но видео тяжёлое (позже) |
| Namco (24) | MAME / MAME 2003-plus | 6502/custom → V6 |
| Taito (17+13) | MAME / FBNeo full (F2/F3) | часть 68K+Z80/M6805 → **V3 Taito 68K** |
| Irem (17) | MAME | 6809/custom → V6 |
| Capcom (16, не-CPS) | MAME / mame2003-plus | 6809/6502 → V6 |
| Data East (15+9) | MAME / FBNeo full | 68K+Z80 → **V4 Data East 68K** |
| Atari (15+10) | MAME (Atari System 1/2, vector) | вектор/6809 — сложно |
| Williams (8) | MAME | 6809 → V6 |
| Midway (8) | MAME / mame2003-plus | 68K, видео T-unit → V5 (сложно) |
| Psikyo (7) | MAME / FBNeo full | 68K → V5 (средне) |
| Bally Midway (7) | MAME | 6809/Z80 → V6 |
| Technos (5+4+1) | MAME / FBNeo full | часть 68K → V4 |
| Прочие малые | MAME (или mame2003-plus) | точечно по CPU |

> mame2003-plus — лёгкая референс-база драйверов для старых плат; полный MAME —
> точнее, но тяжёлый. Для портирования в прошивку релевантны только платы с
> CPU 68K/Z80 (у нас уже есть), остальные — после вехи V6 (новий 6809/6502).

## Без драйвера — по категориям (система/производитель)

Эти игры в прошивке не запустятся: платы этих систем не добавлены (нужен
до-вендор board-драйверов, часто CPU 6809/6502/custom). Категория по
производителю = по сути «система».

### Konami (39)

| Игра | Название | Год |
|---|---|---|
| ajax | Ajax | 1987 |
| aliens | Aliens (World set 1) | 1990 |
| amidar | Amidar | 1981 |
| asterix | Asterix (World ver. EAD) | 1992 |
| blswhstl | Bells and Whistles (Version L) | 1991 |
| bucky | Bucky O'Hare (World version EA) | 1992 |
| circusc | Circus Charlie | 1984 |
| contra | Contra (US) | 1987 |
| crimfght | Crime Fighters (US 4 players) | 1989 |
| fastlane | Fast Lane | 1987 |
| frogger | Frogger | 1981 |
| gijoe | GI Joe (World) | 1992 |
| gradius3 | Gradius III (Japan) | 1989 |
| gyruss | Gyruss (Konami) | 1983 |
| hcastle | Haunted Castle (version M) | 1988 |
| jackal | Jackal (World) | 1986 |
| jailbrek | Jail Break | 1986 |
| junofrst | Juno First | 1983 |
| kicker | Kicker | 1985 |
| metamrph | Metamorphic Force (US ver UAA) | 1993 |
| mikie | Mikie | 1984 |
| mrgoemon | Mr. Goemon (Japan) | 1986 |
| mystwarr | Mystic Warriors (Europe ver EAA) | 1993 |
| nemesis | Nemesis | 1985 |
| parodius | Parodius DA! (World) | 1990 |
| pooyan | Pooyan | 1982 |
| rocnrope | Roc'n Rope | 1983 |
| rushatck | Rush'n Attack | 1985 |
| salamand | Salamander (version D) | 1986 |
| scobra | Super Cobra | 1981 |
| scontra | Super Contra | 1988 |
| scramble | Scramble | 1981 |
| timeplt | Time Pilot | 1982 |
| tmnt2pj | Teenage Mutant Ninja Turtles (Japan 2 Players) | 1990 |
| trackfld | Track and Field | 1983 |
| viostorm | Violent Storm (Europe ver EAB) | 1993 |
| vulcan | Vulcan Venture | 1988 |
| xexex | Xexex (World) | 1991 |
| yiear | Yie Ar Kung-Fu (set 1) | 1985 |

### Sega (30)

| Игра | Название | Год |
|---|---|---|
| aburner2 | After Burner II | 1987 |
| aliensyn | Alien Syndrome (set 1) | 1987 |
| altbeast | Altered Beast (Version 1) | 1988 |
| astorm | Alien Storm | 1990 |
| buckrog | Buck Rogers - Planet of Zoom | 1982 |
| carnival | Carnival (upright) | 1980 |
| columns | Columns (US) | 1990 |
| congo | Congo Bongo (Rev C, 2 board stack) | 1983 |
| cotton | Cotton (Japan) | 19?? |
| enduror | Enduro Racer | 1986 |
| eswat | E-Swat - Cyber Police | 1989 |
| fantzone | Fantasy Zone (Japan New Ver.) | 1986 |
| flicky | Flicky (128k Ver.) | 1984 |
| ga2 | Golden Axe - The Revenge of Death Adder (US) | 1992 |
| gground | Gain Ground | ???? |
| goldnaxe | Golden Axe (Version 1) | 1989 |
| hangon | Hang-On | 1985 |
| outrun | Out Run (set 1) | 1986 |
| pengo | Pengo (World, not encrypted, rev A) | 1982 |
| quartet2 | Quartet 2 (8751 317-0010) | 1986 |
| radm | Rad Mobile | 1991 |
| seganinj | Sega Ninja | 1985 |
| shangon | Super Hang-On | 1992 |
| sharrier | Space Harrier | 1985 |
| shdancer | Shadow Dancer (US) | 1989 |
| shinobi | Shinobi (set 1) | 1987 |
| sonic | Segasonic the Hedgehog (Japan rev. C) | 1992 |
| szaxxon | Super Zaxxon | 1982 |
| thndrbld | Thunder Blade (upright) (bootleg of FD1094 317-0056 set) | 1987 |
| upndown | Up'n Down | 1983 |

### Namco (24)

| Игра | Название | Год |
|---|---|---|
| digdug | Dig Dug (rev 2) | 1982 |
| digdug2 | Dig Dug II (New Ver.) | 1985 |
| drgnbstr | Dragon Buster | 1984 |
| dsaber | Dragon Saber | 1990 |
| galaga | Galaga (Namco rev. B) | 1981 |
| galaga88 | Galaga '88 (set 1) | 1987 |
| galaxian | Galaxian (Namco set 1) | 1979 |
| gaplus | Gaplus (rev. D) | 1984 |
| hopmappy | Hopping Mappy | 1986 |
| kingball | King and Balloon (US) | 1980 |
| liblrabl | Libble Rabble | 1983 |
| nrallyx | New Rally X | 1981 |
| outfxies | Outfoxies | 1994 |
| pacland | Pac-Land (set 1) | 1984 |
| rallyx | Rally X | 1980 |
| rthun2 | Rolling Thunder 2 | 1990 |
| rthunder | Rolling Thunder (new version) | 1986 |
| skykid | Sky Kid (New Ver.) | 1985 |
| splatter | Splatter House (Japan) | 1988 |
| superpac | Super Pac-Man | 1982 |
| sxevious | Super Xevious | 1984 |
| tankfrce | Tank Force (US) | 1991 |
| todruaga | Tower of Druaga (New Ver.) | 1984 |
| valkyrie | Valkyrie No Densetsu (Japan) | 1989 |

### Taito Corporation Japan (17)

| Игра | Название | Год |
|---|---|---|
| aquajack | Aqua Jack (World) | 1990 |
| arkanoid | Arkanoid (World) | 1986 |
| arknoid2 | Arkanoid - Revenge of DOH (World) | 1987 |
| bublbob2 | Bubble Bobble 2 (World) | 1994 |
| chasehq | Chase H.Q. (World) | 1988 |
| deadconx | Dead Connection (World) | 1992 |
| dondokod | Don Doko Don (World) | 1989 |
| drtoppel | Dr. Toppel's Adventure (World) | 1987 |
| elvactr | Elevator Action Returns (World) | 1994 |
| growl | Growl (World) | 1990 |
| gunlock | Gunlock (World) | 1993 |
| liquidk | Liquid Kids (World) | 1990 |
| ninjak | The Ninja Kids (World) | 1990 |
| rastan | Rastan (World) | 1987 |
| spcinv95 | Space Invaders '95 - Attack Of The Lunar Loonies (World) | 1995 |
| thundfox | Thunder Fox (World) | 1990 |
| volfied | Volfied (World) | 1989 |

### Irem (17)

| Игра | Название | Год |
|---|---|---|
| dbreed | Dragon Breed | 1989 |
| gunforce | Gunforce - Battle Fire Engulfed Terror Island (World) | 1991 |
| hharry | Hammerin' Harry (World) | 1990 |
| hook | Hook (World) | 1992 |
| horizon | Horizon | 1985 |
| imgfight | Image Fight (Japan) | 1988 |
| inthunt | In The Hunt (World) | 1993 |
| kungfum | Kung-Fu Master | 1984 |
| lethalth | Lethal Thunder (World) | 1991 |
| loht | Legend of Hero Tonma | 1989 |
| mpatrol | Moon Patrol | 1982 |
| nspirit | Ninja Spirit | 1988 |
| rtype | R-Type (Japan) | 1987 |
| rtype2 | R-Type II | 1989 |
| rtypeleo | R-Type Leo (World rev. C) | 1992 |
| uccops | Undercover Cops (World) | 1992 |
| xmultipl | X Multiply (Japan) | 1989 |

### Capcom (16)

| Игра | Название | Год |
|---|---|---|
| 1942 | 1942 (set 1) | 1984 |
| 1943 | 1943 - The Battle of Midway (US) | 1987 |
| 1943kai | 1943 Kai - Midway Kaisen | 1987 |
| avengers | Avengers (US set 1) | 1987 |
| bbros | Buster Bros. (US) | 1989 |
| bionicc | Bionic Commando (US set 1) | 1987 |
| blktiger | Black Tiger | 1987 |
| commando | Commando (World) | 1985 |
| exedexes | Exed Exes | 1985 |
| gng | Ghosts'n Goblins (World[Q] set 1) | 1985 |
| gunsmoke | Gun.Smoke (World) | 1985 |
| lwings | Legendary Wings (US set 1) | 1986 |
| sidearms | Side Arms - Hyper Dyne (World) | 1986 |
| sonson | Son Son | 1984 |
| srumbler | The Speed Rumbler (set 1) | 1986 |
| trojan | Trojan (US) | 1986 |

### Data East Corporation (15)

| Игра | Название | Год |
|---|---|---|
| actfancr | Act-Fancer Cybernetick Hyper Weapon (World revision 2) | 1989 |
| boogwing | Boogie Wings (Euro v1.5) | 1992 |
| btime | Burger Time (Data East set 1) | 1982 |
| captaven | Captain America and The Avengers (Asia Rev 1.9) | 1991 |
| darkseal | Dark Seal (World revision 3) | 1990 |
| dassault | Desert Assault (US) | 1991 |
| edrandy | The Cliffhanger - Edward Randy (World revision 2) | 1990 |
| hvysmsh | Heavy Smash (Europe version -2) | 1993 |
| nitrobal | Nitro Ball (US) | 1992 |
| nslashers | Night Slashers (Over Sea Rev 1.2) | 1994 |
| robocop | Robocop (World revision 4) | 1988 |
| robocop2 | Robocop 2 (World) | 1991 |
| rohga | Rohga Armour Force (Asia-Europe v3.0) | 1991 |
| tumblep | Tumble Pop (World) | 1991 |
| wizdfire | Wizard Fire (US v1.1) | 1992 |

### Atari (15)

| Игра | Название | Год |
|---|---|---|
| astdelux | Asteroids Deluxe (rev 2) | 1980 |
| asteroid | Asteroids (rev 2) | 1979 |
| bwidow | Black Widow | 1982 |
| bzone | Battle Zone (set 1) | 1980 |
| ccastles | Crystal Castles (version 4) | 1983 |
| centiped | Centipede (revision 3) | 1980 |
| foodf | Food Fight (rev 3) | 1982 |
| jedi | Return of the Jedi | 1984 |
| llander | Lunar Lander (rev 2) | 1979 |
| mhavoc | Major Havoc (rev 3) | 1983 |
| milliped | Millipede | 1982 |
| missile | Missile Command (set 1) | 1980 |
| nitedrvr | Night Driver | 1976 |
| starwars | Star Wars (rev 2) | 1983 |
| tempest | Tempest (rev 3) | 1980 |

### Taito Corporation (13)

| Игра | Название | Год |
|---|---|---|
| bublbobl | Bubble Bobble | 1986 |
| chaknpop | Chack'n Pop | 1983 |
| cleopatr | Cleopatra Fortune (Japan) | 1996 |
| elevator | Elevator Action | 1983 |
| junglek | Jungle King (Japan) | 1982 |
| landmakr | Land Maker (Japan) | 1998 |
| lightbr | Light Bringer (Japan) | 1993 |
| lkage | The Legend of Kage | 1984 |
| pbobble3 | Puzzle Bobble 3 (World) | 1996 |
| slapshot | Slap Shot (Japan) | 1994 |
| spacedx | Space Invaders DX (US) v2.1 | 1994 |
| superman | Superman | 1988 |
| tnzs | The NewZealand Story (Japan, new version) (P0-043A PCB) | 1988 |

### Atari Games (10)

| Игра | Название | Год |
|---|---|---|
| atetris | Tetris (set 1) | 1988 |
| badlands | Bad Lands | 1989 |
| batman | Batman | 1991 |
| blstroid | Blasteroids (rev 4) | 1987 |
| eprom | Escape from the Planet of the Robot Monsters (set 1) | 1989 |
| gaunt22p | Gauntlet II (2 Players, rev 2) | 1986 |
| klax | Klax (set 1) | 1989 |
| pitfight | Pit Fighter (rev 4) | 1990 |
| toobin | Toobin' (rev 3) | 1988 |
| xybots | Xybots (rev 2) | 1987 |

### Data East USA (9)

| Игра | Название | Год |
|---|---|---|
| baddudes | Bad Dudes vs. Dragonninja (US) | 1988 |
| brkthru | Break Thru (US) | 1986 |
| ghostb | The Real Ghostbusters (US 2 Players) | 1987 |
| hbarrel | Heavy Barrel (US) | 1987 |
| karnov | Karnov (US) | 1987 |
| kchamp | Karate Champ (US) | 1984 |
| ringking | Ring King (US set 1) | 1985 |
| slyspy | Sly Spy (US revision 3) | 1989 |
| twocrude | Two Crude (US) | 1990 |

### Williams (8)

| Игра | Название | Год |
|---|---|---|
| bubbles | Bubbles | 1982 |
| defender | Defender (Red label) | 1980 |
| hitice | Hit the Ice (US) | 1990 |
| joust | Joust (White-Green label) | 1982 |
| narc | Narc (rev 7.00) | 1988 |
| robotron | Robotron (Solid Blue label) | 1982 |
| sinistar | Sinistar (revision 3) | 1982 |
| smashtv | Smash T.V. (rev 8.00) | 1990 |

### Banpresto (8)

| Игра | Название | Год |
|---|---|---|
| dbz | Dragonball Z | 1993 |
| dbz2 | Dragonball Z 2 Super Battle | 1994 |
| godzilla | Godzilla | 1993 |
| grdians | Guardians - Denjin Makai II | 1995 |
| macross | Super Spacefortress Macross - Chou-Jikuu Yousai Macross | 1992 |
| macross2 | Super Spacefortress Macross II - Chou-Jikuu Yousai Macross II | 1993 |
| macrossp | Macross Plus | 1996 |
| sailormn | Pretty Soldier Sailor Moon (95-03-22B) | 1995 |

### Midway (8)

| Игра | Название | Год |
|---|---|---|
| invaders | Space Invaders | 1978 |
| kick | Kick (upright) | 1981 |
| mk | Mortal Kombat (rev 5.0 T-Unit 03-19-93) | 1992 |
| mk2 | Mortal Kombat II (rev L3.1) | 1993 |
| mspacman | Ms. Pac-Man | 1981 |
| nbajamte | NBA Jam TE (rev 4.0 03-23-94) | 1994 |
| totcarn | Total Carnage (rev LA1 03-10-92) | 1992 |
| umk3 | Ultimate Mortal Kombat 3 (rev 1.2) | 1994 |

### Psikyo (7)

| Игра | Название | Год |
|---|---|---|
| gnbarich | Gunbarich | 2001 |
| gunbird2 | Gunbird 2 | 1998 |
| loderndf | Lode Runner - The Dig Fight (ver. B) | 2000 |
| s1945 | Strikers 1945 | 1995 |
| s1945ii | Strikers 1945 II | 1997 |
| s1945iii | Strikers 1945 III (World) - Strikers 1999 (Japan) | 1999 |
| tengai | Tengai - Sengoku Blade - Sengoku Ace Episode II | 1996 |

### Bally Midway (7)

| Игра | Название | Год |
|---|---|---|
| journey | Journey | 1983 |
| rampage | Rampage (revision 3) | 1986 |
| shollow | Satan's Hollow (set 1) | 1981 |
| spyhunt | Spy Hunter | 1983 |
| spyhunt2 | Spy Hunter 2 (rev 2) | 1987 |
| tapper | Tapper (Budweiser) | 1983 |
| tron | Tron (set 1) | 1982 |

### Technos (5)

| Игра | Название | Год |
|---|---|---|
| ctribe | The Combatribes (US) | 1990 |
| ddragon | Double Dragon (Japan) | 1987 |
| ddragon2 | Double Dragon II - The Revenge (World) | 1988 |
| ddragon3 | Double Dragon 3 - The Rosetta Stone (US) | 1990 |
| vball | U.S. Championship V'ball (set 1) | 1988 |

### Jaleco (4)

| Игра | Название | Год |
|---|---|---|
| 64street | 64th. Street - A Detective Story (World) | 1991 |
| avspirit | Avenging Spirit | 1991 |
| citycon | City Connection (set 1) | 1985 |
| stdragon | Saint Dragon | 1989 |

### Nintendo (4)

| Игра | Название | Год |
|---|---|---|
| armwrest | Arm Wrestling | 1985 |
| popeye | Popeye (revision D) | 1982 |
| punchout | Punch-Out!! | 1984 |
| spnchout | Super Punch-Out!! | 1984 |

### Sega / Westone (4)

| Игра | Название | Год |
|---|---|---|
| aurail | Aurail (set 3, US) (unprotected) | 1990 |
| riotcity | Riot City | 1991 |
| wb3 | Wonder Boy III - Monster Lair (set 1) | 1988 |
| wbml | Wonder Boy in Monster Land (Japan New Ver.) | 1987 |

### Technos Japan (4)

| Игра | Название | Год |
|---|---|---|
| bogeyman | Bogey Manor | 1985 |
| shadfrce | Shadow Force (US Version 2) | 1993 |
| wwfsstar | WWF Superstars (US) | 1989 |
| wwfwfest | WWF WrestleFest (US) | 1991 |

### Nintendo of America (4)

| Игра | Название | Год |
|---|---|---|
| dkong | Donkey Kong (US set 1) | 1981 |
| dkong3 | Donkey Kong 3 (US) | 1983 |
| dkongjr | Donkey Kong Junior (US) | 1982 |
| mario | Mario Bros. (US, Revision G) | 1983 |

### Tecmo (4)

| Игра | Название | Год |
|---|---|---|
| gaiden | Ninja Gaiden (US) | 1988 |
| rygar | Rygar (US set 1) | 1986 |
| solomon | Solomon's Key (Japan) | 1986 |
| wildfang | Wild Fang - Tecmo Knight | 1989 |

### Irem (licensed from Broderbund) (4)

| Игра | Название | Год |
|---|---|---|
| ldrun | Lode Runner (set 1) | 1984 |
| ldrun2 | Lode Runner II - The Bungeling Strikes Back | 1984 |
| ldrun3 | Lode Runner III - The Golden Labyrinth | 1985 |
| ldrun4 | Lode Runner IV - Teikoku Karano Dasshutsu | 1986 |

### Tad (3)

| Игра | Название | Год |
|---|---|---|
| bloodbro | Blood Bros. | 1990 |
| heatbrl | Heated Barrel (World) | 1992 |
| toki | Toki (World set 1) | 1989 |

### Nichibutsu (3)

| Игра | Название | Год |
|---|---|---|
| cclimber | Crazy Climber (US) | 1980 |
| galivan | Galivan - Cosmo Police (12-16-1985) | 1985 |
| horekid | Kid no Hore Hore Daisakusen | 1987 |

### Kaneko (3)

| Игра | Название | Год |
|---|---|---|
| cyvern | Cyvern (Japan) | 1998 |
| gtmr2 | Mille Miglia 2 - Great 1000 Miles Rally | 1995 |
| mgcrystl | Magical Crystals (World) | 1991 |

### Taito America Corporation (3)

| Игра | Название | Год |
|---|---|---|
| gblchmp | Global Champion (US) | 1994 |
| qix | Qix (set 1) | 1981 |
| zookeep | Zoo Keeper (set 1) | 1982 |

### SNK (3)

| Игра | Название | Год |
|---|---|---|
| ikari | Ikari Warriors (US) | 1986 |
| ikari3 | Ikari III - The Rescue (US, Rotary Joystick) | 1989 |
| pow | P.O.W. - Prisoners of War (US) | 1988 |

### Video System Co. (2)

| Игра | Название | Год |
|---|---|---|
| aerofgt | Aero Fighters | 1992 |
| pipedrm | Pipe Dream (US) | 1990 |

### Gaelco (2)

| Игра | Название | Год |
|---|---|---|
| aligator | Alligator Hunt | 1994 |
| biomtoy | Biomechanical Toy (unprotected) | 1995 |

### Stern (2)

| Игра | Название | Год |
|---|---|---|
| armorcar | Armored Car (set 1) | 1981 |
| berzerk | Berzerk (set 1) | 1980 |

### Tehkan (2)

| Игра | Название | Год |
|---|---|---|
| bombjack | Bomb Jack (set 1) | 1984 |
| pleiads | Pleiads (Tehkan) | 1981 |

### Universal (2)

| Игра | Название | Год |
|---|---|---|
| docastle | Mr. Do's Castle (set 1) | 1983 |
| mrdo | Mr. Do! | 1982 |

### Visco (2)

| Игра | Название | Год |
|---|---|---|
| galmedes | Galmedes (Japan) | 1992 |
| stmblade | Storm Blade (US) | 1996 |

### NMK / Tecmo (2)

| Игра | Название | Год |
|---|---|---|
| gunnail | GunNail | 1993 |
| sabotenb | Saboten Bombers (set 1) | 1992 |

### Gottlieb (2)

| Игра | Название | Год |
|---|---|---|
| krull | Krull | 1983 |
| qbert | Q*bert (US set 1) | 1982 |

### Mitchell (2)

| Игра | Название | Год |
|---|---|---|
| osman | Osman (World) | 1996 |
| puzzloop | Puzz Loop (Europe) | 1998 |

### Banpresto / Gazelle (1)

| Игра | Название | Год |
|---|---|---|
| agallet | Air Gallet | 1996 |

### Kyugo (1)

| Игра | Название | Год |
|---|---|---|
| airwolf | Airwolf | 1987 |

### [Sanritsu] Sega (1)

| Игра | Название | Год |
|---|---|---|
| bankp | Bank Panic | 1984 |

### Data East USA (Bally Midway license) (1)

| Игра | Название | Год |
|---|---|---|
| bnj | Bump 'n' Jump | 1982 |

### Rare (1)

| Игра | Название | Год |
|---|---|---|
| btoads | Battle Toads | 1994 |

### Tad Corporation (1)

| Игра | Название | Год |
|---|---|---|
| cabal | Cabal (World, Joystick version) | 1988 |

### [Technos] (Taito Romstar license) (1)

| Игра | Название | Год |
|---|---|---|
| chinagat | China Gate (US) | 1988 |

### Falcon (1)

| Игра | Название | Год |
|---|---|---|
| ckong | Crazy Kong (set 1) | 1981 |

### Irem (Data East Corporation license) (1)

| Игра | Название | Год |
|---|---|---|
| dsoccr94 | Dream Soccer '94 | 1994 |

### Sammy (1)

| Игра | Название | Год |
|---|---|---|
| dynagear | Dyna Gears | 1994 |

### ???? (1)

| Игра | Название | Год |
|---|---|---|
| fantzn2 | Fantasy Zone 2 | 198? |

### [Data East] (Mitchell license) (1)

| Игра | Название | Год |
|---|---|---|
| funkyjet | Funky Jet | 1992 |

### Noise Factory (1)

| Игра | Название | Год |
|---|---|---|
| gaia | Gaia Crusaders | 1999 |

### Alpha Denshi Co. (1)

| Игра | Название | Год |
|---|---|---|
| gangwars | Gang Wars (US) | 1989 |

### [Namco] (Gremlin license) (1)

| Игра | Название | Год |
|---|---|---|
| geebeeg | Gee Bee (Gremlin) | 1978 |

### Fuuki (1)

| Игра | Название | Год |
|---|---|---|
| gogomile | Go Go! Mile Smile | 1995 |

### Sun Electronics (1)

| Игра | Название | Год |
|---|---|---|
| kangaroo | Kangaroo | 1982 |

### Techstar (Sunn license) (1)

| Игра | Название | Год |
|---|---|---|
| lizwiz | Lizard Wizard | 1985 |

### Konami (Centuri license) (1)

| Игра | Название | Год |
|---|---|---|
| locomotn | Loco-Motion | 1982 |

### Banpresto/Dynamic Pl. Toei Animation (1)

| Игра | Название | Год |
|---|---|---|
| mazinger | Mazinger Z | 1994 |

### Banpresto/Pandorabox (1)

| Игра | Название | Год |
|---|---|---|
| metmqstr | Metamoqester | 1995 |

### Irem America (1)

| Игра | Название | Год |
|---|---|---|
| nbbatman | Ninja Baseball Batman (US) | 1993 |

### Rock-ola (1)

| Игра | Название | Год |
|---|---|---|
| nibbler | Nibbler (set 1) | 1982 |

### UPL (1)

| Игра | Название | Год |
|---|---|---|
| ninjakd2 | Ninja-Kid II (set 1) | 1987 |

### [UPL] (Taito license) (1)

| Игра | Название | Год |
|---|---|---|
| ninjakun | Ninjakun Majou no Bouken | 1984 |

### [Namco] (Midway license) (1)

| Игра | Название | Год |
|---|---|---|
| pacman | Pac-Man (Midway) | 1980 |

### Subsino (1)

| Игра | Название | Год |
|---|---|---|
| penbros | Penguin Brothers (Japan) | 2000 |

### Amstar (1)

| Игра | Название | Год |
|---|---|---|
| phoenix | Phoenix (Amstar) | 1980 |

### Compile (Sega license) (1)

| Игра | Название | Год |
|---|---|---|
| puyopuy2 | Puyo Puyo 2 (Japan) | 1994 |

### Seibu Kaihatsu (1)

| Игра | Название | Год |
|---|---|---|
| raiden2 | Raiden 2 | 1993 |

### Taito Europe Corporation (1)

| Игра | Название | Год |
|---|---|---|
| rambo3 | Rambo III (Europe set 1) | 1989 |

### Technos (Taito America license) (1)

| Игра | Название | Год |
|---|---|---|
| renegade | Renegade (US) | 1986 |

### Nichibutsu + Alice (1)

| Игра | Название | Год |
|---|---|---|
| seicross | Seicross | 1984 |

### Toaplan (1)

| Игра | Название | Год |
|---|---|---|
| snowbros | Snow Bros. - Nick &amp; Tom (set 1) | 1990 |

### Jaleco / The Tetris Company (1)

| Игра | Название | Год |
|---|---|---|
| tetrisp2 | Tetris Plus 2 (World[Q]) | 1997 |

### Arika (1)

| Игра | Название | Год |
|---|---|---|
| tgm2p | Tetris the Absolute The Grand Master 2 Plus | 2000 |

### Capcom (Romstar license) (1)

| Игра | Название | Год |
|---|---|---|
| tigeroad | Tiger Road (US) | 1987 |

### Sega (Escape license) (1)

| Игра | Название | Год |
|---|---|---|
| wboy | Wonder Boy (set 1, new encryption) | 1986 |

## Не найдено в каталоге (26)

bongo, choplift, ckongpt2, ddcrew2, ddux, dmnfrnt, gauntlet2p, gstream, mainevt2p, moomesa, mwalk, nob, punkshot2, puyo, rampart2p, rbisland, seawolft, sf, simpsons2p, solrwarr, sonicfgt, spidman, ssridersubc, tmnt22pu, vendetta2pu, xmen2pa
