/* netcopy.c
 * netcopy -s (send) filename(s) ip
 * or -r (receive) port
 * -p 1234 set port number*/

/* TODO:  crear estructura para el file que contenga permisos */

#include <string.h>
#define _XOPEN_SOURCE 500
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#define MAX_BLOCK_SIZE 1024
char filebuf[MAX_BLOCK_SIZE];
char *port = "8080";

void printhelp(char progname[]);
int server(char *file);
int client(char *hostname);
int get_ip();

int main(int argc, char *argv[]) {
  int (*func)(char *) = NULL;

  int opt, rv;
  /* Simple sanity check */
  if (argc < 3) {
    printhelp(argv[0]);
    return 1;
  }
  /* Parse command-line options */
  while ((opt = getopt(argc, argv, "srhp:")) != -1) {
    switch (opt) {
    case 's': /* server mode. send files   */
              /* Check for ip, port and files arguments */
      func = server;
      break;
    case 'r': /* client mode. receive files */
      /* Check port argument */
      if (func) {
        printhelp(argv[0]);
        return 1;
      } else
        func = client;
      break;
    case 'p': /* select port number */
      port = optarg;
      break;
    case 'h': /* -h for help */
      printhelp(argv[0]);
      return 0;
    default: /* in case of invalid options*/
      printhelp(argv[0]);
      return 1;
    }
  }
  return (func(argv[optind]));
}

int get_ip() {
  FILE *f;
  char line[100], *p, *c;

  if ((f = fopen("/proc/net/route", "r")) == NULL) {
    perror("Error opening /proc/net/route");
    return errno;
  }

  while (fgets(line, 100, f)) {
    p = strtok(line, " \t");
    c = strtok(NULL, " \t");

    if (p != NULL && c != NULL) {
      if (strcmp(c, "00000000") == 0) {
        printf("Default interface is : %s \n", p);
        break;
      }
    }
  }

  // which family do we require , AF_INET or AF_INET6
  struct ifaddrs *ifaddr, *ifa;
  int family, s;
  char ipAdress[INET_ADDRSTRLEN];

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    return errno;
  }

  // Walk through linked list, maintaining head pointer so we can free list
  // later
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    family = ifa->ifa_addr->sa_family;

    if (strcmp(ifa->ifa_name, p) == 0) {
      if (family == AF_INET) {
        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), ipAdress,
                        INET_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);

        if (s != 0) {
          printf("getnameinfo() failed: %s\n", gai_strerror(s));
          return (errno);
        }

        printf("Esperando conexion en %s puerto %s\n", ipAdress, port);
      }
    }
  }

  freeifaddrs(ifaddr);

  return 0;
}

int server(char *filename) {
  int fd, filesz;
  struct stat filestat;
  char buffer[256];
  memset(buffer, 0, sizeof(buffer));

  if (stat(filename, &filestat) == -1) {
    fprintf(stderr, "Error reading %s: %s\n", filename, strerror(errno));
    return errno;
  }
  filesz = filestat.st_size;
  sprintf(buffer, "%s,%ld", filename, filestat.st_size);

  if ((fd = open(filename, O_RDONLY)) == -1) {
    fprintf(stderr, "Error opening %s: %s\n", filename, strerror(errno));
    return errno;
  }

  /* printf("Configuring local address...\n"); */
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  struct addrinfo *local_address;

  if (getaddrinfo(0, port, &hints, &local_address)) {
    perror("getaddrinfo failed");
    return errno;
  }

  /* Creating socket... */
  int socket_listen;
  socket_listen = socket(local_address->ai_family, local_address->ai_socktype,
                         local_address->ai_protocol);
  if (socket_listen == -1) {
    perror("socket() failed");
    return errno;
  }
  /* Binding socket to local address... */
  if (bind(socket_listen, local_address->ai_addr, local_address->ai_addrlen)) {
    perror("bind() failed");
    return errno;
  }

  freeaddrinfo(local_address);

  /* Listening...*/
  if (listen(socket_listen, 10) < 0) {
    perror("listen() failed");
    return errno;
  }

  int e;
  if ((e = get_ip()))
    return e;

  struct sockaddr_storage client_address;
  socklen_t client_len = sizeof(client_address);
  int socket_client =
      accept(socket_listen, (struct sockaddr *)&client_address, &client_len);
  if (socket_client == -1) {
    perror("accept() failed.");
    return errno;
  }

  printf("Cliente conectado desde la IP: ");
  char address_buffer[INET_ADDRSTRLEN];
  getnameinfo((struct sockaddr *)&client_address, client_len, address_buffer,
              INET_ADDRSTRLEN, 0, 0, NI_NUMERICHOST);
  printf("%s\n", address_buffer);

  /* send filename and file data */
  int bytes_w;
  bytes_w = write(socket_client, buffer, strlen(buffer));
  if (bytes_w == -1) {
    perror("Writing to socket error.");
    return errno;
  }

  if (read(socket_client, buffer, sizeof(buffer)) != bytes_w) {
    fprintf(stderr, "Bad response from client\n");
    return 100;
  }

  int total_bytes_w = 0;
  while ((bytes_w = read(fd, filebuf, MAX_BLOCK_SIZE)) > 0) {
    write(socket_client, filebuf, bytes_w);
    total_bytes_w += bytes_w;
  }

  if (total_bytes_w == filesz) {
    printf("File sent OK.\n");
  } else
    fprintf(stderr, "Error sending file.\n");

  close(socket_client);
  close(socket_listen);
  return 0;
}

