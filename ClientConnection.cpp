//****************************************************************************
//                         REDES Y SISTEMAS DISTRIBUIDOS
//
//                     2º de grado de Ingeniería Informática
//
//              This class processes an FTP transaction.
//
//****************************************************************************

#include "ClientConnection.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <langinfo.h>
#include <locale.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "common.h"

int define_socket_TCP(int port) {
  struct sockaddr_in sin;
  int s;
  s = socket(AF_INET, SOCK_STREAM, 0);

  if (s < 0) {
    errexit("No se puede crear el socket: %s\n", strerror(errno));
  }
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(port);
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
    errexit("No se puede enlazar el socket: %s\n", strerror(errno));
  }
  if (listen(s, 5) < 0) {
    errexit("No se puede escuchar en el socket: %s\n", strerror(errno));
  }
  return s;
}

ClientConnection::ClientConnection(int s) {
  int sock = (int)(s);

  char buffer[MAX_BUFF];

  control_socket = s;
  // Check the Linux man pages to know what fdopen does.
  fd = fdopen(s, "a+");
  if (fd == NULL) {
    std::cout << "Connection closed" << std::endl;

    fclose(fd);
    close(control_socket);
    ok = false;
    return;
  }

  ok = true;
  data_socket = -1;
  parar = false;
};

ClientConnection::~ClientConnection() {
  fclose(fd);
  close(control_socket);
}

int connect_TCP(uint32_t address, uint16_t port) {
  struct sockaddr_in sin;
  struct hostent *hent;
  int s;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  sin.sin_addr.s_addr = address;

  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    errexit("No se puede crear el socket: %s\n", strerror(errno));
  }
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
    errexit("No se puede conectar con el servidor: %s\n", strerror(errno));
  }
  return s;
}

void ClientConnection::stop() {
  close(data_socket);
  close(control_socket);
  parar = true;
}

#define COMMAND(cmd) strcmp(command, cmd) == 0

// This method processes the requests.
// Here you should implement the actions related to the FTP commands.
// See the example for the USER command.
// If you think that you have to add other commands feel free to do so. You
// are allowed to add auxiliary methods if necessary.

void ClientConnection::WaitForRequests() {
  if (!ok) {
    return;
  }

  fprintf(fd, "220 Service ready\n");

  while (!parar) {
    fscanf(fd, "%s", command);
    if (COMMAND("USER")) {
      fscanf(fd, "%s", arg);
      fprintf(fd, "331 User name ok, need password\n");
    } else if (COMMAND("PWD")) {
      fprintf(fd, "257 \"/\" is the current directory\n");
      fflush(fd);
    } else if (COMMAND("PASS")) {
      fscanf(fd, "%s", arg);
      if (strcmp(arg, "1234") == 0) {
        fprintf(fd, "230 User logged in\n");
      } else {
        fprintf(fd, "530 Not logged in.\n");
        parar = true;
      }

    } else if (COMMAND("PORT")) {
      int a1, a2, a3, a4, p1, p2;
      fscanf(fd, "%d,%d,%d,%d,%d,%d", &a1, &a2, &a3, &a4, &p1, &p2);
      printf("IP: %d.%d.%d.%d\n", a1, a2, a3, a4);
      uint32_t address = (a4 << 24) | (a3 << 16) | (a2 << 8) | a1;
      uint16_t port = (p1 << 8) | p2;
      data_socket = connect_TCP(address, port);
      fprintf(fd, "200 Data connection established\n");
      fflush(fd);

    } else if (COMMAND("PASV")) {
      int s = define_socket_TCP(0);
      struct sockaddr_in sin;
      socklen_t len = sizeof(sin);

      int value = getsockname(s, (struct sockaddr *)&sin, &len);
			if (value < 0) {
				errexit("Error al obtener el puerto\n");
			}
      uint16_t port = sin.sin_port;
      int p1 = (port >> 8) & 0xFF;
			int p2 = port & 0xFF;
			fprintf(fd, "227 Entering passive mode (127.0.0.1, %d,%d)\n", p1, p2);
      data_socket = accept(s, (struct sockaddr *)&sin, &len);
  
    } else if (COMMAND("STOR")) {
      fscanf(fd, "%s", arg);

			fprintf(fd, "150 File status okay; about to open data connection.\n");
			fflush(fd);
			FILE *file = fopen(arg, "wb+");

			if (file == NULL) {
				fprintf(fd, "450 Requested file action not taken. File unavailable.\n");
				fflush(fd);
				close(data_socket);
				break;
			}

			fprintf(fd, "125 Data connection already open; transfer starting.\n");
			fflush(fd);
			char buffer[MAX_BUFF];

			while (true) {
				int bytes = read(data_socket, buffer, MAX_BUFF);
				fwrite(buffer, 1, bytes, file);
				if (bytes < MAX_BUFF) {
					break;
				}
			}
    } else if (COMMAND("RETR")) {
      fscanf(fd, "%s", arg);
			FILE *file = fopen(arg, "rb");

			if (file == NULL) {
				fprintf(fd, "450 Requested file action not taken. File unavailable.\n");
				fflush(fd);
				close(data_socket);
			} else {
				fprintf(fd, "150 File status okay; about to open data connection.\n");
				fflush(fd);
				char buffer[MAX_BUFF];

				while (true) {
					int bytes = fread(buffer, 1, MAX_BUFF, file);
					send(data_socket, buffer, bytes, 0);
					if (bytes < MAX_BUFF) {
						break;
					}
				}
			}
    } else if (COMMAND("LIST")) {
      DIR* dir = opendir(".");
			if(dir == NULL) {
				fprintf(fd, "450 Requested file action not taken. File unavailable.\n");
				fflush(fd);
				close(data_socket);
			} else {
				fprintf(fd, "-----");
				fflush(fd);
				struct dirent *entry;
				while((entry = readdir(dir)) != NULL) {
					send(data_socket, entry->d_name, strlen(entry->d_name), 0);
				}
				fprintf(fd, "-------");
				closedir(dir);
				close(data_socket);
			}		
    } else if (COMMAND("SYST")) {
      fprintf(fd, "215 UNIX Type: L8.\n");
    }

    else if (COMMAND("TYPE")) {
      fscanf(fd, "%s", arg);
      fprintf(fd, "200 OK\n");
    }

    else if (COMMAND("QUIT")) {
      fprintf(fd,
              "221 Service closing control connection. Logged out if "
              "appropriate.\n");
      close(data_socket);
      parar = true;
      break;
    }

    else {
      fprintf(fd, "502 Command not implemented.\n");
      fflush(fd);
      printf("Comando : %s %s\n", command, arg);
      printf("Error interno del servidor\n");
    }
  }

  fclose(fd);

  return;
};
