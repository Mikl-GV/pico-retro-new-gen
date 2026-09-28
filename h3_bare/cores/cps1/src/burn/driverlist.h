#ifndef CPS1_DRIVERLIST_H
#define CPS1_DRIVERLIST_H
// Минимальный driverlist для вендора CPS-1 (аналог генерируемого FBNeo-файла,
// который строит gamelist.pl из полного набора драйверов). Здесь — только
// те драйверы, которые нужны нашим ромам; список пополняется по игре.
// Имена — struct BurnDriver BurnDrvCps* из drv/capcom/d_cps1.cpp.
extern struct BurnDriver BurnDrvCps3wonders;
extern struct BurnDriver BurnDrvCpsKod;
extern struct BurnDriver BurnDrvCpsUnsquad;
extern struct BurnDriver BurnDrvCpsVarth;
extern struct BurnDriver BurnDrvCpsWillow;
extern struct BurnDriver BurnDrvCpsWof;
static struct BurnDriver* pDriver[] = {
    &BurnDrvCps3wonders,
    &BurnDrvCpsKod,
    &BurnDrvCpsUnsquad,
    &BurnDrvCpsVarth,
    &BurnDrvCpsWillow,
    &BurnDrvCpsWof,
};

// Атрибуция исходников (в FBNeo генерируется; нам не нужна — пустая таблица)
typedef struct { char* game_name; char* sourcefile; } game_sourcefile_entry;
static game_sourcefile_entry sourcefile_table[] = { { (char*)"", (char*)"" } };
#endif