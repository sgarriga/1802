/*******************************************************************************
* Convert file in PLOAD/PSAVE format back to listing text
* Validated against 1802 BASIC3 V1.1 on MemberCHIP Card Rev.B
*
* Refactored version: Uses lookup table instead of giant switch statement,
* improving maintainability and reducing code duplication.
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

// Token mapping structure for keyword lookup
typedef struct {
    uint8_t token;
    const char *keyword;
    bool add_space_before;  // Add space before keyword
    bool add_space_after;   // Add space after keyword
} TokenMap;

static uint8_t read_buffer[16];
static bool show_header = false;
static bool debug = false;

// Token mapping table - replaces large switch statement
static const TokenMap token_map[] = {
    {0x80, "REM",      false, false}, {0x81, "CLS",      false, false},
    {0x82, "STOP",     false, false}, {0x84, "END",      false, false},
    {0x85, "LET",      false, true},  {0x86, "PRINT",    true,  true},
    {0x87, "GOTO",     true,  true},  {0x88, "IF",       false, true},
    {0x89, "INPUT",    false, true},  {0x8a, "LIST",     false, true},
    {0x8b, "GOSUB",    false, true},  {0x8c, "RETURN",   false, false},
    {0x8d, "WAIT",     false, true},  {0x8e, "DIM",      false, true},
    {0x8f, "FOR",      false, true},  {0x90, "NEXT",     false, true},
    {0x91, "FIXED",    false, false}, {0x92, "POKE",     false, false},
    {0x93, "DEG",      false, false}, {0x94, "RAD",      false, false},
    {0x95, "STQ",      false, true},  {0x96, "DEFINT",   false, true},
    {0x97, "PSAVE",    false, false}, {0x98, "PLOAD",    false, false},
    {0x99, "DEFUS",    false, true},  {0x9a, "EOP",      false, false},
    {0x9b, "DATA",     false, true},  {0x9c, "READ",     false, true},
    {0x9d, "RESTORE",  false, false}, {0x9e, "EOD",      false, false},
    {0x9f, "CLD",      false, false}, {0xa0, "SEQ",      false, false},
    {0xa1, "REQ",      false, false}, {0xa2, "EINT",     false, true},
    {0xa3, "DISINT",   false, false}, {0xa4, "DMAPT",    false, true},
    {0xa5, "OUT",      false, false}, {0xa6, "INP",      false, false},
    {0xa7, "TRACE",    false, true},  {0xa8, "CALL",     false, false},
    {0xa9, "MONITOR",  false, false}, {0xaa, "NEW",      false, false},
    {0xab, "MEM",      false, false}, {0xac, "CLEAR",    false, false},
    {0xad, "BYE",      false, false}, {0xae, "EXIT",     false, false},
    {0xaf, "CONT",     false, false}, {0xb0, "AUTO",     false, false},
    {0xb1, "RENUMBER", false, true},  {0xb2, "EDIT",     false, false},
    {0xb3, "FORMAT",   false, true},  {0xb4, "USING",    false, false},
    {0xb5, "AND",      true,  true},  {0xb6, "XOR",      true,  true},
    {0xb7, "OR",       true,  true},  {0xb8, "NOT",      true,  true},
    {0xb9, "CHR$",     false, false}, {0xba, "MID$",     false, false},
    {0xbc, "TAB",      false, false}, {0xbd, ">=",       false, false},
    {0xbe, "<=",       false, false}, {0xbf, "<>",       false, false},
    {0xc0, " STEP ",   false, false}, {0xc1, " TO ",     false, false},
    {0xc5, "THEN",     true,  true},  {0xd4, "SIN",      false, false},
    {0xd5, "COS",      false, false}, {0xd8, "ATN",      false, false},
    {0xd9, "EXP",      false, false}, {0xda, "LOG",      false, false},
    {0xdb, "SQR",      false, false}, {0xdc, "INT",      false, false},
    {0xdd, "PEEK",     false, false}, {0xde, "ABS",      false, false},
    {0xdf, "RND",      false, false}, {0xe0, "USR",      false, false},
    {0xe1, "INUM",     false, false}, {0xe3, "FNUM",     false, false},
    {0xe4, "ASC",      false, false}, {0xe5, "LEN",      false, false},
    {0xe6, "VAL",      false, false}, {0xe7, "SGN",      false, false},
    {0xe8, "MOD",      false, false}, {0xe9, "INP",      false, false},
    {0xea, "NOT",      false, true},  {0xeb, "PI",       false, false},
    {0xec, "FVAL",     false, false}, {0xed, "STR$",     false, false},
    {0xee, "MEM",      false, false}, {0xef, "QST",      false, false},
    {0xf0, "EF",       false, false},
};

static const size_t token_map_size = sizeof(token_map) / sizeof(token_map[0]);

// Lookup token in mapping table, returns NULL if not found
static const TokenMap *lookup_token(uint8_t token) {
    for (size_t i = 0; i < token_map_size; i++) {
        if (token_map[i].token == token)
            return &token_map[i];
    }
    return NULL;
}

// read the next byte
static uint8_t get_next_byte(FILE *f) {
    uint8_t c = 0;

    // each byte is stored 2 encoded nybbles...  @=0 .. O=15
    if (fread(read_buffer, 2, 1, f))
        c = ((read_buffer[0]-64) << 4) + (read_buffer[1]-64);
    return c;
}

// read the header bytes
static void skip_header(FILE *f) {
    uint8_t c;

    // 14 byte header - TODO currently unknown format!
    if (show_header)
        printf("Header Bytes : ");
    for (uint8_t i = HEADER_BYTE_CNT; i > 0; i--) {
        c = get_next_byte(f);
        if (show_header)
            printf("%02x%s", c, (i==1)?"\n":", ");       
    }
}

// read and decode the byte sequence representing an integer value
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
        *byte_cnt += NUMBER_BYTE_CNT;
    }

    return num;
}

// Convert 4-byte Microsoft Binary Format float -> C float
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

// read and decode the byte sequence representing a floating point value
static float get_float_value(FILE *f, uint16_t *byte_cnt) {
    uint8_t mbf[NUMBER_BYTE_CNT];
    float num = 0.0;

    // float is 4-bytes in Microsoft Binary Format
    for (int i = 0; i < NUMBER_BYTE_CNT; i++)
        mbf[i] = get_next_byte(f);

    num = mbf_to_float(mbf); 
    *byte_cnt += NUMBER_BYTE_CNT;

    return num;
}

// remove leading and trailing zeros from a string representation of a floating
// point value as RCA BASIC3 prefers .1 to 0.100
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

// Handle special token: string variable (0xd7)
// Reads the variable letter and appends $
static uint8_t handle_string_var(int8_t *io, FILE *f, uint16_t *byte_cnt) {
    uint8_t c = get_next_byte(f);
    *io = c;
    io++;
    *io = '$';
    (*byte_cnt)++;
    return 1;  // -1 for consistency (writes 2 chars, returns 1)
}

// Handle special token: integer value (0xd2)
// Reads 4-byte integer and formats it
static uint8_t handle_int_value(int8_t *io, FILE *f, uint16_t *byte_cnt) {
    static char formatted[32];
    int32_t val = get_int_value(f, byte_cnt);
    int len = sprintf(formatted, "%d", val);
    strcpy((char *)io, formatted);
    return len - 1;  // -1 for consistency with byte_to_keyword()
}

// Handle special token: float value (0xd3)
// Reads 4-byte float and formats it
static uint8_t handle_float_value(int8_t *io, FILE *f, uint16_t *byte_cnt) {
    static char formatted[32];
    float val = get_float_value(f, byte_cnt);
    sprintf(formatted, "%f", val);
    char *trimmed = trim_float_str(formatted);
    strcpy((char *)io, trimmed);
    return strlen(trimmed) - 1;  // -1 for consistency with byte_to_keyword()
}

// Apply token mapping to output buffer with proper spacing
// Returns count - 1 to match original byte_to_keyword() behavior
// (combined with c++ at end of loop, advances by full count)
static uint8_t apply_keyword(int8_t *io, const char *keyword, bool add_space_before, bool add_space_after) {
    uint8_t incr = 0;

    if (add_space_before) {
        *io = ' ';
        io++;
        incr++;
    }
    strcpy((char *)io, keyword);
    incr += strlen(keyword);
    if (add_space_after) {
        *(io + incr) = ' ';
        incr++;
    }
    return incr - 1;
}

// read and decode each post-header line
static char *decode_line(FILE *f) {
    static char line_buffer[256]; // the line of source listing
    uint8_t *c = NULL;
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
    line_num = ((read_buffer[0]-64) << 12)  + ((read_buffer[1]-64) << 8) + 
               ((read_buffer[2]-64) << 4) + (read_buffer[3]-64);

    sprintf(line_buffer, "%hu ", line_num);
    c = (uint8_t *)line_buffer + strlen(line_buffer);

    while (*c = get_next_byte(f)) {
        byte_cnt++;

        if (byte_cnt == 1) {
            line_len = *c;
            if (debug)
                printf("\nRead byte %d as length %d\n", byte_cnt, line_len);
            continue; // 1st byte after line # is line length in bytes, including self
        }

        if (debug) {
            if (isprint(*c))
                printf("\nRead byte %d as source %02x (%c)\n", byte_cnt, *c, *c);
            else
                printf("\nRead byte %d as source %02x\n", byte_cnt, *c);
        }

        // 0x0d marks end of line
        if (*c == 0x0d) {
            *c = '\0';
            break;
        }

        // map the internal byte token to a keyword or printable character
        // sometimes the byte has side effects - use lookup table for cleaner code
        switch (*c) {
            case 0xd1: // "it's a variable"
                continue; // we don't map or save this, just skip the byte

            case 0xd2: // "it's a 2-byte int"
                c += handle_int_value((int8_t *)c, f, &byte_cnt);
                break;

            case 0xd3: // "it's a float"
                c += handle_float_value((int8_t *)c, f, &byte_cnt);
                break;

            case 0xd7: // "string var" - read variable letter and append $
                c += handle_string_var((int8_t *)c, f, &byte_cnt);
                break;

            case 0x83: // RUN - curiously eats a preceding space
                if (*(c - 1) == ' ')
                    c--;
                c += apply_keyword((int8_t *)c, "RUN", false, false);
                break;

            case 0x84: // END
                c += apply_keyword((int8_t *)c, "END", false, false);
                done = (line_num == 0xffff);
                break;

            // Single character mappings
            case 0xbb: *c = '^';  break;  // power
            case 0xc2: *c = ',';  break;  // comma
            case 0xc3: *c = ';';  break;  // continuation
            case 0xc4: *c = ')';  break;  // close paren
            case 0xc6: *c = '<';  break;  // less than
            case 0xc7: *c = '>';  break;  // greater than
            case 0xc8: *c = '+';  break;  // plus
            case 0xc9: *c = '-';  break;  // minus
            case 0xca: *c = '*';  break;  // multiply
            case 0xcb: *c = '/';  break;  // divide
            case 0xcc: *c = '=';  break;  // assignment
            case 0xcd: *c = ':';  break;  // statement separator
            case 0xce: // end quote
            case 0xcf: *c = '"'; break;  // start quote
            case 0xd0: *c = '@';  break;  // @
            case 0xd6: *c = '(';  break;  // open paren
            case 0xe2: *c = '#';  break;  // #

            // All other keywords from lookup table
            default: {
                const TokenMap *map = lookup_token(*c);
                if (map) {
                    c += apply_keyword((int8_t *)c, map->keyword, map->add_space_before, map->add_space_after);
                } else if (*c < ' ' || *c > '~') {
                    fprintf(stderr, "%02x(%c) ", *c, *c);
                    *c = '?';
                    unknown = true;
                }
            }
        }
        c++;
    }

    if (unknown) {
        fprintf(stderr, "\n");
    }
    if (line_len != byte_cnt) {
        fprintf(stderr, "Line %hu: Length mismatch (expected %u, got %u)\n", 
                line_num, line_len, byte_cnt);
    }
    if (line_num != 0xffff || !done) { 
        // "65535 END" is implicitly appended if the last source line is not END
        printf("%s\n", line_buffer);
    }
    if (unknown) {
        fprintf(stderr, "Stopped! Translate Error\n");
        exit(1);
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
            fprintf(stderr, "Usage:\n%s {options} filename\n", argv[0]);
            exit(1);
        }
    }

    if (arg >= argc) {
        fprintf(stderr, "Usage:\n%s {options} filename\n", argv[0]);
        exit(1);
    }

    f = fopen(argv[arg], "r");
    if (f == NULL) {
        int cc = errno;
        fprintf(stderr, "Stopped! Cannot open %s, got %d.\n", argv[arg], cc);
        exit(cc);
    }

    skip_header(f);
    while (decode_line(f) != NULL);

    fclose(f);
    exit(0);
}
