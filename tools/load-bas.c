#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <stdbool.h>

#include <fcntl.h> // O_RDWR
#include <errno.h>
#include <termios.h> // POSIX terminal control definitions
#include <unistd.h>

// ASCII codes we tend to see
#define LF 0x0A
#define FF 0x0C
#define CR 0x0D


// default TTY
const char s0[] = "/dev/serial0";

// globals - defaults may be changed in parse_args()
static int ms_chr_sleep = 10; // per Lee Hart
static char *dev = (char *) s0;
static int ms_eol_sleep = 250; // per Lee Hart
static bool verbose = false;
static bool warm = false;
static FILE *bas_file = NULL;
static int serial_port = -1;

static void usage(char *argv0) {
	printf("Usage:\n%s [opts] filename\n", argv0);
	printf("Options:\n");
	printf("-?\t\tshow usage\n");
	printf("-c interval\tset inter character delay (ms)\n");
	printf("\t\tdefault is %d\n", ms_chr_sleep);
	printf("-d device\tspecify device (if not %s)\n", s0);
	printf("-l interval\tset end of line delay (ms)\n");
	printf("\t\tdefault is %d\n", ms_eol_sleep);
	printf("-v\t\tverbose output\n");
	printf("-w\t\twarm start BASIC\n");
	exit(0);
}

static void my_exit(int rc) {

	if (serial_port != -1)
		close(serial_port);
	if (bas_file != NULL)
		fclose(bas_file);
	exit(rc);
}

// handle command line overrides of any defaults and assign the input file
static void parse_args(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if      (!strcmp(argv[i], "-?")) {
				usage(argv[0]);
			}
			else if (!strcmp(argv[i], "-c"))
				ms_chr_sleep = atol(argv[++i]);
			else if (!strcmp(argv[i], "-d"))
				dev = argv[++i];
			else if (!strcmp(argv[i], "-l"))
				ms_eol_sleep = atol(argv[++i]);
			else if (!strcmp(argv[i], "-v"))
				verbose = true;
			else if (!strcmp(argv[i], "-w"))
				warm = true;

			continue;
		}
		else
			bas_file = fopen(argv[i], "r");

	}

	if (bas_file == NULL) {
		int cc = errno;
		printf("Error %d from fopen: %s\n", cc, strerror(cc));
		my_exit(cc);
	}
}


// configure the serial commection to the MemberCHIP
static void setup_tty() {
	// open the serial port.
	serial_port = open(dev, O_RDWR);

	if (serial_port < 0) {
		int cc = errno;
		printf("Error %d from open: %s\n", cc, strerror(cc));
		my_exit(cc);
	}

	// create new termios struct, 'tty' by convention
	struct termios tty;

	// read in existing settings, and handle any error
	if (tcgetattr(serial_port, &tty) != 0) {
		int cc = errno;
		printf("Error %d from tcgetattr: %s\n", cc, strerror(cc));
		my_exit(cc);
	}

	tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
	tty.c_cflag |= CS8; // 8 bits per byte (most common)
	tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO; // Disable echo
	tty.c_lflag &= ~ECHOE; // Disable erasure
	tty.c_lflag &= ~ECHONL; // Disable new-line echo
	tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

	tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

	tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
	tty.c_cc[VMIN] = 0;

	// set in/out baud rate to be 4800
	cfsetispeed(&tty, B4800);
	cfsetospeed(&tty, B4800);

	// save tty settings, also checking for error
	if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
		int cc = errno;
		printf("Error %d from tcsetattr: %s\n", cc, strerror(cc));
		my_exit(cc);
	}
}

// read a byte at a time from the serial connection into a static buffer,
// the address of which is returned.
static char *accumulate() {
	static char chip_response_buff[512];
	static bool first_byte = true;
	bool busy = true;
	char *r = chip_response_buff;

	while (busy) { 
		*r = '\0';
		switch (read(serial_port, r, 1)) {
			case -1: // error case
				printf("Error reading: %s", strerror(errno));
				busy = false;
				break;

			case 0: // no data
				busy = false;
				break;

			case 1: // got a byte
				if (*r > 127) { // skip extended ASCII
					if (verbose && !first_byte) {
						printf("MemberCHIP may need reset! [got ASCII %02x]\n", *r);
					}
					first_byte = false;
					continue;
				}
				first_byte = false;
				switch(*r) {
					case CR:
						busy = false;
						// fall through
					case LF:
					case FF:
						*r = '\0';
						break;
					default:
						r++;
				}
				break;
		}
	}
	if (verbose) {
		printf("Read %d bytes. Got message: ", strlen(chip_response_buff));
		for (int i = 0; i < strlen(chip_response_buff); i++)
			printf(isprint(chip_response_buff[i]) ? "%c" : "[%02x]", chip_response_buff[i]);
		printf("\n");
	}
	return chip_response_buff;
}

