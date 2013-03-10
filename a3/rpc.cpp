#include "rpc.h"
#include "common.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <pthread.h>

using std::cerr;
using std::cout;
using std::endl;
using std::vector;
using std::remove;

// Global variables
int my_binder_sock;

// for server's use
char* my_server_identifier;
int my_server_port;
int my_server_sock;
vector<int> my_server_connections;
vector<int> my_server_to_remove;

vector<struct PROC_SKELETON> my_server_procedures;

// for client's use
char* my_client_identifier;
int my_client_port;
int my_client_sock;

// server calls this
int rpcInit() {
  char *binderAddress = getenv("BINDER_ADDRESS");
  char *binderPort = getenv("BINDER_PORT");

  // open connection to binder
  int sockfd, portno;
  struct sockaddr_in binder_addr;
  struct hostent *binder;
  portno = atoi(binderPort);
  sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  binder = gethostbyname(binderAddress);

  bzero((char *) &binder_addr, sizeof(binder_addr));
  binder_addr.sin_family = AF_INET;
  bcopy((char *)binder->h_addr,
        (char *)&binder_addr.sin_addr.s_addr,
        binder->h_length);
  binder_addr.sin_port = htons(portno);

  connect(sockfd,(struct sockaddr *)&binder_addr, sizeof(binder_addr));
  my_binder_sock = sockfd;

  // create a socket for clients to connect to
  int status;
  struct addrinfo hints;
  struct addrinfo* servinfo;
  struct addrinfo* p;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  status = getaddrinfo(NULL, "0", &hints, &servinfo);
  /*
  if (status != 0) {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    return;
  }
  */

  p = servinfo;
  my_server_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

  status = bind(my_server_sock, servinfo->ai_addr, servinfo->ai_addrlen);

  status = listen(my_server_sock, 5);

  // get server identifier
  my_server_identifier = new char[STR_LEN];
  gethostname(my_server_identifier, STR_LEN);
  cout << "server " << my_server_identifier << endl;

  // get port number
  struct sockaddr_in sin;
  socklen_t len = sizeof(sin);
  getsockname(my_server_sock, (struct sockaddr *)&sin, &len);
  my_server_port = ntohs(sin.sin_port);
  cout << "port " << my_server_port << endl;

  //status = listen(my_server_sock, 5);


  return RETURN_SUCCESS;
}

