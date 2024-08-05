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
    String key;
    String val;

    void write(WiFiClient& client) const;
};

class HeaderSet {
public:
    Header headers[MAX_HEADER_COUNT];
    int header_count = 0;

    int find(char *key); // returns the index of the header with the specified key. TODO: add case-insensitivity
    void add(const String& key, const String& val); // add a header manually
    int read_header_line(WiFiClient& c, int &error_code_out);
    void write(WiFiClient& client);
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

    Response(): code(0), status{}, headers(), body{} {
    }

    void write(WiFiClient& client);
};

typedef int (*handler)(const Request&, Response&);
int NotFoundHandler(const Request& req, Response& resp);

class HandlerReg {
public:
    char verb[10];
    char path[MAX_PATH_LENGTH];
    int (*func)(const Request&, Response&); // handler func

    HandlerReg(): verb{}, path{}, func(nullptr) {
    }
};

class Webserver {
private:
    WiFiServer *srv;
    HandlerReg handlers[MAX_HANDLERS]{};
    int handler_count;

public:
    explicit Webserver(WiFiServer* srv);

    // Register handlers against specific paths
    int register_handler(const char *verb, const char *path, handler h);

    // pick a handler based on the verb+path
    handler choose_handler(const Request& req);

    // call this in setup()
    void begin() const {
        if (this->srv != nullptr) {
            this->srv->begin();
        }
    }

    // call this in loop()
    int listen_once(WiFiClient &client, Response& out);
};

#endif //NINAWEBSERVER_LIBRARY_H