static int slow_write(char *tx) {

	int bytes_to_write = strlen(tx);
	if (verbose)
		printf("Sending %d byte%s\n", bytes_to_write, (bytes_to_write > 1) ? "s" : "");
	for (int i = 0; i < bytes_to_write; i++) {
		write(serial_port, &tx[i], 1);
		usleep(1000 * ms_chr_sleep);
		if (tx[i] == CR)
			usleep(1000 *(ms_eol_sleep - ms_chr_sleep));
	}
	return 0;
}

// send a message out and try to get a response
// BASIC source takes a while to process :( - Lee Hart suggests 3 seconds!
static char *dialog(char *tx, bool retry, bool basic) {
	slow_write(tx);

	if (basic)
		sleep(3); // sleep 3 seconds

	// read the reply
	char *response = NULL;
	while (true) {
		response = accumulate();
		if (!retry || *response != '\0')
			break;

		retry = false;;
		usleep(500000); // sleep 0.5 seconds
	};

	// skip leading <CR> (if any)
	if (*response == CR)
		response++;

	return response;
}

static char *exit_basic() {
	char *response = NULL;

	// Write BYE<CR> to serial port - exit basic if we're still there
	response = dialog("BYE\r", true, false);

	if (strstr(response, "PRESS <CR> TO CONTINUE") != NULL) {
		// Write <CR> to serial port - activate monitor
		response = dialog("\r", true, false);
	}

	printf("exit_basic() returns %s\n", response);
}

static void expect(char *target, char *got, bool exact)
{
	bool done = false;
	if (exact) {
		done = strcmp(target, got);
	}
	else {
		done = (strstr(got, target) == NULL);
	}
	if (done) {
		if (exact)
			printf("Expected '%s', but got '%s'!\n", target, got);
		else
			printf("'%s', not found in '%s'!\n", target, got);
		my_exit(-1);
	}
}

static char *find_monitor_prompt() {
	char *response = NULL;

	// write <CR> to serial port - start monitor
	response = dialog("\r", false, false);

	// try another <CR> if we didn't get the monitor banner
	if (strstr(response, "Monitor") == NULL) {
		response = dialog("\r", false, false);
	}

	// if we got a BASIC pronmpt, or something unexpected, assume
	// were in BASIC and try quitting that
	if (*response == ':' || strstr(response, "Monitor") == NULL) {
		response = exit_basic();
	}

	response=accumulate(); // get the monitor '>' prompt
	expect(">", response, true); // may exit

	return response;
}

int main(int argc, char *argv[]) {

	char line_of_code[1024];
	char *response = NULL;
	char *p = NULL;

	if (argc == 1)
		usage(argv[0]); // exits!
	else
		parse_args(argc, argv);

	setup_tty();

	printf("BASIC code loader for MemberChip\n\n");
	printf("Establishing Monitor session...\n");
	response = find_monitor_prompt();

	// Write B to serial port - select Basic
	printf("Entering BASIC3 environment...\n");
	response = dialog("B", true, false);

	// skip BASIC3 banner lines looking for Cold/Warm prompt
	for (int i = 0; i < 5 && strstr(response, "C = Cold") == NULL; i++) 
		response=accumulate();
	expect("C = Cold", response, false); // may exit

	// Write C or W to serial port to select Cold/Warm start
	// and get the BASIC 'READY' and prompt
	response = dialog(warm ? "W" : "C" , false, true);

	// Now get the BASIC ':' prompt
	response=accumulate(); // get the BASIC header and prompt
	expect(":", response, true); // may exit

	printf("Loading BASIC code...\n");

	int source_read = 0;
	int source_good = 0;
	while (fgets(line_of_code, sizeof(line_of_code), bas_file) != NULL) {
		source_read++;
		printf("\rRead line %d        ", source_read);
		fflush(stdout);

		// convert source end-of-line to <CR> for send
		char *p = strrchr(line_of_code, '\n');
		if (p != NULL)
			*p = CR;

		// send it - note certain BASIC lines need more time so
		// we use a retry
		response = dialog(line_of_code, false, true);
		if (*response == '\0') {
			printf("\n");
			printf("Sync issue\n");
			break;
		}

		// now strip end-of-line from source to compare to echo
		*p = '\0';

		// compare echo-back - a mismatch means an error we want to
		// report
		if (strstr(response, line_of_code) == NULL) {
			printf("\n");
			printf("%s\n", line_of_code);
			printf("%s\n", response);
			while (strstr(response, "READY") == NULL)  {
				response = accumulate(); // get the BASIC READY
				printf("%s\n", response);
			}
			usleep(250000); // sleep 0.25 second
		}
		else
			source_good++;
	}
	printf("\n");

	// Write BYE<CR> to serial port - exit basic if we're still there
	response = dialog("BYE\r", false, true);

	close(serial_port);
	fclose(bas_file);

	printf("Status:\n");
	printf("%5d Code Lines Read.\n", source_read);
	printf("%5d Lines Loaded OK.\n", source_good);
	exit(source_read - source_good);
}
