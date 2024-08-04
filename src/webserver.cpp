#include "webserver.h"

#include <iostream>
#include <string.h>

int NotFoundHandler(const Request& req, Response& resp) {
    resp.code = 404;
    strcpy(resp.status, "Not Found");
    resp.body[0] = '\0';
    return 0;
}

Webserver::Webserver(const int port, const char *addr, WiFiServer *srv) {
    this->handler_count = 0;
    this->port = port;
    if (port <= 0) {
        this->port = 80;
    }
    strcpy(this->address, "0.0.0.0");
    if (addr != nullptr) {
        strcpy(this->address, addr);
    }
    this->srv = srv;
}

int Webserver::register_handler(const char* verb, const char *path, handler h) {
    // Validate input. Are the handler and path useable? Are there open slots left to register this handler to?
    if (strlen(path) > MAX_PATH_LENGTH) {
        return 0;
    }
    if (strlen(verb) == 0) {
        return 0;
    }
    if (h == nullptr) {
        return 0;
    }
    if (this->handler_count == MAX_HANDLERS) {
        return 0;
    }

    // We have open slots, and a valid configuration. Save the path against the requested function pointer.
    strcpy(this->handlers[this->handler_count].verb, verb);
    strcpy(this->handlers[this->handler_count].path, path);
    this->handlers[this->handler_count].func = h;

    return 1;
}

int Webserver::listen_once(Response& out) {
    WiFiClient client = this->srv->available();
    if (!client) {
        return 0;
    }

    // Read the incoming request
    Request req;
    int bytes_read = 0;

    // Read the first line. Expected to be "VERB /path/ HTTP/x.y"
    int error_code = 0;
    bytes_read = req.read_verb_line(client, error_code);
    if (error_code != 0) {
        // Serial.println("Failed to read verb line");
        out.code = error_code;
        if (out.code == 404) {
            strcpy(out.status, "Not Found");
        }
        return 0;
    }

    // Read the headers until you reach an empty line
    for (int i=0; i < MAX_HEADER_COUNT; ++i) {
        int current_header_count = req.read_header_line(client, error_code);
        if (current_header_count < 0) {
            // an error occurred
            if (error_code == 400) {
                strcpy(out.status, "Bad Request");
            }
            if (error_code == 500) {
                strcpy(out.status, "Internal Server Error");
            }
            return 0;
        }
        if (current_header_count == 0) {
            // line was empty
            break;
        }
    }

    // Read the body
    int body_read = 0;
    char c;
    while(client.available()) {
        c = client.read();
        if (bytes_read >= MAX_BODY_LENGTH) {
            // request too long
            out.code = 413;
            strcpy(out.status, "Content Too Large");
            client.stop();
            break;
        }
        ++bytes_read;

        req.body[body_read] = c;
        ++body_read;
    }
    return 1;
}

handler Webserver::choose_handler(const Request& req) {
    for (int i=0; i < this->handler_count; i++) {
        if (strcmp(req.verb, this->handlers[i].verb) == 0 && strcmp(req.path, this->handlers[i].path) == 0) {
            return this->handlers[i].func;
        }
    }

    return &NotFoundHandler;
}

int HeaderSet::read_header_line(WiFiClient& c, int& error_code_out) {
    // read in the whole line, trimming the "\r\n" at the end, and keeping track of where the ':' separator was.
    char buf[MAX_HEADER_LENGTH];
    int bytes_read = 0;
    int sep_pos = -1;
    for (bytes_read=0; bytes_read < MAX_HEADER_LENGTH; ++bytes_read) {
        buf[bytes_read] = c.read();
        if (buf[bytes_read] == ':') {
            sep_pos = bytes_read;
        }
        if (buf[bytes_read] == '\n') {
            break;
        }
    }

    // check for error and edge cases
    if (bytes_read < 2) {
        // the line was missing the '\r' before the '\n'
        error_code_out = 400;
        return -1;
    }
    if (bytes_read == 2) {
        // empty line. This is "valid", insofar as it marks the end of the header section
        error_code_out = 0;
        return 0;
    }

    if (sep_pos < 0) {
        // not a properly-formed header
        error_code_out = 400;
        return -1;
    }

    if (this->header_count == MAX_HEADER_COUNT) {
        // valid header, but more than this library can handle
        error_code_out = 500;
        return -1;
    }

    if (strlen(buf+sep_pos+1) == 0) {
        // separator at the end of the line
        error_code_out = 400;
        return -1;
    }

    // replace the \r\n characters with '\0' to terminate the string
    for (int i=sep_pos; i < bytes_read; ++i) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = '\0';
        }
    }

    // Parse a Header object
    buf[sep_pos] = '\n';
    strcpy(this->headers[this->header_count].key, buf);
    strcpy(this->headers[this->header_count].val, buf+sep_pos+1);
    this->header_count++;
    error_code_out = 0;
    return this->header_count;
}

int Request::read_header_line(WiFiClient& c, int &error_code_out) {
    return this->headers.read_header_line(c, error_code_out);
}

int Request::read_verb_line(WiFiClient& c, int& error_code_out) {
    int i;
    int bytes_read = 0;

    // read the verb
    for (i=0; i < 10; ++i) {
        this->verb[i] = c.read();
        bytes_read++;
        if (this->verb[i] == '\n') {
            // invalid request
            error_code_out = 400;
            return bytes_read;
        }
        if (this->verb[i] == ' ') {
            // We've finished reading the verb part of the line.
            // Null-terminate the string and break out of the loop.
            this->verb[i] = '\n';
            break;
        }
    }

    // read the path
    for (i=0; i < MAX_PATH_LENGTH; ++i) {
        this->path[i] = c.read();
        bytes_read++;
        if (this->path[i] == '\n') {
            // invalid request
            error_code_out = 400;
            return bytes_read;
        }
        if (this->path[i] == ' ') {
            // finished reading the path part of the line.
            // Null-terminate the string and break out of the loop.
            this->path[i] = '\n';
            break;
        }
    }

    // read the rest
    while (c.read() != '\n') {
        bytes_read++;
        // we don't really care what else is in this line
    }

    // Success!
    error_code_out = 0;
    return bytes_read;
}
