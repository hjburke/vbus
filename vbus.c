#include <stdio.h>                /* perror() */
#include <stdlib.h>               /* atoi() */
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>               /* read() */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <string.h>
#include "ini.h"

#define INI_FILE    "/usr/local/etc/vbus.ini"
#define PVERSION 4

int debug = 0;

typedef struct {
    const char* ip;
    int port;
} configuration;

configuration config;

// Configuration defaults
static char* defaultIP="192.168.254.168";



typedef struct 
{
  char  sampleTimestamp[100];
  float tempCollectorC;
  float tempCollectorF;
  float tempTank1C;
  float tempTank1F;
  float tempTank2C;
  float tempTank2F;
  float tempPoolC;
  float tempPoolF;
  int   pump;
  int   valveTank1;
  int   valveTank2;
  int   valvePool;
} BXPlus;

BXPlus stats = {};

void writeDataJSON() {
  FILE *f;

  f = fopen("/dev/shm/solar.txt", "w");
  if (f == NULL) {
    printf("ERROR: Failed to open file\n");
  } else {
    fprintf(f, "{\n");
    fprintf(f, "  \"sampleTime\": \"%s\",\n", stats.sampleTimestamp);
    fprintf(f, "  \"tempCollector\": %.1f,\n", stats.tempCollectorF);
    fprintf(f, "  \"tempTank1\": %.1f,\n", stats.tempTank1F);
    fprintf(f, "  \"tempTank2\": %.1f,\n", stats.tempTank2F);
    fprintf(f, "  \"tempPool\": %.1f,\n", stats.tempPoolF);
    fprintf(f, "  \"speedPump\": %d,\n", stats.pump);
    fprintf(f, "  \"valveTank1\": %d,\n", stats.valveTank1);
    fprintf(f, "  \"valveTank2\": %d,\n", stats.valveTank2);
    fprintf(f, "  \"valvePool\": %d\n", stats.valvePool);
    fprintf(f, "}\n");

    fclose(f);
  }
}

unsigned char VBus_CalcCrc(const unsigned char *Buffer, int Offset, int Length)
{
  unsigned char Crc;
  int i;
  Crc = 0x7F;
  for (i = 0; i < Length; i++) {
    Crc = (Crc - Buffer [Offset + i]) & 0x7F;
  }
  return Crc;
}

void VBus_ExtractSeptett(unsigned char *Buffer, int Offset, int Length)
{
  unsigned char Septett;
  int i;
  Septett = 0;
  for (i = 0; i < Length; i++) {
    if (Buffer [Offset + i] & 0x80) {
      Buffer [Offset + i] &= 0x7F;
      Septett |= (1 << i);
    }
  }
  Buffer [Offset + Length] = Septett;
}

void VBus_InjectSeptett(unsigned char *Buffer, int Offset, int Length)
{
  unsigned char Septett;
  int i;
  Septett = Buffer [Offset + Length];
  for (i = 0; i < Length; i++) {
    if (Septett & (1 << i)) {
      Buffer [Offset + i] |= 0x80;
    }
  }
}

void decode_temp(unsigned char *s, float *tc, float *tf) {
  *tc = (float)(s[0]+(s[1]*256)) / 10;
  *tf = (*tc * 9/5) +32;
  if (debug) printf("Temp %.1f(C) %.1f(F)\n", *tc, *tf);
}

void decode_packet(unsigned char *s, int num_bytes)
{
  unsigned int protocol = 0;
  unsigned int dest = 0;
  unsigned int src = 0;
  unsigned int command = 0;
  unsigned int frames = 0;
  unsigned int checksum = 0;
  unsigned int calcchecksum = 0;

  if (s[PVERSION] == 16) {
    protocol=1;
  } else if (s[PVERSION] == 32) {
    protocol=2;
  } else if (s[PVERSION] == 48) {
    protocol=3;
  }

  if (debug) printf("V%d ",protocol);

  if (protocol == 1) {
    checksum = s[8];
    calcchecksum = VBus_CalcCrc(s,0,8);

    if (checksum != calcchecksum) {
      printf("CHECKSUM ERROR\n");
      return;
    }

    dest = s[0]+(s[1]*256);
    src = s[2]+(s[3]*256);
    command = s[5]+(s[6]*256);
    frames = s[7];

    if (debug) {
      printf("DEST %04x ", dest);
      printf("SRC %04x ", src);
      printf("COMMAND %04x ", command);
      printf("FRAMES %02x ", frames);
      printf("\n");
    }

    s += 9;
    num_bytes -= 9;
  }

  if (protocol==1 && dest==0x0010 && src==0x7112) {
    int i;
    for (i=1; i <= frames; i++) {

      checksum = s[5];
      calcchecksum = VBus_CalcCrc(s,0,5);

      if (checksum != calcchecksum) {
        printf("CHECKSUM ERROR\n");
        return;
      }

      VBus_InjectSeptett(s, 0, 4);

      if (i==1) {
        decode_temp(s,&stats.tempCollectorC, &stats.tempCollectorF);
        decode_temp(s+2,&stats.tempTank1C, &stats.tempTank1F);
      } else if (i==2) {
        //decode_temp(s,3);
        decode_temp(s+2,&stats.tempTank2C, &stats.tempTank2F);
      } else if (i==3) {
        decode_temp(s,&stats.tempPoolC, &stats.tempPoolF);
        //decode_temp(s+2,6);
      } else if (i==4) {
        //decode_temp(s,7);
        //decode_temp(s+2,8);
      } else if (i==5) {
        //decode_temp(s,9);
        //decode_temp(s+2,10);
      } else if (i==6) {
        //decode_temp(s,11);
        //decode_temp(s+2,12);
      } else if (i==11) {
        stats.pump = s[0];
        stats.valveTank1 = s[1];
        stats.valveTank2 = s[2];
        stats.valvePool = s[3];
      } else {
        if (debug) {
          printf("Frame %2d Byte 1 - %02x %3d\n", i,s[0],s[0]);
          printf("Frame %2d Byte 2 - %02x %3d\n", i,s[1],s[1]);
          printf("Frame %2d Byte 3 - %02x %3d\n", i,s[2],s[2]);
          printf("Frame %2d Byte 4 - %02x %3d\n", i,s[3],s[3]);
        }
      }

      s += 6;
      num_bytes -= 6;
    }

    time_t thetime=time(NULL);
    strftime(stats.sampleTimestamp, sizeof(stats.sampleTimestamp), "%Y-%m-%d %H:%M:%S", localtime(&thetime));

    writeDataJSON();
  }

  if (debug) {
    while(num_bytes-- > 0) {
      printf("%02x ", (unsigned int) *s++);
    }
    printf("\n");
  }
}

