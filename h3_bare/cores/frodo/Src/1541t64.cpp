/*
 *  1541t64.cpp - 1541 emulation in archive-type files (.t64/LYNX/.p00)
 *
 *  Frodo (C) 1994-1997,2002-2005 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  NOTES:
 *   - This module handles access to files inside (uncompressed) archives
 *     and makes the archive look like a disk. It supports C64S tape images
 *     (.t64), C64 LYNX archives and .p00 files.
 *   - If any file is opened, the contents of the file in the archive file are
 *     copied into a temporary file which is used for reading. This is done
 *     to insert the load address.
 *
 *  Incompatibilities:
 *   - Only read accesses possible
 *   - No "raw" directory reading
 *   - No relative/sequential/user files
 *   - Unimplemented commands: B-P, M-R, M-W, C, S, P, N
 *   - Impossible to implement: B-R, B-W, B-E, B-A, B-F, M-E
 */

#include "sysdeps.h"

#include "1541t64.h"
#include "IEC.h"
#include "Prefs.h"

#define DEBUG 0
#include "debug.h"

#if defined(__LIBRETRO__) 
#if defined(ANDROID) || defined(__ANDROID__)
#include "android_tmpfile.i"
#endif
#endif

// Prototypes
static bool is_t64_header(const uint8 *header);
static bool is_lynx_header(const uint8 *header);
static bool is_p00_header(const uint8 *header);
static bool parse_t64_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title);
static bool parse_lynx_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title);
static bool parse_p00_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title);


/*
 *  Constructor: Prepare emulation
 */

ArchDrive::ArchDrive(IEC *iec, const char *filepath) : Drive(iec), the_file(NULL)
{
	for (int i=0; i<16; i++) {
		mem[i] = NULL;
		mem_size[i] = 0;
		mem_pos[i] = 0;
	}
	Reset();

	// Open archive file
	if (change_arch(filepath))
		Ready = true;
}


/*
 *  Destructor
 */

ArchDrive::~ArchDrive()
{
	// Close archive file
	if (the_file) {
		close_all_channels();
		fclose(the_file);
	}
	Ready = false;
}


/*
 *  Open the archive file
 */

bool ArchDrive::change_arch(const char *path)
{
	FILE *new_file;

	// Open new archive file
	if ((new_file = fopen(path, "rb")) != NULL) {

		file_info.clear();

		// Read header, determine archive type and parse archive contents
		uint8 header[64];
		fread(header, 1, 64, new_file);
		bool parsed_ok = false;
		if (is_t64_header(header)) {
			archive_type = TYPE_T64;
			parsed_ok = parse_t64_file(new_file, file_info, dir_title);
		} else if (is_lynx_header(header)) {
			archive_type = TYPE_LYNX;
			parsed_ok = parse_lynx_file(new_file, file_info, dir_title);
		} else if (is_p00_header(header)) {
			archive_type = TYPE_P00;
			parsed_ok = parse_p00_file(new_file, file_info, dir_title);
		}

		if (!parsed_ok) {
			fclose(new_file);
			if (the_file) {
				close_all_channels();
				fclose(the_file);
				the_file = NULL;
			}
			return false;
		}

		// r1290: в логе видно, сколько записей реально разобрано в архиве —
		// по нему отличаем «не тот файл выбран» от «архив не распознан».
		printf("arch: %s, %d entries\n", path, (int)file_info.size());

		// Close old archive if open, and set new file
		if (the_file) {
			close_all_channels();
			fclose(the_file);
			the_file = NULL;
		}
		the_file = new_file;
		return true;
	}
	return false;
}


/*
 *  Open channel
 */

uint8 ArchDrive::Open(int channel, const uint8 *name, int name_len)
{
	D(bug("ArchDrive::Open channel %d, file %s\n", channel, name));

	set_error(ERR_OK);

	// Channel 15: Execute file name as command
	if (channel == 15) {
		execute_cmd(name, name_len);
		return ST_OK;
	}

	// Close previous file if still open
	if (mem[channel]) {
		delete[] mem[channel];
		mem[channel] = NULL;
		mem_size[channel] = mem_pos[channel] = 0;
	}

	if (name[0] == '#') {
		set_error(ERR_NOCHANNEL);
		return ST_OK;
	}

	if (the_file == NULL) {
		set_error(ERR_NOTREADY);
		return ST_OK;
	}

	if (name[0] == '$')
		return open_directory(channel, name + 1, name_len - 1);

	return open_file(channel, name, name_len);
}


