# 1802/tools

## renum - tool to renumber BASIC listings

Usage: ./renum inputfile [outputfile]
       ./renum [options] inputfile

Options:
  -s, --start <number>     Starting line number (default: 100)
  -i, --step <number>      Step between line numbers (default: 10)
  -b, --boundary <number>  Round line numbers to next multiple of this (default: 1000)
  -o, --output <file>      Output file (default: stdout)
  --gosub                  Round GOSUB targets
  --gap                    Round large gaps
Defaults: start=100, step=10, boundary=1000, output=stdout

## mchip - wrapper script around minicom that tries to figure out the TTY

reval - validate and rename BASIC variables

Usage: ./reval [options] inputfile

Options:
  -o, --output <file>      Output file (default: stdout)

Numeric variables must be one letter or one letter followed by one digit.
String variables must be one letter followed by `$`.
Array variables must be one letter.

## p2txt - convert psave/pload format file to listing text

Usage: ./p2txt inputfile 

Output goes to stdout - redirect if you want to save it
WORK IN PROGRESS
