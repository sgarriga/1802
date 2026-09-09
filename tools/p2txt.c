/*******************************************************************************
 * Convert file in PLOAD/PSAVE format back to listing text
 * Validated against 1802 BASIC3 V1.1 on MemberCHIP Card Rev.B
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

#define HEADER_BYTE_CNT 14
#define NUMBER_BYTE_CNT 4

#define LINE_NUM_NYBBLES 4

static uint8_t read_buffer[16];
static bool show_header = false;
static bool debug = false;

typedef struct {
	uint8_t  token;
	const char *keyword;
	bool ensure_prespace;
} TokenMap;

static const TokenMap token_map[] = {
	{0x80, "REM", false},
	{0x81, "CLS", false},
	{0x82, "STOP", false},
	{0x83, "RUN", false},
	{0x84, "END", false},
	{0x85, "LET ", false},
	{0x86, "PRINT ", true},
	{0x87, "GOTO ", true},
	{0x88, "IF ", false},
	{0x89, "INPUT ", false},
	{0x8a, "LIST ", false},
	{0x8b, "GOSUB ", false},
	{0x8c, "RETURN", false},
	{0x8d, "WAIT ", false},
	{0x8e, "DIM ", false},
	{0x8f, "FOR ", false},
	{0x90, "NEXT ", false},
	{0x91, "FIXED", false},
	{0x92, "POKE", false},
	{0x93, "DEG", false},
	{0x94, "RAD", false},
	{0x95, "STQ ", false},
	{0x96, "DEFINT ", false},
	{0x97, "PSAVE", false},
	{0x98, "PLOAD", false},
	{0x99, "DEFUS ", false},
	{0x9a, "EOP", false},
	{0x9b, "DATA ", false},
	{0x9c, "READ ", false},
	{0x9d, "RESTORE", false},
	{0x9e, "EOD", false},
	{0x9f, "CLD", false},
	{0xa0, "SEQ", false},
	{0xa1, "REQ", false},
	{0xa2, "EINT ", false},
	{0xa3, "DISINT", false},
	{0xa4, "DMAPT ", false},
	{0xa5, "OUT", false},
	{0xa6, "INP", false},
	{0xa7, "TRACE ", false},
	{0xa8, "CALL", false},
	{0xa9, "MONITOR", false}, /* not MemberCHIP */
	{0xaa, "NEW", false},
	{0xab, "MEM", false},
	{0xac, "CLEAR", false},
	{0xad, "BYE", false},
	{0xae, "EXIT", false},
	{0xaf, "CONT", false},
	{0xb0, "AUTO", false},
	{0xb1, "RENUMBER ", false},
	{0xb2, "EDIT", false},
	{0xb3, "FORMAT ", false},
	{0xb4, "USING", false},
	{0xb5, "AND ", true},
	{0xb6, "XOR ", true},
	{0xb7, "OR ", true},
	{0xb8, "NOT ", true}, /* logical - not MemberCHIP */
	{0xb9, "CHR$", false},
	{0xba, "MID$", false},
	{0xbb, "^", false},
	{0xbc, "TAB", false},
	{0xbd, ">=", false},
	{0xbe, "<=", false},
	{0xbf, "<>", false},
	{0xc0, " STEP ", false},
	{0xc1, " TO ", false},
	{0xc2, ",", false},
	{0xc3, ";", false},
	{0xc4, ")", false},
	{0xc5, "THEN ", true},
	{0xc6, "<", false},
	{0xc7, ">", false},
	{0xc8, "+", false},
	{0xc9, "-", false},
	{0xca, "*", false},
	{0xcb, "/", false},
	{0xcc, "=", false},
	{0xcd, ":", false},
	{0xce, "\"", false},
	{0xcf, "\"", false},
	{0xd0, "@", false},
	{0xd1, "", false}, /* variable indicator */
	{0xd2, "", false},    /* 2-byte int indicator */  
	{0xd3, "", false},     /* float indicator */
	{0xd4, "SIN", false},
	{0xd5, "COS", false},
	{0xd6, "(", false},
	{0xd7, "", false},     /* string var indicator */
	{0xd8, "ATN", false},
	{0xd9, "EXP", false},
	{0xda, "LOG", false},
	{0xdb, "SQR", false},
	{0xdc, "INT", false},
	{0xdd, "PEEK", false},
	{0xde, "ABS", false},
	{0xdf, "RND", false},
	{0xe0, "USR", false},
	{0xe1, "INUM", false},
	{0xe2, "#", false},
	{0xe3, "FNUM", false},
	{0xe4, "ASC", false},
	{0xe5, "LEN", false},
	{0xe6, "VAL", false}, /* not MemberCHIP */
	{0xe7, "SGN", false},
	{0xe8, "MOD", false},
	{0xe9, "INP", false},
	{0xea, "NOT ", false}, /* bitwise */
	{0xeb, "PI", false},
	{0xec, "FVAL", false},
	{0xed, "STR$", false}, /* not MemberCHIP */
	{0xee, "MEM", false},
	{0xef, "QST", false},
	{0xf0, "EF", false}
};

