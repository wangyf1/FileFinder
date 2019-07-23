#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>


#include "QueryProtocol.h"
#include "MovieSet.h"
#include "MovieIndex.h"
#include "DocIdMap.h"
#include "htll/Hashtable.h"
#include "QueryProcessor.h"
#include "FileParser.h"
#include "FileCrawler.h"

#define BUFFER_SIZE 1000

int Cleanup();

DocIdMap docs;
Index docIndex;

#define SEARCH_RESULT_LENGTH 1500

char movieSearchResult[SEARCH_RESULT_LENGTH];

void sigchld_handler(int s) {
  write(0, "Handling zombies...\n", 20);
  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;

  while (waitpid(-1, NULL, WNOHANG) > 0);

  errno = saved_errno;
}


void sigint_handler(int sig) {
  write(0, "Ahhh! SIGINT!\n", 14);
  Cleanup();
  exit(0);
}


void SendHelper(SearchResultIter iter, int client_fd) {
  SearchResult output = malloc(sizeof(struct searchResult));
  char dest[SEARCH_RESULT_LENGTH];
  char response[BUFFER_SIZE];
  int ind = 0;
  int i = 1;

  while (SearchResultIterHasMore(iter) != 0) {
    SearchResultGet(iter, output);
    CopyRowFromFile(output, docs, dest);
    SearchResultNext(iter);
    if (send(client_fd, dest, strlen(dest), 0) == -1) {
      printf("Result not sent successfully\n");
      exit(1);
    }
    ind = recv(client_fd, response, 4, 0);
    response[ind] = '\0';
    if (CheckAck(response) != 0) {
      printf("ACK not check successfully\n");
      exit(1);
    }
    printf("Sent result no. %d\n", i);
    i++;
  }
  SearchResultGet(iter, output);
  CopyRowFromFile(output, docs, dest);
  if (send(client_fd, dest, strlen(dest), 0) == -1) {
    printf("Result not sent successfully\n");
    exit(1);
  }
  ind = recv(client_fd, response, 4, 0);
  response[ind] = '\0';
  if (CheckAck(response) != 0) {
    printf("ACK not check successfully\n");
    exit(1);
  }
  printf("Sent result no. %d\n", i);
  free(output);
  DestroySearchResultIter(iter);
  printf("Destroying search result iter\n");
}

int HandleConnections(int sock_fd) {
  // Step 5: Accept connection
  // Fork on every connection.
  char query[BUFFER_SIZE];
  char response[BUFFER_SIZE];
  int client_fd;
  printf("Waiting for connection\n");
  while (1) {
    client_fd = accept(sock_fd, NULL, NULL);
    if (client_fd == -1) {
      exit(1);
    }
    printf("%s%d\n", "Client connected to client_fd: ", client_fd);
    if (!fork()) {
      close(sock_fd);
      if (SendAck(client_fd) == -1) {
        printf("ACK not sent\n");
        exit(1);
      }
      int len = recv(client_fd, query, sizeof(query) - 1, 0);
      query[len] = '\0';
      SearchResultIter iter = FindMovies(docIndex, query);
      uint64_t num_results = 0;
      if (iter == NULL) {
        printf("No result\n");
      } else {
        num_results = NumResultsInIter(iter);
      }
      char num_to_send[BUFFER_SIZE];
      snprintf(num_to_send, BUFFER_SIZE, "%ld", num_results);
      printf("%s results will be sending to client\n", num_to_send);
      if (send(client_fd, num_to_send, strlen(num_to_send), 0) == -1) {
        printf("Error sending num of results\n");
        exit(1);
      }
      int index = recv(client_fd, response, sizeof(response) - 1, 0);
      response[index] = '\0';
      if (CheckAck(response) != 0) {
        printf("ACK not check successfully\n");
        exit(1);
      }
      if (num_results != 0) {
        printf("Start sending\n");
        SendHelper(iter, client_fd);
        printf("Finish sending\n");
      }
      SendGoodbye(client_fd);
    }
  }

  close(client_fd);
  printf("Client connection closed");
  return 0;
}

void Setup(char *dir) {
  struct sigaction sa;

  sa.sa_handler = sigchld_handler;  // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  struct sigaction kill;

  kill.sa_handler = sigint_handler;
  kill.sa_flags = 0;  // or SA_RESTART
  sigemptyset(&kill.sa_mask);

  if (sigaction(SIGINT, &kill, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("Crawling directory tree starting at: %s\n", dir);
  // Create a DocIdMap
  docs = CreateDocIdMap();
  CrawlFilesToMap(dir, docs);
  printf("Crawled %d files.\n", NumElemsInHashtable(docs));

  // Create the index
  docIndex = CreateIndex();

  // Index the files
  printf("Parsing and indexing files...\n");
  ParseTheFiles(docs, docIndex);
  printf("%d entries in the index.\n", NumElemsInHashtable(docIndex->ht));
}

int Cleanup() {
  DestroyOffsetIndex(docIndex);
  DestroyDocIdMap(docs);
  return 0;
}

void printInstruction() {
  printf("%s\n", "Start server by calling ./queryserver [datadir] port");
}

int main(int argc, char **argv) {
  // Get args
  if (argc != 3) {
    printInstruction();
    exit(1);
  }
  char *dir_to_crawl = argv[1];
  char *port_string = argv[2];
  char *ip = "127.0.0.1";
  Setup(dir_to_crawl);

  // Step 1: Get address stuff
  struct addrinfo hints, *result;
  int status;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  status = getaddrinfo(ip, port_string, &hints, &result);
  if (status != 0) {
    printf("Wrong ipaddress and port\n");
    exit(1);
  }
  // Step 2: Open socket
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  // Step 3: Bind socket
  if (bind(sock_fd, result->ai_addr, result->ai_addrlen) != 0) {
    printf("Error binding socket\n");
    exit(1);
  }
  // Step 4: Listen on the socket
  if (listen(sock_fd, 10) != 0) {
    printf("Error listening on socket\n");
    exit(1);
  }
  // Step 5: Handle the connections
  HandleConnections(sock_fd);
  // Got Kill signal
  freeaddrinfo(result);
  close(sock_fd);
  Cleanup();
  return 0;
}
