#include "evn.h"

#include <errno.h> // errno
#include <unistd.h> // close
#include <stdlib.h> // free

static int evn_server_unix_create(struct sockaddr_un* socket_un, char* sock_path);

inline struct evn_stream* evn_stream_create(int fd) {
  puts("[dummyd] new stream");
  struct evn_stream* stream;

  //stream = realloc(NULL, sizeof(struct evn_stream));
  stream = calloc(1, sizeof(struct evn_stream));
  stream->fd = fd;
  stream->type = 0;
  evn_set_nonblock(stream->fd);
  ev_io_init(&stream->io, evn_stream_read_priv_cb, stream->fd, EV_READ);

  puts("[dummyd] .nc");
  return stream;
}

int evn_stream_destroy(EV_P_ struct evn_stream* stream)
{
  // TODO delay freeing of server until streams have closed
  // or link loop directly to stream?
  ev_io_stop(EV_A_ &stream->io);
  close(stream->fd);
  free(stream);
  return 0;
}

// This callback is called when data is readable on the unix socket.
void evn_stream_read_priv_cb(EV_P_ ev_io *w, int revents)
{
  char data[4096];
  struct evn_exception error;
  int length;
  struct evn_stream* stream = (struct evn_stream*) w;

  evn_debugs("[EVN] - new connection - EV_READ - daemon fd has become readable");
  usleep(100);
  length = recv(stream->fd, &data, 4096, 0);

  if (length < 0)
  {
    if (stream->error) { stream->error(EV_A_ stream, &error); }
  }
  else if (0 == length)
  {
    if (stream->close) { stream->close(EV_A_ stream, false); }
    evn_stream_destroy(EV_A_ stream);
  }
  else if (length > 0)
  {
    if (stream->data) { stream->data(EV_A_ stream, data, length); }
  }
}

void evn_server_connection_priv_cb(EV_P_ ev_io *w, int revents)
{
  puts("[EVN] - new connection - EV_READ - deamon fd has become readable");

  int stream_fd;
  struct evn_stream* stream;

  // since ev_io is the first member,
  // watcher `w` has the address of the
  // start of the evn_server struct
  struct evn_server* server = (struct evn_server*) w;

  while (1)
  {
    stream_fd = accept(server->fd, NULL, NULL);
    if (stream_fd == -1)
    {
      if( errno != EAGAIN && errno != EWOULDBLOCK )
      {
        fprintf(stderr, "accept() failed errno=%i (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    }
    puts("[EVN] new stream");
    stream = evn_stream_create(stream_fd);
    if (server->connection) { server->connection(EV_A_ server, stream); }
    //stream->index = array_push(&server->streams, stream);
    ev_io_start(EV_A_ &stream->io);
  }
  puts("[EVN] finished accepting connections.");
}

// Simply adds O_NONBLOCK to the file descriptor of choice
int evn_set_nonblock(int fd)
{
  int flags;

  flags = fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  return fcntl(fd, F_SETFL, flags);
}

static int evn_server_unix_create(struct sockaddr_un* socket_un, char* sock_path)
{
  int fd;

  unlink(sock_path);

  // Setup a unix socket listener.
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (-1 == fd) {
    perror("echo server socket");
    exit(EXIT_FAILURE);
  }

  // Set it non-blocking
  if (-1 == evn_set_nonblock(fd)) {
    perror("echo server socket nonblock");
    exit(EXIT_FAILURE);
  }

  // Set it as unix socket
  socket_un->sun_family = AF_UNIX;
  strcpy(socket_un->sun_path, sock_path);

  return fd;
}

int evn_server_destroy(EV_P_ struct evn_server* server)
{
  if (server->close) { server->close(server->EV_A_ server); }
  ev_io_stop(server->EV_A_ &server->io);
  free(server);
  return 0;
}

struct evn_server* evn_server_create(EV_P_ char* sock_path, int max_queue)
{
    struct timeval timeout = {0, 500000}; // 500000 us, ie .5 seconds;
    struct evn_server* server = calloc(1, sizeof(struct evn_server));

    server->EV_A = EV_A;
    server->fd = evn_server_unix_create(&server->socket, sock_path);
    server->socket_len = sizeof(server->socket.sun_family) + strlen(server->socket.sun_path);

    //array_init(&server->streams, 128);

    if (-1 == bind(server->fd, (struct sockaddr*) &server->socket, server->socket_len))
    {
      perror("echo server bind");
      exit(EXIT_FAILURE);
    }

    if (-1 == listen(server->fd, max_queue)) {
      perror("listen");
      exit(EXIT_FAILURE);
    }
    
    if (-1 == setsockopt(server->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) {
      perror("setting timeout to 0.5 sec");
      exit(EXIT_FAILURE);
    }
    printf("set the receive timeout to %lf\n", ((double)timeout.tv_sec+(1.e-6)*timeout.tv_usec));
    return server;
}

int evn_server_listen(struct evn_server* server)
{
  ev_io_init(&server->io, evn_server_connection_priv_cb, server->fd, EV_READ);
  ev_io_start(server->EV_A_ &server->io);

  return 0;
}