int rpcCall(char* name, int* argTypes, void** args) {
  int status;

  // create a socket to do calls to the binder
  char *binderAddress = getenv("BINDER_ADDRESS");
  char *binderPort = getenv("BINDER_PORT");

  // open connection to binder
  int sockfd, portno;
  struct sockaddr_in binder_addr;
  struct hostent *binder;
  portno = atoi(binderPort);
  sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  binder = gethostbyname(binderAddress);

  bzero((char *) &binder_addr, sizeof(binder_addr));
  binder_addr.sin_family = AF_INET;
  bcopy((char *)binder->h_addr,
        (char *)&binder_addr.sin_addr.s_addr,
        binder->h_length);
  binder_addr.sin_port = htons(portno);

  connect(sockfd,(struct sockaddr *)&binder_addr, sizeof(binder_addr));
  my_binder_sock = sockfd;

  // make call to binder
  // socket stuff to binder
  struct CLIENT_BINDER_LOC_REQUEST req;
  req.name = name;
  req.argTypes = argTypes;
  status = req.sendMessage(my_binder_sock);
  if (status < 0) {
    return status;
  }

  // receive response to location request
  // message type
  int msg_type;
  status = recv(my_binder_sock, &msg_type, sizeof msg_type, 0);
  if (status < 0) {
    cerr << "ERROR: receive failed" << endl;
    return RETURN_FAILURE;
  }

  struct CLIENT_BINDER_LOC_SUCCESS* res_success;
  struct CLIENT_BINDER_LOC_FAILURE* res_failure;

  // handle location request response message
  if (msg_type == MSG_LOC_FAILURE) {
    res_failure = CLIENT_BINDER_LOC_FAILURE::readMessage(my_binder_sock);
    return RETURN_FAILURE;
  } else if (msg_type == MSG_LOC_SUCCESS) {
    res_success = CLIENT_BINDER_LOC_SUCCESS::readMessage(my_binder_sock);
    cerr << res_success->server_identifier << endl;
    cerr << res_success->port << endl;
    if (res_success == NULL) {
      return RETURN_FAILURE;
    }
  } else {
    // bad message type?
    return RETURN_FAILURE;
  }

  // make connection to server
  // TODO refactor this block?
  int server_sock;
  struct sockaddr_in server_addr;
  struct hostent *server;
  portno = res_success->port;
  server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  server = gethostbyname(res_success->server_identifier);

  bzero((char *) &binder_addr, sizeof(binder_addr));
  binder_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr,
        (char *)&binder_addr.sin_addr.s_addr,
        server->h_length);
  binder_addr.sin_port = htons(portno);


  connect(server_sock,(struct sockaddr *)&binder_addr, sizeof(binder_addr));

  // send execute message
  struct CLIENT_SERVER_EXECUTE executeReq;
  executeReq.name = name;
  executeReq.argTypes = argTypes;
  executeReq.args = args;
  status = executeReq.sendMessage(server_sock);
  if (status < 0) {
    return status;
  }

  // receive response to execute message
  // message type
  msg_type = -1;
  status = recv(server_sock, &msg_type, sizeof msg_type, 0);
  if (status < 0) {
    cerr << "ERROR: receive failed" << endl;
    return RETURN_FAILURE;
  }

  struct CLIENT_SERVER_EXECUTE_SUCCESS* exec_success;
  struct CLIENT_SERVER_EXECUTE_FAILURE* exec_failure;

  // handle location request response message
  if (msg_type == MSG_EXECUTE_FAILURE) {
    exec_failure = CLIENT_SERVER_EXECUTE_FAILURE::readMessage(server_sock);
    return RETURN_FAILURE;
  } else if (msg_type == MSG_EXECUTE_SUCCESS) {
    cerr << "exec success" << endl;
    exec_success = CLIENT_SERVER_EXECUTE_SUCCESS::readMessage(server_sock);
    cerr << "exec success done reading" << endl;
    if (exec_success == NULL) {
      return RETURN_FAILURE;
    }
  } else {
    // bad message type?
    return RETURN_FAILURE;
  }

  // set the returned args
  args = exec_success->args;

  // close connection
  close(my_binder_sock);

  return RETURN_SUCCESS;
}

int rpcCacheCall(char* name, int* argTypes, void** args) {
  return 0;
}

int rpcRegister(char* name, int* argTypes, skeleton f) {
  int len;
  int status;
  //cerr << name << ' ' << argTypes[0] << ' ' << argTypes[1] << ' ' << argTypes[2] << ' ' << argTypes[3] << endl;
  //cerr << argTypesLength(argTypes) << endl;

  // make an entry in local database
  struct PROC_SKELETON procedure;
  // copy the function name so we own it
  len = strlen(name) + 1;
  procedure.name = new char[len];
  strncpy(procedure.name, name, len);
  // copy the argTypes so we own it
  len = argTypesLength(argTypes);
  procedure.argTypes = new int[len];
  memcpy(procedure.argTypes, argTypes, len*sizeof(int));
  procedure.f = f;
  my_server_procedures.push_back(procedure);

  // send the message to binder
  struct SERVER_BINDER_REGISTER msg;
  msg.server_identifier = my_server_identifier;
  msg.port = my_server_port;
  msg.name = procedure.name;
  msg.argTypes = procedure.argTypes;

  status = msg.sendMessage(my_binder_sock);

  if (status < 0) {
    return status;
  }

  return REGISTER_SUCCESS;
}

