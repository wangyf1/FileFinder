#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "includes/QueryProtocol.h"


char *port_string = "1500";
unsigned short int port;
char *ip = "127.0.0.1";
// 34.210.203.41 1800

#define BUFFER_SIZE 1000

void RunQueryHelper(int sock_fd) {
  char resp[BUFFER_SIZE];
  int len;
  while (1) {
    len = read(sock_fd, &resp, sizeof(resp) - 1);
    resp[len] = '\0';
    if (strcmp(resp, "GOODBYE") == 0) {
      break;
    }
    printf("%s\n", resp);
    if (SendAck(sock_fd) != 0) {
      exit(1);
    }
  }
}

void RunQuery(char *query) {
  if (strlen(query) > 100) {
    printf("%s\n", "Query should be less than 100 characters.");
  }
  // Find the address
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
  // Create the socket
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  // Connect to the server
  if (connect(sock_fd, result->ai_addr, result->ai_addrlen) == -1) {
    printf("Not connected to movie server\n");
    exit(1);
  }
  printf("Connected to movie server\n");
  // Do the query-protocol
  char resp[BUFFER_SIZE];
  int len = read(sock_fd, &resp, sizeof(resp) - 1);
  resp[len] = '\0';
  if (CheckAck(resp) != 0) {
    printf("ACK not check successfully\n");
    exit(1);
  }
  write(sock_fd, query, strlen(query));
  char result_num[BUFFER_SIZE];
  int length = read(sock_fd, &result_num, sizeof(result_num) - 1);
  result_num[length] = '\0';
  printf("num_responses: %s\n", result_num);
  if (SendAck(sock_fd) != 0) {
    printf("ACK not sent successfully\n");
    exit(1);
  }
  if (atoi(result_num) > 0) {
    RunQueryHelper(sock_fd);
  } else {
    printf("No search result\n");
  }
  // Close the connection
  freeaddrinfo(result);
  close(sock_fd);
}

void RunPrompt() {
  char input[BUFFER_SIZE];
  while (1) {
    printf("Enter a term to search for, or q to quit: ");
    scanf("%s", input);
    printf("input was: %s\n", input);
    if (strlen(input) == 1) {
      if (input[0] == 'q') {
        printf("Thanks for playing! \n");
        return;
      }
    }
    printf("\n\n");
    RunQuery(input);
  }
}
void printInstruction() {
  printf("%s\n", "Start client by calling ./queryclient [ipaddress] [port]");
}

int main(int argc, char **argv) {
  // Check/get arguments
  if (argc != 3) {
    printInstruction();
    exit(1);
  }

  // Get info from user
  ip = argv[1];
  port_string = argv[2];
  // Run Query
  RunPrompt();
  return 0;
}