/*
 *  Open file
 */

uint8 ArchDrive::open_file(int channel, const uint8 *name, int name_len)
{
	uint8 plain_name[NAMEBUF_LENGTH];
	int plain_name_len;
	int mode = FMODE_READ;
	int type = FTYPE_DEL;
	int rec_len = 0;
	parse_file_name(name, name_len, plain_name, plain_name_len, mode, type, rec_len);

	// Channel 0 is READ, channel 1 is WRITE
	if (channel == 0 || channel == 1) {
		mode = channel ? FMODE_WRITE : FMODE_READ;
		if (type == FTYPE_DEL)
			type = FTYPE_PRG;
	}

	bool writing = (mode == FMODE_WRITE || mode == FMODE_APPEND);

	// Wildcards are only allowed on reading
	if (writing && (strchr((const char *)plain_name, '*') || strchr((const char *)plain_name, '?'))) {
		set_error(ERR_SYNTAX33);
		return ST_OK;
	}

	// Allow only read accesses
	if (writing) {
		set_error(ERR_WRITEPROTECT);
		return ST_OK;
	}

	// Relative files are not supported
	if (type == FTYPE_REL) {
		set_error(ERR_UNIMPLEMENTED);
		return ST_OK;
	}

	// Find file
	int num;
	if (find_first_file(plain_name, plain_name_len, num)) {
		// r1290: видно, какую именно запись выбрал LOAD"*" (имя, размер, offset).
		printf("arch: open '%s' size=%u off=%u\n", file_info[num].name,
		       (unsigned)file_info[num].size, (unsigned)file_info[num].offset);

		// r1289: файл копируем в RAM (tmpfile() в bare-metal недоступен).
		// Для .t64 первыми идут два байта адреса загрузки — их ждёт KERNAL.
		size_t hdr = (archive_type == TYPE_T64) ? 2 : 0;
		size_t total = hdr + file_info[num].size;
		uint8 *buf = new uint8[total];

		if (buf) {
			if (hdr) {
				buf[0] = file_info[num].sa_lo;
				buf[1] = file_info[num].sa_hi;
			}
			fseek(the_file, file_info[num].offset, SEEK_SET);
			if (fread(buf + hdr, 1, file_info[num].size, the_file) == file_info[num].size) {
				mem[channel] = buf;
				mem_size[channel] = total;
				mem_pos[channel] = 0;
				if (mode == FMODE_READ)	// Read and buffer first byte
					read_char[channel] = mem[channel][mem_pos[channel]++];
			} else {
				delete[] buf;
				set_error(ERR_FILENOTFOUND);
			}
		} else
			set_error(ERR_FILENOTFOUND);
	} else
		set_error(ERR_FILENOTFOUND);

	return ST_OK;
}


/*
 *  Find first file matching wildcard pattern
 */

// Return true if name 'n' matches pattern 'p'
static bool match(const uint8 *p, int p_len, const uint8 *n)
{
	while (p_len-- > 0) {
		if (*p == '*')	// Wildcard '*' matches all following characters
			return true;
		if ((*p != *n) && (*p != '?'))	// Wildcard '?' matches single character
			return false;
		p++; n++;
	}

	return *n == 0;
}

bool ArchDrive::find_first_file(const uint8 *pattern, int pattern_len, int &num)
{
	// r1290: LOAD"*" брал первую запись архива, а ею часто оказывается
	// заставка или загрузчик. Для wildcard выбираем самый крупный файл —
	// это обычно сама игра.
	bool wildcard = (pattern_len == 1 && pattern[0] == '*');
	int best = -1;
	size_t best_size = 0;

	vector<c64_dir_entry>::const_iterator i, end = file_info.end();
	int idx = 0;
	for (i = file_info.begin(); i != end; i++, idx++) {
		if (!match(pattern, pattern_len, (uint8 *)i->name))
			continue;
		if (!wildcard) {
			num = idx;
			return true;
		}
		if (i->size > best_size) {
			best_size = i->size;
			best = idx;
		}
	}

	if (wildcard && best >= 0) {
		num = best;
		return true;
	}
	return false;
}


