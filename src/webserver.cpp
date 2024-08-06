#include "webserver.h"

#include <iostream>
#include <cstring>

int NotFoundHandler(const Request& req, Response& resp) {
    resp.code = 404;
    strcpy(resp.status, "Not Found");
    resp.body = "";
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

    // Read the first line. Expected to be "VERB /path/ HTTP/x.y"
    if (!req.read_verb_line(client)) {
        out.code = 400;
        strcpy(out.status, "Bad Request");
        return 1;
    }

    // Read the headers until you reach an empty line
    int error_code = 0;
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
    if (client.available()) {
        req.body = client.readString();
        req.body.trim();
    } else {
        req.body = "";
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

handler Webserver::choose_handler(const Request& req) const {
    for (int i=0; i < this->handler_count; i++) {
        if (req.verb == this->handlers[i].verb && req.path == this->handlers[i].path) {
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

int Request::read_verb_line(WiFiClient& c) {
    // read the verb
    this->verb = c.readStringUntil(' ');
    if (this->verb.length() == 0) {
        return 0;
    }
    // read the path
    this->path = c.readStringUntil(' ');
    if (this->path.length() == 0) {
        return 0;
    }
    // read the http version
    const String tmp = c.readStringUntil('\n');
    if (tmp.length() == 0) {
        return 0;
    }

    // Success!
    return 1;
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
    // If there is a body to send, set the content-length header as well
    if (this->body.length() > 0) {
        const String len(this->body.length());
        this->headers.add("Content-Length", len);
    }
    client.print("HTTP/1.1 ");
    client.print(this->code);
    client.print(" ");
    client.println(this->status);
    this->headers.write(client);
    client.print("\r\n"); // empty line between headers and body
    if (this->body.length() > 0) {
        client.print(this->body);
        client.print("\r\n\r\n");
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
