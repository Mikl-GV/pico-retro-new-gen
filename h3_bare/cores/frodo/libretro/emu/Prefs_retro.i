void Prefs::set_drive8(char *filename,int type)
{
	strcpy(DrivePath[0], filename);
}

// r1284 (bare-metal): редактор предпочтений не нужен (виджетов нет).
// RunPrefsEditor()/ShowEditor вызываются из GUI-веток, на H3 недоступны;
// возвращаем «не изменено», чтобы не строить диалог.
bool Prefs::ShowEditor(bool startup, char *prefs_name)
{
	(void)startup; (void)prefs_name;
	return false;
}