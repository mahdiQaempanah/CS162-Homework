#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

#define MAX_BUFFER_SIZE 8092
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

int size_of_file(int fd){
  struct stat st;
  fstat(fd, &st);
  return st.st_size;
}

int size_of_number(int i){
  int ret = 0;
  for(;i != 0; ret +=1, i/=10){}
  return  ret; 
}

void send_http_respone(int fd, int status_code, char** headers, int num_headers, char* data) {
    http_start_response(fd, status_code);
    for (int i = 0; i < num_headers; i++) {
        http_send_header(fd, headers[i*2], headers[i*2+1]);
    }
    http_end_headers(fd);
    if ((data != NULL) && (data[0] != '\0')) {
        http_send_string(fd, data);
    }
    close(fd);
    return;
}

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 * 
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sesnsitive to time-out errors.
 */
void serve_file(int fd, char *path) {
  int file_fd = open(path, O_RDONLY);
  int file_size = size_of_file(file_fd);
  char file_size_str[size_of_number(file_size)+2];
  
  sprintf(file_size_str, "%d", file_size);

  char* buffer = malloc(sizeof(char) * (file_size+10));
  read(file_fd, buffer, file_size);
  printf("serve file with path:%s\n", path);

  char* headers[] = {"Content-Type", http_get_mime_type(path), "Content-Length", file_size_str}; 
  send_http_respone(fd, 200, headers, 2, buffer);
  close(file_fd);
}

void serve_directory(int fd, char *path) {
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
  http_end_headers(fd);

  char* index_addr;
  if(path[strlen(path)-1] == '/'){
    index_addr = malloc(strlen(path) + strlen("index.html") + 1);
    strcpy(index_addr, path);
    strcat(index_addr, "index.html"); 
  } else{
    index_addr = malloc(strlen(path) + strlen("/index.html") + 1);
    strcpy(index_addr, path);
    strcat(index_addr, "/index.html"); 
  }
    

  struct stat pathst;
  if(stat(index_addr, &pathst) == 0) {
    printf("index.html in %s exists\n", path);
    serve_file(fd, index_addr);
  } 
  else{
    printf("index.html in %s does not exist\n", path);
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
    http_end_headers(fd);

    DIR* directory = opendir(path); 
    struct dirent* dir;
    while((dir = readdir(directory)) != NULL){
      if(strcmp(dir->d_name, ".") == 0)
        continue;
      
      else if(strcmp(dir->d_name, "..") == 0){
        char* parent = malloc(MAX_BUFFER_SIZE); 
        char* text = malloc(MAX_BUFFER_SIZE);

        if(path[strlen(path)-1] == '/')
          sprintf(parent, "%s", path);
        else
          sprintf(parent, "%s/", path); 

        parent[strlen(parent)-1] = '\0';
        for(int i = strlen(parent)-1; parent[i] != '/'; i--)
          parent[i] = '\0'; 
        sprintf(text, "<a href='./%s'>Parent directory</a><br>\n", dir->d_name);
        http_send_string(fd, text);
        free(text);
        free(parent); 
      }

      else{
        char* new_link = malloc(MAX_BUFFER_SIZE); 
        char* text = malloc(MAX_BUFFER_SIZE);

        if(path[strlen(path)-1] == '/')
          sprintf(new_link, "%s%s", path, dir->d_name);
        else
          sprintf(new_link, "%s/%s", path, dir->d_name); 
        
        struct stat pathst;
          if(stat(new_link, &pathst) == 0) {
            if(S_ISREG(pathst.st_mode)) 
              sprintf(text, "<a href='./%s'  style=\"color:green\">%s</a><br>\n", dir->d_name, dir->d_name);
            else
              sprintf(text, "<a href='./%s' style=\"color:red\">%s</a><br>\n", dir->d_name, dir->d_name); 
        }

        http_send_string(fd, text);
        free(text);
        free(new_link); 
      }
    }
  }
  free(index_addr);
}


/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 * 
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {
  struct http_request *request = http_request_parse(fd);

  if (request == NULL || request->path[0] != '/') {
    http_start_response(fd, 400);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  if (strstr(request->path, "..") != NULL) {
    http_start_response(fd, 403);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  /* Remove beginning `./` */
  char *path = malloc(strlen(server_files_directory) + strlen(request->path) + 1);
  memcpy(path, server_files_directory, strlen(server_files_directory));
  memcpy(path + strlen(server_files_directory), request->path+1, strlen(request->path));


  struct stat pathst;
  if(stat(path, &pathst) == 0) {
    if(S_ISREG(pathst.st_mode)) 
      serve_file(fd, path);
    else if(S_ISDIR(pathst.st_mode))
      serve_directory(fd, path);
    return; 
  } 
  else{
    char* headers[] = {"Content-Type", "text/html"};
    send_http_respone(fd, 404, headers, 1, "invalid direcotry or file input");
    return;
  }

  return; 
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */

void transfer_message(int dst, int src) {
  void *buffer = malloc(MAX_BUFFER_SIZE);
  size_t size;
  while ((size = read(src, buffer, MAX_BUFFER_SIZE)) > 0)
    http_send_data(dst, buffer, size);
  free(buffer);
}

typedef struct proxy_info {
  int src_socket;
  int dst_socket;
  int is_finished; 
  pthread_cond_t* cond;
} proxy_info;

struct proxy_info *create_proxy_info(int src, int dst, pthread_cond_t* cond) {
  proxy_info *proxy_info = (struct proxy_info*) malloc(sizeof(proxy_info));
  proxy_info->src_socket = src;
  proxy_info->dst_socket = dst;
  proxy_info->is_finished = 0; 
  proxy_info->cond = cond; 
  return proxy_info;
}

void *run_proxy(void *args) {
  proxy_info *pinfo = (proxy_info *) args;
  transfer_message(pinfo->dst_socket, pinfo->src_socket);
  pinfo->is_finished = 1; 
  pthread_cond_signal(pinfo->cond); 
  return NULL;
}

void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;  
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    close(fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(fd);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(target_fd, (struct sockaddr*) &target_address, sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);
    char *headers[] = {"Content-Type", "text/html"};
    send_http_respone(fd, 502, headers, 1, "<center><h1>502 Bad Gateway</h1><hr></center>");
    close(target_fd);
    return;
  }
  else{
    pthread_t t[2]; 
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    proxy_info *client_to_server = create_proxy_info(fd, target_fd, &cond);
    proxy_info *server_to_client = create_proxy_info(target_fd, fd, &cond);
    pthread_create(&t[0], NULL, run_proxy, client_to_server);
    pthread_create(&t[1], NULL, run_proxy, server_to_client);
    while(client_to_server->is_finished == 0 && client_to_server->is_finished == 0)
      pthread_cond_wait(&cond, &mutex);
    close(fd);
    close(target_fd);
  }
}

void* thread_routine(void* arg) {
    void (*request_handler)(int) = (void (*)(int)) arg;
    while(1){
      int fd = wq_pop(&work_queue);
      request_handler(fd);
      close(fd);
    }
    return NULL;
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) 
        pthread_create(&threads[i], NULL, thread_routine, (void*) request_handler);
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  wq_init(&work_queue);
  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    if(num_threads == 0){
      request_handler(client_socket_number);
      close(client_socket_number);
    }
    else{
      wq_push(&work_queue, client_socket_number); 
    }

  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