/*
 *  Open directory, create temporary file
 */

uint8 ArchDrive::open_directory(int channel, const uint8 *pattern, int pattern_len)
{
	// Special treatment for "$0"
	if (pattern[0] == '0' && pattern_len == 1) {
		pattern++;
		pattern_len--;
	}

	// Skip everything before the ':' in the pattern
	uint8 *t = (uint8 *)memchr(pattern, ':', pattern_len);
	if (t) {
		t++;
		pattern_len -= t - pattern;
		pattern = t;
	}

	// r1289: каталог собираем в RAM (tmpfile() в bare-metal недоступен).
	// 32 байта заголовка + по 32 на совпавшую запись + финальная строка.
	int matches = 0;
	for (vector<c64_dir_entry>::const_iterator it = file_info.begin(); it != file_info.end(); it++)
		if (pattern_len == 0 || match(pattern, pattern_len, (uint8 *)it->name))
			matches++;

	uint8 *out = new uint8[(matches + 2) * 32];
	if (!out)
		return ST_OK;

	uint8 *w = out;
	uint8 buf[32];

	// Create directory title
	memcpy(buf, "\001\004\001\001\0\0\022\042                \042 00 2A", 32);
	for (int i=0; i<16 && dir_title[i]; i++)
		buf[i + 8] = dir_title[i];
	memcpy(w, buf, 32); w += 32;

	// Create one line for every directory entry
	for (vector<c64_dir_entry>::const_iterator i = file_info.begin(); i != file_info.end(); i++) {

		// Include only files matching the pattern
		if (!(pattern_len == 0 || match(pattern, pattern_len, (uint8 *)i->name)))
			continue;

		// Clear line with spaces and terminate with null byte
		memset(buf, ' ', 31);
		buf[31] = 0;

		uint8 *p = buf;
		*p++ = 0x01;	// Dummy line link
		*p++ = 0x01;

		// Calculate size in blocks (254 bytes each)
		int n = (i->size + 254) / 254;
		*p++ = n & 0xff;
		*p++ = (n >> 8) & 0xff;

		p++;
		if (n < 10) p++;	// Less than 10: add one space
		if (n < 100) p++;	// Less than 100: add another space

		// Convert and insert file name
		*p++ = '\"';
		for (int j=0; j<16 && i->name[j]; j++)
			*p++ = i->name[j];
		*p++ = '\"';
		p += 18;

		// File type
		switch (i->type) {
			case FTYPE_DEL: *p++ = 'D'; *p++ = 'E'; *p++ = 'L'; break;
			case FTYPE_SEQ: *p++ = 'S'; *p++ = 'E'; *p++ = 'Q'; break;
			case FTYPE_PRG: *p++ = 'P'; *p++ = 'R'; *p++ = 'G'; break;
			case FTYPE_USR: *p++ = 'U'; *p++ = 'S'; *p++ = 'R'; break;
			case FTYPE_REL: *p++ = 'R'; *p++ = 'E'; *p++ = 'L'; break;
			default:        *p++ = '?'; *p++ = '?'; *p++ = '?'; break;
		}

		memcpy(w, buf, 32); w += 32;
	}

	// Final line
	memcpy(w, "\001\001\0\0BLOCKS FREE.             \0\0", 32); w += 32;

	mem[channel] = out;
	mem_size[channel] = (size_t)(w - out);
	mem_pos[channel] = 0;
	read_char[channel] = mem[channel][mem_pos[channel]++];

	return ST_OK;
}


/*
 *  Close channel
 */

uint8 ArchDrive::Close(int channel)
{
	D(bug("ArchDrive::Close channel %d\n", channel));

	if (channel == 15) {
		close_all_channels();
		return ST_OK;
	}

	if (mem[channel]) {
		delete[] mem[channel];
		mem[channel] = NULL;
		mem_size[channel] = mem_pos[channel] = 0;
	}

	return ST_OK;
}


/*
 *  Close all channels
 */

void ArchDrive::close_all_channels(void)
{
	for (int i=0; i<15; i++)
		Close(i);

	cmd_len = 0;
}


/*
 *  Read from channel
 */