void handle_data(int clientSocket)
{
  unsigned char buffer[1024];
  unsigned char *bptr = buffer;
  int status;
  int nbread = 0;

  while (0 < (status = read(clientSocket, bptr, 1))) {
    if (*bptr == 170) {
      if (nbread > 0) {
        if (debug) printf("SYNC ");
        decode_packet(buffer,nbread);
      }
      bptr=buffer;
      nbread=0;
    } else {
      bptr++;
      nbread++;
    }

  }
}

// Handle callback from the ini file parser
static int
handler(void *user, const char *section, const char *name, const char *value)
{
#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

	configuration* pconfig = (configuration*)user;

	if (MATCH("vbus", "port")) {
		pconfig->port = atoi(value);
	} else
	if (MATCH("vbus", "ip")) {
		pconfig->ip = strdup(value);
	}
	else {
		return 0;  /* unknown section/name, error */
    }
	return 1;
}


int main(int argc, char *argv[])
{
/*  union {
    unsigned char *buffer;
    struct {
       char sync;
       int  dest;
       int  src;
       char proto;
    } pkt;
  } vpk;*/

    int clientSocket,
        remotePort,
        status = 0;
    struct hostent *hostPtr = NULL;
    struct sockaddr_in serverName = { 0 };
    unsigned char buffer[1024] = "";
    char *remoteHost = NULL;

    config.ip=defaultIP;
    config.port=7053;

/*    if (3 != argc)
    {
        fprintf(stderr, "Usage: %s <serverHost> <serverPort>\n", argv[0]);
        exit(1);
    }

    remoteHost = argv[1];
    remotePort = atoi(argv[2]);*/

    remoteHost = (char *)config.ip;
    remotePort = config.port;

	// Read in the (optional) ini file
    if (ini_parse(INI_FILE, handler, &config) < 0) {
		fprintf(stderr, "WARNING: Failed to open config file %s, continuing with defaults.\n", INI_FILE);
    }

    clientSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (-1 == clientSocket)
    {
        perror("socket()");
        exit(1);
    }

    /*
     * need to resolve the remote server name or
     * IP address */
    hostPtr = gethostbyname(remoteHost);
    if (NULL == hostPtr)
    {
        hostPtr = gethostbyaddr(remoteHost, strlen(remoteHost), AF_INET);
        if (NULL == hostPtr)
        {
        perror("Error resolving server address");
        exit(1);
        }
    }

    serverName.sin_family = AF_INET;
    serverName.sin_port = htons(remotePort);
    (void) memcpy(&serverName.sin_addr,
      hostPtr->h_addr,
      hostPtr->h_length);

    status = connect(clientSocket,
      (struct sockaddr*) &serverName,
        sizeof(serverName));
    if (-1 == status)
    {
        perror("connect()");
        exit(1);
    }

    /*
     * Client application specific code goes here
     *
     * e.g. receive messages from server, respond,
     * etc. */
    while (0 < (status = read(clientSocket, buffer, sizeof(buffer) - 1)))
    {
	if (strncmp(buffer,"+HELLO",6)==0) {
	  printf("Got the hello\n");
	  write(clientSocket,"PASS vbus\n", 10);
	} else if (strncmp(buffer,"+OK: Password accepted",22)==0) {
	  printf("Password accepted\n");
	  write(clientSocket,"DATA\n", 5);
	} else if (strncmp(buffer,"+OK: Data incoming...",21)==0) {
	  printf("Ready for data\n");
	  // Now we are in the data phase, read a byte at a time until
	  // you get to the AA SYNC byte
	  handle_data(clientSocket);
	  
	  //printf("Received %d bytes:\n",status);
	  //print_hex(buffer,status);
          //printf("%d: %s|", status, buffer);
	} else {
	  printf("Unexpected response\n");
	}
    }

    if (-1 == status)
    {
        perror("read()");
    }

    close(clientSocket);

    return 0;
}