int rpcExecute() {
  fd_set readfds;
  int n;
  struct sockaddr_storage their_addr;

  // start listening
  int status = listen(my_server_sock, 5);

  while (true) {
    // build the connection list
    FD_ZERO(&readfds);
    FD_SET(my_server_sock, &readfds);
    n = my_server_sock;
    for (vector<int>::iterator it = my_server_connections.begin();
        it != my_server_connections.end(); ++it) {
      int connection = *it;
      FD_SET(connection, &readfds);
      if (connection > n)
        n = connection;
    }
    n = n+1;

    status = select(n, &readfds, NULL, NULL, NULL);

    if (status == -1) {
      cerr << "ERROR: select failed." << endl;
    } else {
      cerr << "in here" << endl;
      // one or both of the descriptors have data
      if (FD_ISSET(my_server_sock, &readfds)) {
        // ready to accept
        socklen_t addr_size = sizeof their_addr;
        int new_sock = accept(my_server_sock, (struct sockaddr*)&their_addr, &addr_size);

        if (new_sock < 0) {
          cerr << "ERROR: while accepting connection" << endl;
          close(new_sock);
          continue;
        }

        // add new connection
        my_server_connections.push_back(new_sock);
        cerr << "in here 2" << endl;

      } else {
        // a connection is ready to send us stuff
        for (vector<int>::iterator it = my_server_connections.begin();
            it != my_server_connections.end(); ++it) {
          int connection = *it;
          cerr << "in here 3" << endl;
          if (FD_ISSET(connection, &readfds)) {
            //process_server_message(connection);

            int status;

            // receive the message type
            int msg_type;
            status = recv(connection, &msg_type, sizeof msg_type, 0);
            cerr << msg_type << endl;

            if (status < 0) {
              cerr << "ERROR: receive failed" << endl;
              //return -1;
            }
            if (status == 0) {
              // client has closed the connection
              my_server_to_remove.push_back(connection);
              //return -1;
            }

            switch (msg_type) {
              case MSG_TERMINATE:
                return 0;
                break;
              case MSG_EXECUTE:
                cerr << "in here 4" << endl;
                struct CLIENT_SERVER_EXECUTE* res = CLIENT_SERVER_EXECUTE::readMessage(connection);
                cerr << res->name << endl;
                cerr << res->argTypes[0] << " " << res->argTypes[1] << endl;

                for (vector<struct PROC_SKELETON>::iterator p = my_server_procedures.begin(); p != my_server_procedures.end(); ++p) {
                  cerr << "in here 5" << endl;
                  struct PROC_SKELETON proc = (*p);
                  if (strcmp(proc.name, res->name) == 0) {
                    cerr << "in here 6" << endl;
                    bool match = true;
                    int len = argTypesLength(res->argTypes);
                    if (len != argTypesLength(proc.argTypes)) {
                      match = false;
                    }

                    for (int i = 0; i < len; i++) {
                      if (res->argTypes[i] != proc.argTypes[i]) {
                        match = false;
                      }
                    }
                    cerr << "in here 7" << endl;

                    if (match) {
                      cerr << "in here 8" << endl;
                      //(*proc.f)(res->argTypes, res->args);
                      cerr << "in here 9" << endl;

                      // send the server location to the client
                      struct CLIENT_SERVER_EXECUTE_SUCCESS msg;
                      msg.name = res->name;
                      msg.argTypes = res->argTypes;
                      msg.args = res->args;
                      msg.sendMessage(connection);
                      break;
                    }
                  }
                }
                break;
            }
          }
        }
      }
      // close connections
      for (vector<int>::iterator it = my_server_to_remove.begin(); it != my_server_to_remove.end(); ++it) {
        my_server_connections.erase(remove(my_server_connections.begin(), my_server_connections.end(), *it), my_server_connections.end());
        close(*it);
      }
      my_server_to_remove.clear();
    }

  }
  return 0;
}

// Client calls this
int rpcTerminate() {
  // verify that the request comes from the same ip address/hostname
  // Inform all of the servers
  // Wait for servers to terminate
  // Terminate binder
  int msg_type = MSG_TERMINATE;
  send(my_binder_sock, &msg_type, sizeof(msg_type), 0);
  return 0;
}
