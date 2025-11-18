#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <Winsock2.h>
#endif
#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
typedef unsigned long long SOCKET;
#define INVALID_SOCKET (SOCKET)(~0)
#endif

#define IP_ADDRESS "127.0.0.1" // create socket in localhost
#define PORT                                                                   \
  3000 // 1-65535 usually use 1024-5000 for temporary TCP/IP connection

/*
Basic cilent steps:
1. setup cilent sockets
2. try to connect to sever
3. use send and recv to send and recieve message
4. close the socket
*/
int main(int argc, char *argv[]) {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  /*remember to check with the value like what I do in sever !!!*/
  SOCKET clntSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  struct sockaddr_in sockAddr;
  memset(&sockAddr, 0, sizeof(sockAddr));
  sockAddr.sin_family = AF_INET;
  sockAddr.sin_addr.s_addr = inet_addr(IP_ADDRESS);
  sockAddr.sin_port = htons(PORT);
  connect(clntSock, (struct sockaddr *)&sockAddr, sizeof(struct sockaddr));
  /* If the user provided command-line args, send them as the initial
     handshake so the server can launch onecard with those args. */
  if (argc > 1) {
    /* join argv[1..] with spaces */
    char hbuf[1024];
    hbuf[0] = '\0';
    size_t pos = 0;
    for (int i = 1; i < argc && pos + 2 < sizeof(hbuf); ++i) {
      size_t l = strlen(argv[i]);
      if (pos + l + 2 >= sizeof(hbuf)) break;
      if (i > 1) hbuf[pos++] = ' ';
      memcpy(hbuf + pos, argv[i], l);
      pos += l;
    }
    hbuf[pos++] = '\n';
    ssize_t sn = send(clntSock, hbuf, pos, 0);
    if (sn < 0) perror("send handshake");
    else printf("Sent handshake args: %.*s", (int)(pos - 1), hbuf);
  }
#ifdef __linux__
  /* receive server greeting (optional) */
  char greet[1024];
  ssize_t gn = recv(clntSock, greet, sizeof(greet) - 1, 0);
  if (gn > 0) {
    greet[gn] = '\0';
    printf("Received from server: %s\n", greet);
  }

  /* Interactive streaming: use select to handle socket input (onecard output)
     and stdin input (user play) concurrently. Read available socket data in a
     loop and write it to stdout so large ASCII blocks aren't truncated. */
  enum { BUF_SZ = 16384 };
  char sbuf[4096];
  char rbuf[BUF_SZ];
  fd_set rfds;
  int maxfd = clntSock > STDIN_FILENO ? clntSock : STDIN_FILENO;
  for (;;) {
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(clntSock, &rfds);
    int sel = select(maxfd + 1, &rfds, NULL, NULL, NULL);
    if (sel < 0) {
      perror("select");
      break;
    }
    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      if (fgets(sbuf, sizeof(sbuf), stdin) == NULL) {
        /* EOF on stdin -> close and exit */
        break;
      }
      size_t tosend = strlen(sbuf);
      size_t off = 0;
      while (off < tosend) {
        ssize_t sent = send(clntSock, sbuf + off, tosend - off, 0);
        if (sent < 0) {
          perror("send");
          goto client_done;
        }
        off += (size_t)sent;
      }
      printf("Sent: %s", sbuf);
      fflush(stdout);
    }
    if (FD_ISSET(clntSock, &rfds)) {
      /* Read whatever is available from socket and print to stdout */
      for (;;) {
        ssize_t rn = recv(clntSock, rbuf, sizeof(rbuf), 0);
        if (rn < 0) {
          perror("recv");
          goto client_done;
        }
        if (rn == 0) {
          /* connection closed */
          goto client_done;
        }
        fwrite(rbuf, 1, (size_t)rn, stdout);
        fflush(stdout);
        /* If less than buffer read then likely no more immediate data */
        if ((size_t)rn < sizeof(rbuf)) break;
      }
    }
  }
client_done: ;

#endif
#ifdef _WIN32
  closesocket(clntSock);
#endif
#ifdef __linux__
  close(clntSock);
#endif

#ifdef _WIN32
  WSACleanup();
#endif
}