static const size_t token_map_size = sizeof(token_map) / sizeof(token_map[0]);

// read the next byte
//
static uint8_t get_next_byte(FILE *f) {
	uint8_t c = 0;

	// each byte is stored 2 encoded nybbles...  @=0 .. O=15
	if (fread(read_buffer, 2, 1, f))
		c = ((read_buffer[0]-64) << 4) + (read_buffer[1]-64);
	return c;
}

// read the header bytes
//
static void skip_header(FILE *f) {
	uint8_t c;

	// 14 byte header - TODO currently unknown format!
	if (show_header)
		printf("Header Bytes : ");
	for (uint8_t i=HEADER_BYTE_CNT; i>0; i--) {
		c = get_next_byte(f);
		if (show_header)
			printf("%02x%s", c, (i==0)?"\n":", ");       
	}
}

// read and decode the byte sequence representing an integer value
//
static int32_t get_int_value(FILE *f, uint16_t *byte_cnt) {
	int32_t num = 0;

	// integers are 32-bit (4-bytes/8 nybbles MSB .. LSB)
	if (fread(read_buffer, (NUMBER_BYTE_CNT * 2), 1, f)) {
		num = ((read_buffer[0]-64) << 28) + 
			((read_buffer[1]-64) << 24) + 
			((read_buffer[2]-64) << 20) + 
			((read_buffer[3]-64) << 16) +
			((read_buffer[4]-64) << 12) + 
			((read_buffer[5]-64) << 8) + 
			((read_buffer[6]-64) << 4) + 
			(read_buffer[7]-64);
		*byte_cnt+=NUMBER_BYTE_CNT;
	}

	return num;
}

// Convert 4-byte Microsoft Binary Format float -> C float
//
static float mbf_to_float(uint8_t *mbf) {

	// 1. A zero exponent represents zero
	if (mbf[0] == 0) {
		return 0.0f;
	}

	// 2. Extract Unbiased Exponent (Bias = 128)
	uint8_t exponent = (int)mbf[0] - 128;

	// 3. Extract Sign (MSB of byte 1)
	int8_t sign = (mbf[1] & 0x80) ? -1 : 1;

	// 4. Reconstruct Mantissa bits
	// MBF implicit leading bit is 1 at position 23 (0.1_2 base)
	uint32_t mantissa_bits = (1U << 23) 
		| ((unsigned int)(mbf[1] & 0x7F) << 16) 
		| ((unsigned int)mbf[2] << 8) 
		| (unsigned int)mbf[3];

	// 5. Convert mantissa integer bits to fractional value (divide by 2^24)
	double mantissa = (double)mantissa_bits / (1U << 24);

	// 6. Calculate Final Value: Sign * Mantissa * 2^Exponent
	// (ldexp scales a float by powers of 2 efficiently)
	return (float)(sign * mantissa * ldexp(1.0, exponent));
}

// read and decode the byte sequence representing an floating point value
//
static float get_float_value(FILE *f, uint16_t *byte_cnt) {
	uint8_t mbf[NUMBER_BYTE_CNT];
	float num = 0.0;

	// float is 4-bytes in Microsoft Binary Format
	for (int i=0; i < NUMBER_BYTE_CNT; i++)
		mbf[i] = get_next_byte(f);

	num = mbf_to_float(mbf); 
	*byte_cnt+=NUMBER_BYTE_CNT;

	return num;
}

// remove leading and trailing zeros from a string representation of a floating
// point value as RCA BASIC3 prefers .1 to 0.100
//
static char *trim_float_str(char *in) {
	static char out[8] = ".0";
	uint8_t l;

	// trim leading 0   
	while (*in == '0')
		in++;
	l = strlen(in);

	// trim trailing 0s
	if (l) {
		strcpy(out, in);
		while ((l > 0) && (out[--l] == '0'))
			out[l] = '\0';
	}

	return out;
}