uint8 ArchDrive::Read(int channel, uint8 &byte)
{
	D(bug("ArchDrive::Read channel %d\n", channel));

	// Channel 15: Error channel
	if (channel == 15) {
		byte = *error_ptr++;

		if (byte != '\x0d')
			return ST_OK;
		else {	// End of message
			set_error(ERR_OK);
			return ST_EOF;
		}
	}

	if (!mem[channel]) return ST_READ_TIMEOUT;

	// Get char from buffer and read next
	byte = read_char[channel];
	if (mem_pos[channel] >= mem_size[channel])
		return ST_EOF;
	read_char[channel] = mem[channel][mem_pos[channel]++];
	return ST_OK;
}


/*
 *  Write to channel
 */

uint8 ArchDrive::Write(int channel, uint8 byte, bool eoi)
{
	D(bug("ArchDrive::Write channel %d, byte %02x, eoi %d\n", channel, byte, eoi));

	// Channel 15: Collect chars and execute command on EOI
	if (channel == 15) {
		if (cmd_len > 58) {
			set_error(ERR_SYNTAX32);
			return ST_TIMEOUT;
		}
		
		cmd_buf[cmd_len++] = byte;

		if (eoi) {
			execute_cmd(cmd_buf, cmd_len);
			cmd_len = 0;
		}
		return ST_OK;
	}

	if (!mem[channel])
		set_error(ERR_FILENOTOPEN);
	else
		set_error(ERR_WRITEPROTECT);

	return ST_TIMEOUT;
}


/*
 *  Execute drive commands
 */

// RENAME:new=old
//        ^   ^
// new_file   old_file
void ArchDrive::rename_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_file, int old_file_len)
{
	// Check if destination file is already present
	int num;
	if (find_first_file(new_file, new_file_len, num)) {
		set_error(ERR_FILEEXISTS);
		return;
	}

	// Check if source file is present
	if (!find_first_file(old_file, old_file_len, num)) {
		set_error(ERR_FILENOTFOUND);
		return;
	}

	set_error(ERR_WRITEPROTECT);
}

// INITIALIZE
void ArchDrive::initialize_cmd(void)
{
	close_all_channels();
}

// VALIDATE
void ArchDrive::validate_cmd(void)
{
}


/*
 *  Reset drive
 */

void ArchDrive::Reset(void)
{
	close_all_channels();
	cmd_len = 0;	
	set_error(ERR_STARTUP);
}


/*
 *  Check whether file with given header (64 bytes) and size looks like one
 *  of the file types supported by this module
 */

static bool is_t64_header(const uint8 *header)
{
	if (memcmp(header, "C64S tape file", 14) == 0
	 || memcmp(header, "C64 tape image", 14) == 0
	 || memcmp(header, "C64S tape image", 15) == 0)
		return true;
	else
		return false;
}

static bool is_lynx_header(const uint8 *header)
{
	return memcmp(header + 0x38, "USE LYNX", 8) == 0;
}

static bool is_p00_header(const uint8 *header)
{
	return memcmp(header, "C64File", 7) == 0;
}

bool IsArchFile(const char *path, const uint8 *header, long size)
{
	return is_t64_header(header) || is_lynx_header(header) || is_p00_header(header);
}


/*
 *  Read directory of archive file into (empty) c64_dir_entry vector,
 *  returns false on error
 */

static bool parse_t64_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title)
{
	// r1288: размер нужен для проверки записей (offset/размер вне файла).
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	if (fsize < 96)
		return false;

	// Заголовок: max/title лежат в блоке на offset 32 (T64 v1).
	fseek(f, 32, SEEK_SET);
	uint8 hdr[32];
	if (fread(hdr, 32, 1, f) != 1)
		return false;
	memcpy(dir_title, hdr + 8, 16);

	// r1288: таблица записей начинается с 64 (стандартный заголовок 32+32),
	// но у T64 с текстовым заголовком ("C64 tape image file") блок на 2 байта
	// короче — записи с 62. Пробуем обе базы и берём ту, что даёт валидные
	// записи. Поле max из заголовка у таких файлов равно 0 (старый код читал
	// ровно одну запись и промахивался).
	for (int base = 64; base >= 62; base -= 2) {
		fseek(f, base, SEEK_SET);
		uint8 rec[32];
		int added = 0;

		while (fread(rec, 32, 1, f) == 1) {
			if (rec[0] != 1)
				continue;

			size_t size = ((size_t)((rec[5] << 8) | rec[4]))
			            - ((size_t)((rec[3] << 8) | rec[2]));
			off_t offset = (off_t)((rec[11] << 24) | (rec[10] << 16) | (rec[9] << 8) | rec[8]);

			// Служебные записи (start=end) и данные вне файла не нужны.
			if (size == 0 || offset <= 0 || offset >= fsize || (off_t)size > fsize - offset)
				continue;

			// Convert file name (strip trailing spaces)
			uint8 name_buf[17];
			memcpy(name_buf, rec + 16, 16);
			name_buf[16] = 0x20;
			uint8 *p = name_buf + 16;
			while (*p-- == 0x20) ;
			p[2] = 0;

			vec.push_back(c64_dir_entry(name_buf, FTYPE_PRG, false, false, size, offset, rec[2], rec[3]));
			added++;
		}

		if (added)
			return true;
	}

	return false;
}