int client(char *hostname) {
  int r;
  long filesize;
  char filename[255];
  char buffer[255];
  memset(buffer, 0, sizeof(buffer));

  /* printf("Configuring remote address.. \n"); */
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *server_address;
  if (getaddrinfo(hostname, port, &hints, &server_address)) {
    perror("getaddrinfo failed");
    return errno;
  }
  /* get remote address */
  char address_buffer[INET_ADDRSTRLEN];
  char service_buffer[8];
  getnameinfo(server_address->ai_addr, server_address->ai_addrlen,
              address_buffer, INET_ADDRSTRLEN, service_buffer,
              sizeof(service_buffer), NI_NUMERICHOST | NI_NUMERICSERV);

  /* printf("Creating socket...\n"); */
  int socket_cliente;
  socket_cliente =
      socket(server_address->ai_family, server_address->ai_socktype,
             server_address->ai_protocol);
  if (socket_cliente == -1) {
    perror("socket() failed");
    return errno;
  }
  if (connect(socket_cliente, server_address->ai_addr,
              server_address->ai_addrlen)) {
    perror("connect() failed");
    return errno;
  }
  freeaddrinfo(server_address);
  printf("Conectado a IP: %s puerto %s\n", address_buffer, service_buffer);

  /* receive file code here */

  r = read(socket_cliente, buffer, sizeof(buffer));
  if (r == -1) {
    perror("Reading socket error");
    return errno;
  }

  /* Check if file exist. Dont work! */
  /* if (!((utime(filename, NULL) == -1) && errno == ENOENT)) {
    fprintf(stderr, "File alredy exist\n");
    return 101;
  } */

  /* Responder con los datos recibidos */
  write(socket_cliente, buffer, r);
  /* Extraer filename y filesize */
  sscanf(buffer, "%[^,],%ld", filename, &filesize);

  int fd;

  if ((fd = open(filename, O_RDWR | O_CREAT, 0666)) == -1) {
    perror("Error opening file");
    return errno;
  }
  int recbytes = 0;
  while (recbytes != filesize) {
    r = read(socket_cliente, filebuf, MAX_BLOCK_SIZE);
    if (r == -1) {
      perror("Reading socket error");
      return errno;
    }

    if (write(fd, filebuf, r) == -1) {
      perror("Error writing to file");
      return errno;
    }
    recbytes = recbytes + r;
  }

  if (close(fd) == -1) {
    perror("Error closing file");
    return errno;
  }

  printf("File received OK\n");
  close(socket_cliente);
  return 0;
}

void printhelp(char progname[]) {
  printf("%s [-r] ip\n", progname);
  printf("%s [-s] filename\n", progname);
  printf("%s [-p] port\n", progname);
}
