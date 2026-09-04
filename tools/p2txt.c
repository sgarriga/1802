#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

void getHDR(FILE *f) {
    int n;

    // 14 nybble header
    for (int i=0; i<(14*2); i++)
        n = fgetc(f);
}

int get_int(FILE *f, int *byte_cnt) {
    char read_buffer[16];
    int num = 0;

    // int is 4-bytes (8 nybbles)
    if (fread(read_buffer, 8, 1, f)) {
        num = ((read_buffer[0]-64) << 28) + 
              ((read_buffer[1]-64) << 24) + 
              ((read_buffer[2]-64) << 20) + 
              ((read_buffer[3]-64) << 16) +
              ((read_buffer[4]-64) << 12) + 
              ((read_buffer[5]-64) << 8) + 
              ((read_buffer[6]-64) << 4) + 
              (read_buffer[7]-64);
        // assuming msb .. lsb for now
        *byte_cnt+=4;
    }

    return num;
}

float mbf_to_float(unsigned char *mbf) {
    // 1. A zero exponent represents zero
    if (mbf[0] == 0) {
        return 0.0f;
    }

    // 2. Extract Unbiased Exponent (Bias = 128)
    int exponent = (int)mbf[0] - 128;

    // 3. Extract Sign (MSB of byte 1)
    int sign = (mbf[1] & 0x80) ? -1 : 1;

    // 4. Reconstruct Mantissa bits
    // MBF implicit leading bit is 1 at position 23 (0.1_2 base)
    unsigned int mantissa_bits = (1U << 23) 
                           | ((unsigned int)(mbf[1] & 0x7F) << 16) 
                           | ((unsigned int)mbf[2] << 8) 
                           | (unsigned int)mbf[3];

    // 5. Convert mantissa integer bits to fractional value (divide by 2^24)
    double mantissa = (double)mantissa_bits / (1U << 24);

    // 6. Calculate Final Value: Sign * Mantissa * 2^Exponent
    // (ldexp scales a float by powers of 2 efficiently)
    return (float)(sign * mantissa * ldexp(1.0, exponent));
}

float get_flt(FILE *f, int *byte_cnt) {
    char read_buffer[16];
    unsigned char mbf[4];
    float num = 0.0;
    char exp = 0;
    unsigned int man = 0;

    // float is 4-bytes (8 nybbles)
    // assuming 8-bit signed exponent & 24-bit mantissa
    if (fread(read_buffer, 8, 1, f)) {

        printf("%8.8s\n", read_buffer);
        for (int i=0, j=0; i < 8; i+=2,j++) {
           mbf[j] = ((read_buffer[i]-64) << 4) + (read_buffer[i+1]-64);
        }
        num = mbf_to_float(mbf); 
        *byte_cnt+=4;
    }

    return num;
}