static bool parse_lynx_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title)
{
	// Dummy directory title
	strcpy(dir_title, "LYNX ARCHIVE    ");

	// Read header and get number of directory blocks and files contained
	fseek(f, 0x60, SEEK_SET);
	int dir_blocks;
	fscanf(f, "%d", &dir_blocks);
	while (getc(f) != 0x0d)
		if (feof(f))
			return false;
	int num_files;
	fscanf(f, "%d\x0d", &num_files);

	// Construct file information array
	vec.reserve(num_files);
	int cur_offset = dir_blocks * 254;
	for (int i=0; i<num_files; i++) {

		// Read and convert file name (strip trailing shift-spaces)
		uint8 name_buf[17];
		fread(name_buf, 16, 1, f);
		name_buf[16] = 0xa0;
		uint8 *p = name_buf + 16;
		while (*p-- == 0xa0) ;
		p[2] = 0;

		// Read file length and type
		int num_blocks, last_block;
		char type_char;
		fscanf(f, "\x0d%d\x0d%c\x0d%d\x0d", &num_blocks, &type_char, &last_block);
		size_t size = (num_blocks - 1) * 254 + last_block - 1;

		int type;
		switch (type_char) {
			case 'S':
				type = FTYPE_SEQ;
				break;
			case 'U':
				type = FTYPE_USR;
				break;
			case 'R':
				type = FTYPE_REL;
				break;
			default:
				type = FTYPE_PRG;
				break;
		}

		// Read start address
		long here = ftell(f);
		uint8 sa_lo, sa_hi;
		fseek(f, cur_offset, SEEK_SET);
		fread(&sa_lo, 1, 1, f);
		fread(&sa_hi, 1, 1, f);
		fseek(f, here, SEEK_SET);

		// Add entry
		vec.push_back(c64_dir_entry(name_buf, type, false, false, size, cur_offset, sa_lo, sa_hi));

		cur_offset += num_blocks * 254;
	}

	return true;
}

static bool parse_p00_file(FILE *f, vector<c64_dir_entry> &vec, char *dir_title)
{
	// Dummy directory title
	strcpy(dir_title, ".P00 FILE       ");

	// Contains only one file
	vec.reserve(1);

	// Read file name and start address
	uint8 name_buf[17];
	fseek(f, 8, SEEK_SET);
	fread(name_buf, 17, 1, f);
	name_buf[16] = 0;
	uint8 sa_lo, sa_hi;
	fseek(f, 26, SEEK_SET);
	fread(&sa_lo, 1, 1, f);
	fread(&sa_hi, 1, 1, f);

	// Get file size
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f) - 26;

	// Add entry
	vec.push_back(c64_dir_entry(name_buf, FTYPE_PRG, false, false, size, 26, sa_lo, sa_hi));
	return true;
}

bool ReadArchDirectory(const char *path, vector<c64_dir_entry> &vec)
{
	// Open file
	FILE *f = fopen(path, "rb");
	if (f) {

		// Read header
		uint8 header[64];
		fread(header, 1, sizeof(header), f);

		// Determine archive type and parse archive
		bool result = false;
		char dir_title[16];
		if (is_t64_header(header))
			result = parse_t64_file(f, vec, dir_title);
		else if (is_lynx_header(header))
			result = parse_lynx_file(f, vec, dir_title);
		else if (is_p00_header(header))
			result = parse_p00_file(f, vec, dir_title);

		fclose(f);
		return result;
	} else
		return false;
}
