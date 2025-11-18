#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#ifdef _WIN32
#include <Winsock2.h>
#endif
#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef unsigned long long SOCKET;
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#endif
// #pragma comment(lib, "Ws2_32.lib") //be insteaded of -lwsock32 when not
// complie in VS/MSVC more details in
// https://learn.microsoft.com/zh-cn/windows/win32/winsock/creating-a-basic-winsock-application

#define IP_ADDRESS "127.0.0.1" // create socket in localhost
#define PORT                                                                   \
  3000 // 1-65535 usually use 1024-5000 for temporary TCP/IP connection

/*
Basic server steps:
1. setup server sockets
2. bind to a IP address and port
3. set socket to listen mode (wait to be connected)
4. accept a new socket corresponding to one connection
5. use new socket send and recieve message
6. close the socket
*/
int main(int argc, char *argv[]) {
  // you need to initialize in windows
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  // create socket
  SOCKET servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (servSock == INVALID_SOCKET) {
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }
  /*
  AF_INET : using ipv4
  SOCK_STREAM : transfrom data stream
  you can use SOCK_NONBLOCK to setup no block socket in linux

  IPPROTO_TCP : using TCP to send and recieve

  not recommend to use UDP because it will lead packet loss
  */

  // bind socket to a IP address
  struct sockaddr_in sockAddr;
  memset(&sockAddr, 0, sizeof(sockAddr));
  sockAddr.sin_family = AF_INET;
  sockAddr.sin_addr.s_addr = inet_addr(IP_ADDRESS);
  sockAddr.sin_port = htons(PORT);
  if (bind(servSock, (struct sockaddr *)&sockAddr, sizeof(struct sockaddr)) ==
      SOCKET_ERROR) {
#ifdef _WIN32
    closesocket(servSock);
    WSACleanup();
#endif
#ifdef __linux__
    close(servSock);
#endif
    return 1;
  }

  // use for IPV4
  // struct sockaddr_in {
  // sa_family_t    sin_family; /* address family: AF_INET */
  // in_port_t      sin_port;   /* port in network byte order */
  // struct in_addr sin_addr;   /* internet address */
  // };
  // /* Internet address. */
  // struct in_addr {
  //     uint32_t       s_addr;     /* address in network byte order */
  // };

  // use for IPV6
  // struct sockaddr_in6 {
  //     sa_family_t     sin6_family;   /* AF_INET6 */
  //     in_port_t       sin6_port;     /* port number */
  //     uint32_t        sin6_flowinfo; /* IPv6 flow information */
  //     struct in6_addr sin6_addr;     /* IPv6 address */
  //     uint32_t        sin6_scope_id; /* Scope ID (new in 2.4) */
  // };

  // struct in6_addr {
  //     unsigned char   s6_addr[16];   /* IPv6 address */
  // };

  if (listen(servSock, 20) == SOCKET_ERROR) {
#ifdef _WIN32
    closesocket(servSock);
    WSACleanup();
#endif
#ifdef __linux__
    close(servSock);
#endif
    return 1;
  }
  // set the socket to be "listen" statu!
  // not means try to listen the connection form cilent
  // The max connection num 20

  struct sockaddr clntAddr; // store the information(IP/port) of cilent
#ifdef _WIN32
  int nSize = sizeof(struct sockaddr);
#endif
#ifdef __linux
  unsigned int nSize = sizeof(struct sockaddr);
#endif
  SOCKET clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &nSize);
  if (clntSock == INVALID_SOCKET) {
#ifdef _WIN32
    closesocket(servSock);
    WSACleanup();
#endif
#ifdef __linux__
    close(servSock);
#endif
    return 1;
  }

  printf("Client connected\n");

  /* Read an optional initial handshake from client to supply onecard args.
     We wait briefly (1s) for the client to send an argument string; if none
     is provided we fall back to running onecard without extra args. */
  char hand[1024];
  hand[0] = '\0';
  {
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(clntSock, &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    int sel = select(clntSock + 1, &rfds, NULL, NULL, &tv);
    if (sel > 0 && FD_ISSET(clntSock, &rfds)) {
      ssize_t hn = recv(clntSock, hand, sizeof(hand) - 1, 0);
      if (hn > 0) {
        /* trim trailing newline */
        if (hand[hn - 1] == '\n') hand[hn - 1] = '\0';
        else hand[hn] = '\0';
        printf("Client requested onecard args: '%s'\n", hand);
      } else {
        hand[0] = '\0';
      }
    }
  }

  /* Create pipes to communicate with onecard process */
  int pc_stdin[2]; /* parent writes to child's stdin */
  int pc_stdout[2]; /* parent reads from child's stdout */
  if (pipe(pc_stdin) < 0 || pipe(pc_stdout) < 0) {
    perror("pipe");
    close(clntSock);
    close(servSock);
    return 1;
  }

  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    close(clntSock);
    close(servSock);
    return 1;
  }
  if (child == 0) {
    /* Child: redirect stdin/stdout and exec onecard (game engine) */
    close(pc_stdin[1]);
    dup2(pc_stdin[0], STDIN_FILENO);
    close(pc_stdin[0]);

    close(pc_stdout[0]);
    dup2(pc_stdout[1], STDOUT_FILENO);
    close(pc_stdout[1]);

    /* Exec the game binary (relative path). If the client supplied an
       argument string in `hand` we split it into argv tokens and pass them
       to execv so onecard can run with that argument. */
    const char *prog = "./onecard";
    if (hand[0] == '\0') {
      execl(prog, prog, NULL);
    } else {
      /* build argv array: prog, tokens..., NULL */
      char *argvv[16];
      int argcv = 0;
      argvv[argcv++] = (char *)prog;
      /* tokenise hand in-place */
      char *p = hand;
      while (argcv < (int)(sizeof(argvv) / sizeof(argvv[0])) - 1 && p && *p) {
        /* skip leading spaces */
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;
        argvv[argcv++] = p;
        /* find end */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
      }
      argvv[argcv] = NULL;
      execv(prog, argvv);
    }
    perror("execl");
    _exit(1);
  }

  /* Parent: close child's ends */
  close(pc_stdin[0]);
  close(pc_stdout[1]);

  /* Forward loop: forward client -> onecard stdin, and onecard stdout -> client */
  fd_set readfds;
  int maxfd = (pc_stdout[0] > clntSock) ? pc_stdout[0] : clntSock;
  char buf[2048];
  while (1) {
    FD_ZERO(&readfds);
    FD_SET(clntSock, &readfds);
    FD_SET(pc_stdout[0], &readfds);
    int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
    if (ready < 0) {
      if (errno == EINTR) continue;
      perror("select");
      break;
    }
    if (FD_ISSET(clntSock, &readfds)) {
      ssize_t rn = recv(clntSock, buf, sizeof(buf), 0);
      if (rn <= 0) {
        if (rn < 0) perror("recv");
        break;
      }
      /* Forward to onecard stdin (handle partial writes) */
      {
        size_t towrite = (size_t)rn;
        size_t off = 0;
        while (off < towrite) {
          ssize_t wn = write(pc_stdin[1], buf + off, towrite - off);
          if (wn < 0) {
            if (errno == EINTR) continue;
            perror("write to onecard");
            break;
          }
          off += (size_t)wn;
        }
      }
      printf("Forwarded from client to onecard: %.*s", (int)rn, buf);
      fflush(stdout);
    }
    if (FD_ISSET(pc_stdout[0], &readfds)) {
      ssize_t rn = read(pc_stdout[0], buf, sizeof(buf));
      if (rn <= 0) {
        if (rn < 0) perror("read from onecard");
        break;
      }
      /* Send to client (handle partial sends) */
      {
        size_t tosend = (size_t)rn;
        size_t off = 0;
        while (off < tosend) {
          ssize_t sn = send(clntSock, buf + off, tosend - off, 0);
          if (sn < 0) {
            if (errno == EINTR) continue;
            perror("send to client");
            break;
          }
          off += (size_t)sn;
        }
      }
      printf("Forwarded from onecard to client: %.*s", (int)rn, buf);
      fflush(stdout);
    }
  }

  /* cleanup */
  close(pc_stdin[1]);
  close(pc_stdout[0]);
  close(clntSock);
  // sent and recieve data from client
  // recommend to use uint8 instead of char* to transform value

  /*
  send and recv and same parameters
  int __stdcall recv(SOCKET s, char *buf, int len, int flags)
  int __stdcall send(SOCKET s, const char *buf, int len, int flags)
  return the length of sending / recieving
  */
#ifdef _WIN32
  closesocket(clntSock);
  closesocket(servSock);
#endif
#ifdef __linux__
  close(clntSock);
  close(servSock);
#endif

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
