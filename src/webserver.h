#ifndef NINAWEBSERVER_LIBRARY_H
#define NINAWEBSERVER_LIBRARY_H


#include <WiFiNINA.h>

#define MAX_PATH_LENGTH 128
#define MAX_BODY_LENGTH 1024
#define MAX_REQUEST_LENGTH 2048
#define MAX_HANDLERS 16
#define MAX_HEADER_LENGTH 128
#define MAX_HEADER_COUNT 10

class Header {
public:
    char key[MAX_HEADER_LENGTH];
    char val[MAX_HEADER_LENGTH];
};

class HeaderSet {
public:
    Header headers[MAX_HEADER_COUNT];
    int header_count;

    int find(char *key); // returns the index of the header with the specified key. TODO: add case-insensitivity
    int read_header_line(WiFiClient& c, int &error_code_out);
};

class Request {
public:
    char verb[10];
    char path[MAX_PATH_LENGTH];
    HeaderSet headers;
    char body[MAX_BODY_LENGTH];

    int read_verb_line(WiFiClient& c, int &error_code_out);
    int read_header_line(WiFiClient& c, int &error_code_out);
};

class Response {
public:
    int code;
    char status[22];
    HeaderSet headers;
    char body[MAX_BODY_LENGTH];
};

typedef int (*handler)(const Request&, Response&);
int NotFoundHandler(const Request& req, Response& resp);

class HandlerReg {
public:
    char verb[10];
    char path[MAX_PATH_LENGTH];
    int (*func)(const Request&, Response&); // handler func
};

class Webserver {
private:
    int port;
    char address[17]; // should default to "0.0.0.0"
    WiFiServer *srv;
    HandlerReg handlers[MAX_HANDLERS];
    int handler_count;

public:
    Webserver(const int port, const char *addr, WiFiServer* srv);

    // Register handlers against specific paths
    int register_handler(const char *verb, const char *path, handler h);

    // pick a handler based on the verb+path
    handler choose_handler(const Request& req);

    // call this in setup()
    void begin() {
        if (this->srv != nullptr) {
            this->srv->begin();
        }
    }

    // call this in loop()
    int listen_once(Response& out);
};

#endif //NINAWEBSERVER_LIBRARY_H
