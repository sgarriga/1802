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
            printf("%02x%s", (i==0)?"\n":", ");       
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

// convert byte to keyword
//
static uint8_t byte_to_keyword(int8_t *io, char *i, bool prespace) {
    int8_t incr = 0;


    if (prespace && *(io-1) != ' ') { // add leading space if there isn't one
        *io = ' ';
        io++;
        incr++;
    }
    strcpy((char *)io, i); 
    incr += strlen(i) - 1;
    return incr;
}

// read and decode each post-header line
//
static char *decode_line(FILE *f) {
    static char line_buffer[256]; // the line of source listing
    static char formatted_val[8]; // used when we have to format numeric values
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
    line_num = ((read_buffer[0]-64) << 12)  + ((read_buffer[1]-64) << 8) + ((read_buffer[2]-64) << 4) + (read_buffer[3]-64);

    sprintf(line_buffer, "%hu ", line_num);
    c = line_buffer + strlen(line_buffer);

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
        // sometimes the byte has side effects
        switch(*c) {
            case 0x80: // REM
                 c+=byte_to_keyword(c, "REM", false);
                 break;

            case 0x81: // CLS
                 c+=byte_to_keyword(c, "CLS", false);
                 break;

            case 0x82: // STOP - not MemberCHIP
                 c+=byte_to_keyword(c, "STOP", false);
                 break;

            case 0x83: // RUN
                 // curiously RUN seems to eat a preceeding space
                 if (*(c-1) == ' ')
                     c--;
                 c+=byte_to_keyword(c, "RUN", false);
                 break;

            case 0x84: // END
                 c+=byte_to_keyword(c, "END", false);
                 done = (line_num == 0xffff);
                 // "63335 END" is saved if there isn't an explicit last line with "END", but is not shown by LIST if it was entered
                 break;

            case 0x85: // LET
                 c+=byte_to_keyword(c, "LET ", false);
                 break;

            case 0x86: // PRINT
                 c+=byte_to_keyword(c, "PRINT ", true);
                 break;

            case 0x87: // GOTO
                 c+=byte_to_keyword(c, "GOTO ", true);
                 break;

            case 0x88: // IF 
                 c+=byte_to_keyword(c, "IF ", false);
                 break;

            case 0x89: // INPUT
                 c+=byte_to_keyword(c, "INPUT ", false);
                 // INPUT "MSG" A$ shown by LIST as INPUT "MSG"A$
                 break;

            case 0x8a: // LIST
                 c+=byte_to_keyword(c, "LIST ", false);
                 break;

            case 0x8b: // GOSUB
                 c+=byte_to_keyword(c, "GOSUB ", false);
                 break;

            case 0x8c: // RETURN
                 c+=byte_to_keyword(c, "RETURN", false);
                 break;

            case 0x8d: // WAIT
                 c+=byte_to_keyword(c, "WAIT ", false);
                 break;

            case 0x8e: // DIM
                 c+=byte_to_keyword(c, "DIM ", false);
                 break;

            case 0x8f: // FOR
                 c+=byte_to_keyword(c, "FOR ", false);
                 break;

            case 0x90: // NEXT
                 c+=byte_to_keyword(c, "NEXT ", false);
                 break;

            case 0x91: // FIXED
                 c+=byte_to_keyword(c, "FIXED", false);
                 break;

            case 0x92: // POKE
                 c+=byte_to_keyword(c, "POKE", false);
                 break;

            case 0x93: // DEG
                 c+=byte_to_keyword(c, "DEG", false);
                 break;

            case 0x94: // RAD
                 c+=byte_to_keyword(c, "RAD", false);
                 break;

            case 0x95: // STQ
                 c+=byte_to_keyword(c, "STQ ", false);
                 break;

            case 0x96: // DEFINT
                 c+=byte_to_keyword(c, "DEFINT ", false);
                 break;

            case 0x97: // PSAVE
                 c+=byte_to_keyword(c, "PSAVE", false);
                 break;

            case 0x98: // PLOAD
                 c+=byte_to_keyword(c, "PLOAD", false);
                 break;

            case 0x99: // DEFUS
                 c+=byte_to_keyword(c, "DEFUS ", false);
                 break;

            case 0x9a: // EOP
                 c+=byte_to_keyword(c, "EOP", false);
                 break;

            case 0x9b: // DATA
                 c+=byte_to_keyword(c, "DATA ", false);
                 break;

            case 0x9c: // READ
                 c+=byte_to_keyword(c, "READ ", false);
                 break;

            case 0x9d: // RESTORE
                 c+=byte_to_keyword(c, "RESTORE", false);
                 break;

            case 0x9e: // EOD
                 c+=byte_to_keyword(c, "EOD", false);
                 break;

            case 0x9f: // CLD
                 c+=byte_to_keyword(c, "CLD", false);
                 break;

            case 0xa0: // SEQ - not MemberCHIP
                 c+=byte_to_keyword(c, "SEQ", false);
                 break;

            case 0xa1: // REQ - not MemberCHIP
                 c+=byte_to_keyword(c, "REQ", false);
                 break;

            case 0xa2: // EINT
                 c+=byte_to_keyword(c, "EINT ", false);
                 break;

            case 0xa3: // DISINT
                 c+=byte_to_keyword(c, "DISINT", false);
                 break;

            case 0xa4: // DMAPT
                 c+=byte_to_keyword(c, "DMAPT ", false);
                 break;

            case 0xa5: // OUT - not MemberCHIP
                 c+=byte_to_keyword(c, "OUT", false);
                 break;

            case 0xa6: // INP
                 c+=byte_to_keyword(c, "INP", false);
                 break;

            case 0xa7: // TRACE
                 c+=byte_to_keyword(c, "TRACE ", false);
                 break;

            case 0xa8: // CALL
                 c+=byte_to_keyword(c, "CALL", false);
                 break;

            case 0xa9: // MONITOR - not MemberCHIP
                 c+=byte_to_keyword(c, "MONITOR", false);
                 break;

            case 0xaa: // NEW
                 c+=byte_to_keyword(c, "NEW", false);
                 break;

            case 0xab: // MEM
                 c+=byte_to_keyword(c, "MEM", false);
                 break;

            case 0xac: // CLEAR - not MemberCHIP
                 c+=byte_to_keyword(c, "CLEAR", false);
                 break;

            case 0xad: // BYE
                 c+=byte_to_keyword(c, "BYE", false);
                 break;

            case 0xae: // EXIT
                 c+=byte_to_keyword(c, "EXIT", false);
                 break;

            case 0xaf: // CONT - not MemberCHIP
                 c+=byte_to_keyword(c, "CONT", false);
                 break;

            case 0xb0: // AUTO - not MemberCHIP
                 c+=byte_to_keyword(c, "AUTO", false);
                 break;

            case 0xb1: // RENUMBER
                 c+=byte_to_keyword(c, "RENUMBER ", false);
                 break;

            case 0xb2: // EDIT
                 c+=byte_to_keyword(c, "EDIT", false);
                 break;

            case 0xb3: // FORMAT
                 c+=byte_to_keyword(c, "FORMAT ", false);
                 break;

            case 0xb4: // USING - not MemberCHIP
                 c+=byte_to_keyword(c, "USING", false);
                 break;

            case 0xb5: // AND
                 c+=byte_to_keyword(c, "AND ", true);
                 break;

            case 0xb6: // XOR
                 c+=byte_to_keyword(c, "XOR ", true);
                 break;

            case 0xb7: // OR
                 c+=byte_to_keyword(c, "OR ", true);
                 break;

            case 0xb8: // NOT logical - not MemberCHIP
                 c+=byte_to_keyword(c, "NOT ", true);
                 break;

            case 0xb9: // CHR$
                 c+=byte_to_keyword(c, "CHR$", false);
                 break;

            case 0xba: // MID$
                 c+=byte_to_keyword(c, "MID$", false);
                 break;

            case 0xbb: // power
                 *c = '^';
                 break;

            case 0xbc: // TAB
                 c+=byte_to_keyword(c, "TAB", false);
                 break;

            case 0xbd: // greater than or equals
                 c+=byte_to_keyword(c, ">=", false);
                 break;

            case 0xbe: // less than or equals
                 c+=byte_to_keyword(c, "<=", false);
                 break;

            case 0xbf: // not equals
                 c+=byte_to_keyword(c, "<>", false);
                 break;

            case 0xc0: // STEP
                 c+=byte_to_keyword(c, " STEP ", false);
                 break;

            case 0xc1: // TO
                 c+=byte_to_keyword(c, " TO ", false);
                 break;

            case 0xc2: // comma
                 *c = ',';
                 break;

            case 0xc3: // continuation
                 *c = ';';
                 break;

            case 0xc4: // close paren
                 *c = ')';
                 break;

            case 0xc5: // THEN
                 c+=byte_to_keyword(c, "THEN ", true);
                 break;

            case 0xc6: // less than
                 *c = '<';
                 break;

            case 0xc7: // greater than
                 *c = '>';
                 break;

            case 0xc8: // plus
                 *c = '+';
                 break;

            case 0xc9: // minus
                 *c = '-';
                 break;

            case 0xca: // multiply
                 *c = '*';
                 break;

            case 0xcb: // divide
                 *c = '/';
                 break;

            case 0xcc: // assignment
                 *c = '=';
                 break;

            case 0xcd: // statement separator
                 *c = ':';
                 break;

            case 0xce: // end quote
            case 0xcf: // start quote
                 *c = '"';
                 break;

            case 0xd0: // @
                 *c = '@';
                 break;

            case 0xd1: // "it's a variable"
                 continue; // we don't map or save this, just skip the byte

            case 0xd2: // "it's a 2-byte int"
                 sprintf(formatted_val, "%d", get_int_value(f, &byte_cnt));
                 c+=byte_to_keyword(c, formatted_val, false);
                 break;

            case 0xd3: // "it's a float"
                 sprintf(formatted_val, "%f", get_float_value(f, &byte_cnt));
                 c+=byte_to_keyword(c, trim_float_str(formatted_val), false);
                 break;

            case 0xd4: // SIN
                 c+=byte_to_keyword(c, "SIN", false);
                 break;

            case 0xd5: // COS
                 c+=byte_to_keyword(c, "COS", false);
                 break;

            case 0xd6: // open paren
                 *c = '(';
                 break;

            case 0xd7: // "string var" 
                       // this byte preceeds the variable name letter
                       // after which the $ must be added
                 *c = get_next_byte(f);
                 byte_cnt++;
                 c++;
                 *c = '$';
                 break;

            case 0xd8: // ATN
                 c+=byte_to_keyword(c, "ATN", false);
                 break;

            case 0xd9: // EXP
                 c+=byte_to_keyword(c, "EXP", false);
                 break;

            case 0xda: // LOG
                 c+=byte_to_keyword(c, "LOG", false);
                 break;

            case 0xdb: // SQR
                 c+=byte_to_keyword(c, "SQR", false);
                 break;

            case 0xdc: // INT
                 c+=byte_to_keyword(c, "INT", false);
                 break;

            case 0xdd: // PEEK
                 c+=byte_to_keyword(c, "PEEK", false);
                 break;

            case 0xde: // ABS
                 c+=byte_to_keyword(c, "ABS", false);
                 break;

            case 0xdf: // RND
                 c+=byte_to_keyword(c, "RND", false);
                 break;

            case 0xe0: // USR
                 c+=byte_to_keyword(c, "USR", false);
                 break;

            case 0xe1: // INUM
                 c+=byte_to_keyword(c, "INUM", false);
                 break;

            case 0xe2: // #
                 *c = '#';
                 break;

            case 0xe3: // FNUM
                 c+=byte_to_keyword(c, "FNUM", false);
                 break;

            case 0xe4: // ASC
                 c+=byte_to_keyword(c, "ASC", false);
                 break;

            case 0xe5: // LEN
                 c+=byte_to_keyword(c, "LEN", false);
                 break;

            case 0xe6: // VAL - not MemberCHIP
                 c+=byte_to_keyword(c, "VAL", false);
                 break;

            case 0xe7: // SGN
                 c+=byte_to_keyword(c, "SGN", false);
                 break;

            case 0xe8: // MOD
                 c+=byte_to_keyword(c, "MOD", false);
                 break;

            case 0xe9: // INP
                 c+=byte_to_keyword(c, "INP", false);
                 break;

            case 0xea: // NOT (bitwise)
                 c+=byte_to_keyword(c, "NOT ", false);
                 break;

            case 0xeb: // PI
                 c+=byte_to_keyword(c, "PI", false);
                 break;

            case 0xec: // FVAL
                 c+=byte_to_keyword(c, "FVAL", false);
                 break;

            case 0xed: // STR$ - not MemberCHIP
                 c+=byte_to_keyword(c, "STR$", false);
                 break;

            case 0xee: // MEM
                 c+=byte_to_keyword(c, "MEM", false);
                 break;

            case 0xef: // QST
                 c+=byte_to_keyword(c, "QST", false);
                 break;

            case 0xf0: // EF  
                 c+=byte_to_keyword(c, "EF", false);
                 break;

            default:
                if (*c < ' ' || *c > '~') {
                    printf("%02x(%c) ", *c, *c);
                    *c = '?';
                    unknown = true;;
                }
        }
        c++;
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
