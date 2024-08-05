#include "webserver.h"

#include <iostream>
#include <cstring>

int NotFoundHandler(const Request& req, Response& resp) {
    resp.code = 404;
    strcpy(resp.status, "Not Found");
    strcpy(resp.body, "\r\n");
    return 0;
}

Webserver::Webserver(WiFiServer *srv) {
    this->handler_count = 0;
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
    Serial.print("registering handler for '");
    Serial.print(this->handlers[this->handler_count].verb);
    Serial.print(" ");
    Serial.print(this->handlers[this->handler_count].path);
    Serial.println("'");
    this->handlers[this->handler_count].func = h;
    this->handler_count++;

    return 1;
}

int Webserver::listen_once(WiFiClient &client, Response& out) {
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
        out.code = error_code;
        if (out.code == 404) {
            strcpy(out.status, "Not Found");
        }
        return 1;
    }
    Serial.print("Read first line: ");
    Serial.print(req.verb);
    Serial.print(" " );
    Serial.println(req.path);

    // Read the headers until you reach an empty line
    for (int i=0; i < MAX_HEADER_COUNT; ++i) {
        int current_header_count = req.read_header_line(client, error_code);
        if (current_header_count == -2) {
            // indicates empty string
            break;
        } else if (current_header_count < 0) {
            // other errors
            if (error_code == 400) {
                strcpy(out.status, "Bad Request");
            }
            if (error_code == 500) {
                strcpy(out.status, "Internal Server Error");
            }
            return 0;
        } else {
            // success, new header read in
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

    // Now that we have populated the Request, decide what to do with it.
    auto h = choose_handler(req);
    if (h == nullptr) {
        h = &NotFoundHandler;
    }

    const int success = h(req, out);
    if (!success) {
        Serial.print("Error handling request ");
        Serial.println(success);
        out.code = 500;
        strcpy(out.status, "Internal Server Error");
        return 1;
    }

    return 1;
}

handler Webserver::choose_handler(const Request& req) {
    for (int i=0; i < this->handler_count; i++) {
        if (strcmp(req.verb, this->handlers[i].verb) == 0 && strcmp(req.path, this->handlers[i].path) == 0) {
            return this->handlers[i].func;
        }
    }

    Serial.println("Using default handler: NotFound");
    return &NotFoundHandler;
}

int HeaderSet::read_header_line(WiFiClient& c, int& error_code_out) {
    if (this->header_count == MAX_HEADER_COUNT) {
        // valid header, but more than this library can handle
        Serial.println("Aborting HTTP read - too many headers");
        error_code_out = 500;
        return -1;
    }

    String header_str = c.readStringUntil('\r');
    header_str.trim();
    if (header_str.length() == 0) {
        // empty line
        error_code_out = 0;
        return -2;
    }

    int sep_index = header_str.indexOf(':');
    if (sep_index < 0) {
        Serial.println("empty header key");
        // invalid header line
        error_code_out = 400;
        return -1;
    }
    this->headers[this->header_count].key = header_str.substring(0, sep_index);
    this->headers[this->header_count].val = header_str.substring(sep_index+1);

    if (this->headers[this->header_count].key.length() == 0) {
        Serial.println("Error: empty header key");
        error_code_out = 400;
        return -1;
    }
    if (this->headers[this->header_count].val.length() == 0) {
        Serial.println("Error: empty header val");
        error_code_out = 400;
        return -1;
    }

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
            this->verb[i] = '\0';
            break;
        }
    }

    // read the path
    for (i=0; i < MAX_PATH_LENGTH; ++i) {
        this->path[i] = c.read();
        bytes_read++;
        if (this->path[i] == '\n') {
            // invalid request
            Serial.println("Error: got newline while reading path.");
            error_code_out = 400;
            return bytes_read;
        }
        if (this->path[i] == ' ') {
            // finished reading the path part of the line.
            // Null-terminate the string and break out of the loop.
            this->path[i] = '\0';
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

void HeaderSet::write(WiFiClient& client) {
    for (const auto& header : this->headers) {
        header.write(client);
    }
}

void Header::write(WiFiClient &client) const {
    if (this->key.length() == 0) {
        return;
    }
    client.print(this->key.c_str());
    client.print(": ");
    client.println(this->val.c_str());
}


void Response::write(WiFiClient& client) {
    if (this->code == 0) {
        Serial.println("Error: No status code set");
        this->code = 204;
    }
    if (strlen(this->status) == 0) {
        Serial.println("Error: No status message set");
        strcpy(this->status, "No Content");
    }
    // always send Connection: close header
    this->headers.add("Connection", "close");
    client.print("HTTP/1.1 ");
    client.print(this->code);
    client.print(" ");
    client.println(this->status);
    this->headers.write(client);
    client.print("\n"); // empty line between headers and body
    if (strlen(this->body) > 0) {
        client.write(this->body);
        client.write("\r\n\r\n");
    }
}

void HeaderSet::add(const String& key, const String& val) {
    if (this->header_count == MAX_HEADER_COUNT) {
        // TODO: signal error
        return;
    }
    this->headers[this->header_count].key = key;
    this->headers[this->header_count].val = val;
    this->header_count++;
}