char *flt_trim(char *in) {
    static char out[8] = ".0";
    int l;

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

#define detok(a, b) strcpy(a, b); a+=strlen(b) - 1;

char *getLine(FILE *f) {
    static char line_buffer[256];
    char read_buffer[8];
    char temp_buffer[8];
    char *c = NULL;
    unsigned short line_num = 0;
    unsigned short line_len = 0;
    int byte_cnt = 0;
    int unknown = 0;
    int done = 0;
    int is_string = 0;

    memset(line_buffer, 0, sizeof(line_buffer));

    // each character in file is a nybble
    // 1st 4 nybbles are line number, FFFF may indicate end of listing (unsure)
    if (!fread(read_buffer, 4, 1, f))
        return NULL;
    line_num = ((read_buffer[0]-64) << 12)  + ((read_buffer[1]-64) << 8) + ((read_buffer[2]-64) << 4) + (read_buffer[3]-64);

    sprintf(line_buffer, "%hu ", line_num);
    c = line_buffer + strlen(line_buffer);

    while (fread(read_buffer, 2, 1, f)) {
        byte_cnt++;

        if (byte_cnt == 1) {
            line_len = ((read_buffer[0]-64) << 4) + (read_buffer[1]-64);
#if DEBUG
            printf("\nRead byte %d as length %d\n", byte_cnt, line_len);
#endif
            continue; // 1st byte after line # is line length in bytes, including self
        }

        *c = ((read_buffer[0]-64) << 4) + (read_buffer[1]-64);
#if DEBUG
        printf("\nRead byte %d as source %02x\n", byte_cnt, *c);
#endif

        switch(*c) {
            case 0x80: // REM
                 detok(c, "REM");
                 break;

            case 0x81: // CLS
                 detok(c, "CLS");
                 break;

            case 0x84: // END
                 detok(c, "END");
                 if (line_num == 0xffff)
                     done = 1;
                 break;

            case 0x86: // PRINT
                 if (*(c-1) != ' ') {
                     *c = ' ';
                     c++;
                 }
                 detok(c, "PRINT ");
                 break;

            case 0x87: // GOTO
                 if (*(c-1) != ' ') {
                     *c = ' ';
                     c++;
                 }
                 detok(c, "GOTO ");
                 break;

            case 0x88: // IF 
                 detok(c, "IF ");
                 break;

            case 0x89: // INPUT
                 detok(c, "INPUT ");
                 break;

            case 0x8b: // GOSUB
                 detok(c, "GOSUB ");
                 break;

            case 0x8c: // RETURN
                 detok(c, "RETURN");
                 break;

            case 0x8e: // DIM
                 detok(c, "DIM ");
                 break;

            case 0x8f: // FOR
                 detok(c, "FOR ");
                 break;

            case 0x90: // NEXT
                 detok(c, "NEXT ");
                 break;

            case 0x91: // FIXED
                 detok(c, "FIXED ");
                 break;

            case 0x96: // DEFINT
                 detok(c, "DEFINT ");
                 break;

            case 0x9b: // DATA
                 detok(c, "DATA ");
                 break;

            case 0x9c: // READ
                 detok(c, "READ");
                 break;

            case 0x9d: // RESTORE
                 detok(c, "RESTORE");
                 break;

            case 0x9f: // CLD
                 detok(c, "CLD");
                 break;

            case 0xae: // EXIT
                 detok(c, "EXIT ");
                 break;

            case 0xba: // MID$
                 detok(c, "MID$");
                 break;

            case 0xbb: // power
                 *c = '^';
                 break;

            case 0xbc: // TAB
                 detok(c, "TAB");
                 break;

            case 0xbd: // >=
                 detok(c, ">=");
                 break;

            case 0xbe: // <=
                 detok(c, "<=");
                 break;

            case 0xbf: // <>
                 detok(c, "<>");
                 break;

            case 0xc0: // STEP
                 detok(c, " STEP ");
                 break;

            case 0xc1: // TO
                 detok(c, " TO ");
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
                 detok(c, "THEN ");
                 break;

            case 0xc6: // <
                 *c = '<';
                 break;

            case 0xc7: // >
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
                 detok(c, "");
                 break;

            case 0xd2: // "it's an int"
                 sprintf(temp_buffer, "%d", get_int(f, &byte_cnt));
                 detok(c, temp_buffer);
                 break;

            case 0xd3: // "it's a float"
                 sprintf(temp_buffer, "%f", get_flt(f, &byte_cnt));
                 detok(c, flt_trim(temp_buffer));
                 break;

            case 0xd4: // SIN
                 detok(c, "SIN");
                 break;

            case 0xd6: // open paren
                 *c = '(';
                 break;

            case 0xd7: // "string var"
                 is_string = 1;
                 break;

            case 0xda: // LOG
                 detok(c, "LOG");
                 break;

            case 0xdb: // SQR
                 detok(c, "SQR");
                 break;

            case 0xdc: // INT
                 detok(c, "INT");
                 break;

            case 0xde: // ABS
                 detok(c, "ABS");
                 break;

            case 0xdf: // RND
                 detok(c, "RND");
                 break;

            case 0xe3: // FNUM
                 detok(c, "FNUM");
                 break;

            case 0xe5: // LEN
                 detok(c, "LEN");
                 break;

            case 0xe8: // MOD
                 detok(c, "MOD");
                 break;

            case 0xeb: // PI
                 detok(c, "PI");
                 break;

            case 0xec: // FVAL
                 detok(c, "FVAL");
                 break;

        }

        if (*c == 0x0d) {
            *c = '\0';
            break;
        }

        if (is_string && *c == 0xd7)
            continue; // we're not going to advance c - we need to find the variable name then append $

        if (*c < ' ' || *c > '~') {
            printf("%02x(%c) ", *c, *c);
            *c = '?';
            unknown++;
        }

        c++;
        
        if (is_string) {
            *c = '$';
            c++;
            is_string = 0;
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
        exit(0);
    }
    return c;
}

int main(int argc, char *argv[])
{
    FILE *f;

    f = fopen(argv[1], "r");
    getHDR(f);
    while (getLine(f) != NULL);
    fclose(f);
}
