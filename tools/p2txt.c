#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#define HEADER_BYTE_CNT 14
#define NUMBER_BYTE_CNT 4

#define LINE_NUM_NYBBLES 4

static uint8_t read_buffer[16];
static bool show_header = false;

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
#define byte_to_keyword(a, b) strcpy((char *)a, b); a+=strlen(b) - 1;

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
#if DEBUG
            printf("\nRead byte %d as length %d\n", byte_cnt, line_len);
#endif
            continue; // 1st byte after line # is line length in bytes, including self
        }

#if DEBUG
        printf("\nRead byte %d as source %02x\n", byte_cnt, *c);
#endif

        // 0x0d marks end of line
        if (*c == 0x0d) {
            *c = '\0';
            break;
        }

        // map the internal byte token to a keyword or printable character
        // sometimes the byte has side effects
        switch(*c) {
            case 0x80: // REM
                 byte_to_keyword(c, "REM");
                 break;

            case 0x81: // CLS
                 byte_to_keyword(c, "CLS");
                 break;

            case 0x84: // END
                 byte_to_keyword(c, "END");
                 if (line_num == 0xffff)
                     done = true; // "63335 END" is saved if there isn't an
                                  // explicit last line with "END"!
                 break;

            case 0x86: // PRINT
                 if (*(c-1) != ' ') { // PRINT and GOTO have a formatting quirk
                     *c = ' ';
                     c++;
                 }
                 byte_to_keyword(c, "PRINT ");
                 break;

            case 0x87: // GOTO
                 if (*(c-1) != ' ') { // PRINT and GOTO have a formatting quirk
                     *c = ' ';
                     c++;
                 }
                 byte_to_keyword(c, "GOTO ");
                 break;

            case 0x88: // IF 
                 byte_to_keyword(c, "IF ");
                 break;

            case 0x89: // INPUT
                 byte_to_keyword(c, "INPUT ");
                 break;

            case 0x8b: // GOSUB
                 byte_to_keyword(c, "GOSUB ");
                 break;

            case 0x8c: // RETURN
                 byte_to_keyword(c, "RETURN");
                 break;

            case 0x8e: // DIM
                 byte_to_keyword(c, "DIM ");
                 break;

            case 0x8f: // FOR
                 byte_to_keyword(c, "FOR ");
                 break;

            case 0x90: // NEXT
                 byte_to_keyword(c, "NEXT ");
                 break;

            case 0x91: // FIXED
                 byte_to_keyword(c, "FIXED ");
                 break;

            case 0x96: // DEFINT
                 byte_to_keyword(c, "DEFINT ");
                 break;

            case 0x9b: // DATA
                 byte_to_keyword(c, "DATA ");
                 break;

            case 0x9c: // READ
                 byte_to_keyword(c, "READ");
                 break;

            case 0x9d: // RESTORE
                 byte_to_keyword(c, "RESTORE");
                 break;

            case 0x9f: // CLD
                 byte_to_keyword(c, "CLD");
                 break;

            case 0xae: // EXIT
                 byte_to_keyword(c, "EXIT ");
                 break;

            case 0xba: // MID$
                 byte_to_keyword(c, "MID$");
                 break;

            case 0xbb: // power
                 *c = '^';
                 break;

            case 0xbc: // TAB
                 byte_to_keyword(c, "TAB");
                 break;

            case 0xbd: // greater than or equals
                 byte_to_keyword(c, ">=");
                 break;

            case 0xbe: // less than or equals
                 byte_to_keyword(c, "<=");
                 break;

            case 0xbf: // not equals
                 byte_to_keyword(c, "<>");
                 break;

            case 0xc0: // STEP
                 byte_to_keyword(c, " STEP ");
                 break;

            case 0xc1: // TO
                 byte_to_keyword(c, " TO ");
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
                 byte_to_keyword(c, "THEN ");
                 break;

            case 0xc6: // lass than
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

            case 0xd1: // "it's a variable"
                 byte_to_keyword(c, "");
                 break;

            case 0xd2: // "it's an int"
                 sprintf(formatted_val, "%d", get_int_value(f, &byte_cnt));
                 byte_to_keyword(c, formatted_val);
                 break;

            case 0xd3: // "it's a float"
                 sprintf(formatted_val, "%f", get_float_value(f, &byte_cnt));
                 byte_to_keyword(c, trim_float_str(formatted_val));
                 break;

            case 0xd4: // SIN
                 byte_to_keyword(c, "SIN");
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

            case 0xda: // LOG
                 byte_to_keyword(c, "LOG");
                 break;

            case 0xdb: // SQR
                 byte_to_keyword(c, "SQR");
                 break;

            case 0xdc: // INT
                 byte_to_keyword(c, "INT");
                 break;

            case 0xde: // ABS
                 byte_to_keyword(c, "ABS");
                 break;

            case 0xdf: // RND
                 byte_to_keyword(c, "RND");
                 break;

            case 0xe3: // FNUM
                 byte_to_keyword(c, "FNUM");
                 break;

            case 0xe5: // LEN
                 byte_to_keyword(c, "LEN");
                 break;

            case 0xe8: // MOD
                 byte_to_keyword(c, "MOD");
                 break;

            case 0xeb: // PI
                 byte_to_keyword(c, "PI");
                 break;

            case 0xec: // FVAL
                 byte_to_keyword(c, "FVAL");
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