// convert append byte or keyword
//
static uint8_t add_byte_or_keyword(int8_t *io, const char *i, bool prespace) {
	int8_t incr = 0;


	if (prespace && *(io-1) != ' ') { // add leading space if there isn't one
		*io = ' ';
		io++;
		incr++;
	}
	strcpy((char *)io, i); 
	incr += strlen(i);
	return incr;
}

// read and decode each post-header line
//
static char *decode_line(FILE *f) {
	static char line_buffer[256]; // the line of source listing
	static char formatted_val[16]; // used when we have to format numeric values
	uint8_t *c = NULL;
	uint8_t byte = '\0';
	uint16_t line_num = 0;
	uint8_t line_len = 0;
	uint16_t byte_cnt = 0;
	bool unknown = false;
	bool done = false;

	memset(line_buffer, 0, sizeof(line_buffer));

	// each character in file is a nybble
	// 1st 4 nybbles are line number, FFFF may indicate end of listing (unsure)
	if (!fread(read_buffer, LINE_NUM_NYBBLES, 1, f))
		return NULL;
	line_num = ((read_buffer[0]-64) << 12)  + ((read_buffer[1]-64) << 8) + ((read_buffer[2]-64) << 4) + (read_buffer[3]-64);

	sprintf(line_buffer, "%hu ", line_num);
	c = line_buffer + strlen(line_buffer);

	while (byte = get_next_byte(f)) {
		byte_cnt++;

		if (byte_cnt == 1) {
			line_len = byte;
			if (debug)
				printf("\nRead byte %d as length %d\n", byte_cnt, line_len);
			continue; // 1st byte after line # is line length in bytes, including self
		}

		if (debug) {
			if (isprint(*c))
				printf("\nRead byte %d as source %02x (%c)\n", byte_cnt, byte, byte);
			else
				printf("\nRead byte %d as source %02x\n", byte_cnt, byte);
		}

		// 0x0d marks end of line, so let's shortcut later logic
		if (byte == 0x0d) {
			*c = '\0';
			break;
		}

		// **Special Pre Processing **

		if (byte == 0x83 && *(c - 1) == ' ') {
			c--; // RUN seems to eat a preceeding space
		}

		// map the internal byte token to a keyword 
		// sometimes the byte has side effects
		if (byte >= 0x80 && byte <= 0xf0) {
			for (size_t i = 0; i < token_map_size; i++) {
				if (token_map[i].token == byte) {
					c+=add_byte_or_keyword(c, token_map[i].keyword, token_map[i].ensure_prespace);
				}
			} 
		}
		else if (byte < ' ' || byte > '~') {
			printf("%02x(%c) ", byte, byte);
			*c = '?';
			c++;
			unknown = true;;
		}
		else {
                        *c = byte;
			c++;
                }

		// ** Special Post Processing **
		switch(byte) {

			case 0x84: // "63335 END" is saved if there isn't an explicit last line with "END", but is not shown by LIST if it was entered
				done = (line_num == 0xffff);
                                break;

			case 0xd2: // read the integer
				sprintf(formatted_val, "%d", get_int_value(f, &byte_cnt));
				c+=add_byte_or_keyword(c, formatted_val, false);
				break;

			case 0xd3: // read the float
				sprintf(formatted_val, "%f", get_float_value(f, &byte_cnt));
				c+=add_byte_or_keyword(c, trim_float_str(formatted_val), false);
				break;

			case 0xd7: // read the string variable name and append $
				*c = get_next_byte(f);
				byte_cnt++;
				c++;
				*c = '$';
				c++;
				break;

		}
	}

	if (unknown) {
		printf("\n");
	}
	if (line_len !=  byte_cnt) {
		printf("Line Read Error!! %d vs %d\n", line_len, byte_cnt);
	}
	if (line_num != 0xffff || !done) { // TODO is line # 65535 allowed, or reserved for an implicit END
					   // if the last source line is not END, "65535 END" seems to be appended
		printf("%s\n", line_buffer);
	}
	if (unknown) {
		printf("Stopped! Translate Error\n");
		exit(0);
	}
	return c;
}

int main(int argc, char *argv[])
{
	FILE *f;
	int arg;

	// process opts
	for (arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-')
			break; 
		else if (!strcmp(argv[arg], "-h"))
			show_header = true;
		else {
			printf("Usage:\n%s {options} filename\n", argv[0]);
			exit(0);
		}
	}

	f = fopen(argv[arg], "r");
	if (f == NULL) {
		int cc = errno;
		printf("Stopped! Cannot open %s, got %d.\n", argv[1], cc);
		exit(cc);
	}

	skip_header(f);
	while (decode_line(f) != NULL);

	fclose(f);
	exit(0);
}
