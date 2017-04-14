/**
  @file  websocket.h
  @brief A header file include websocket function
*/

#include "websocket.h"

/**
 * Function: getHandshakeKey
 * ----------------------------
 *   compute |Sec-WebSocket-Accept| header field
 *   return base64-encoded sha1 of the given string
 */
char * getHandshakeKey(char *str) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char magic[80], *encoded;
    size_t input_length = 20, output_length;

    strcpy(magic, str);
    strcat(magic, GUID);
    SHA1((unsigned char*)magic, strlen(magic), hash);
    encoded = base64_encode(hash, input_length, (size_t*)&output_length);

    return encoded;
}

/**
 * Function: openHandshake
 * ----------------------------
 *   recieve handshake message from client and return them
 *   the corresponding header field
 *   return 1 if success, -1 if failed
 */
int openHandshake(int server_socket) {
    char buffer[BUFFERSIZE], serv_handshake[300], *token, *string, *sec_ws_accept;
    Header *header = newHeader();
    int length, state;

    printlog("-- Handshaking-- \n");

    // receive message from the client to buffer
    memset(&buffer, 0, sizeof(buffer));
    if ( (length = recv(server_socket, buffer, BUFFERSIZE, 0)) < 0 ) {
        printlog("Handshaking failed\n");
        close(server_socket);
        return -1;
    }

    string = calloc(length+1, 1);
    strncpy(string, buffer, length);

    token = strtok(string, "\r\n");
    header->get = token;
    if (strncasecmp("GET / HTTP/1.1", header->get, 14) != 0) {
        printlog("Invalid header\n");
        return -1;
    }

    while (token != NULL) {
        if (strncasecmp("Upgrade: ", token, 9) == 0) {
            // Upgrade
            header->upgrade = token+9;
            printlog("-> Upgrade: %s\n", header->upgrade);
        } else if (strncasecmp("Connection: ", token, 12) == 0) {
            // connection
            header->connection = token+12;
            printlog("-> Connection: %s\n", header->connection);
        } else if (strncasecmp("Host: ", token, 6) == 0) {
            // host
            header->host = token+6;
            printlog("-> Host: %s\n", header->host);
        } else if (strncasecmp("Origin: ", token, 8) == 0) {
            // origin
            header->origin = token+8;
            printlog("-> Origin: %s\n", header->origin);
        } else if (strncasecmp("Sec-WebSocket-Key: ", token, 19) == 0) {
            // key
            header->key = token+19;
            printlog("-> Sec-WebSocket-Key: %s\n", header->key);
        } else if (strncasecmp("Sec-WebSocket-Version: ", token, 23) == 0) {
            // version
            header->version = strtol(token+23, (char**)NULL, 10);
            printlog("-> Sec-WebSocket-Version: %d\n", header->version);
        } else if (strncasecmp("Sec-WebSocket-Extensions: ", token, 26) == 0) {
            // extensions
            header->extension = token+26;
            printlog("-> Sec-WebSocket-Extensions: %s\n", header->extension);
        } else if (strncasecmp("Sec-WebSocket-Protocol: ", token, 24) == 0) {
            // protocol
            header->protocol = token+24;
            printlog("-> Sec-WebSocket-Protocol: %s\n", header->protocol);
        } else if (strncasecmp("User-Agent: ", token, 12) == 0) {
            // protocol
            header->protocol = token+12;
            printlog("-> User-Agent: %s\n", header->protocol);
        }
        token = strtok(NULL, "\r\n");
    }

    // sha1, encode64
    sec_ws_accept = slice(getHandshakeKey(header->key), 28);

    // compose server handshake message
    strcpy(serv_handshake, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    strcat(serv_handshake, sec_ws_accept);
    strcat(serv_handshake, "\r\n\r\n");

    // return handshake from the server
    state = send(server_socket, serv_handshake, strlen(serv_handshake), 0);

    free(sec_ws_accept);

    return 0;

}

Header *newHeader() {
    Header *header = malloc(sizeof(Header));
    if (header != NULL) {
        header->get = NULL;
        header->upgrade = NULL;
        header->connection = NULL;
        header->host = NULL;
        header->origin = NULL;
        header->key = NULL;
        header->version = 0;
        header->accept = NULL;
        header->protocol = NULL;
        header->extension = NULL;
        header->agent = NULL;
    }
    return header;
}

int wsSend(Node *this, http_frame *frame) {
    User *user = (User*)this->data;
    int skip;
    char buffer[BUFFERSIZE];

    memset(buffer, 0, sizeof(buffer));

    if (frame->size <= 125) {
        skip = 2;
        buffer[1] = frame->size;
    } else if (frame->size <= 65535) {
        uint16_t len16;
        skip = 4;
        buffer[1] = 126;
        len16 = htons(frame->size);
        memcpy(buffer+2, &len16, sizeof(uint16_t));
    } else {
        uint64_t len64;
        skip = 10;
        buffer[1] = 127;
        len64 = htonl(frame->size);
        memcpy(buffer+2, &len64, sizeof(uint64_t));
    }

    // write http frame to buffer
    buffer[0] = frame->opcode;
    memcpy(buffer+skip, frame->message, frame->size);

    // send buffer to client
    if (send(user->socket, (void *)&buffer, frame->size + skip, 0) <= 0) {
        printlog("%s\n", "Error on sending message");
        return -1;
    }
    printlog("Message sent to: %d\n", user->socket);
    return 0;
}

int wsRecv(Node *this, http_frame *frame) {
    User *user = (User*)this->data;
    int opcode, length, hasmask, skip;
    char buffer[BUFFERSIZE], mask[4];
    memset(buffer, '\0', BUFFERSIZE);
    if (recv(user->socket, buffer, BUFFERSIZE, 0) <= 0) {
        printlog("%s\n", "Error on recieving message");
        return CLIENT_DISCONNECT;
    }

    opcode = buffer[0] & 0xff;
    hasmask = buffer[1] & 0x80 ? 1 : 0;
    length = buffer[1] & 0x7f;
    if (opcode != 129) {
        // bad opcode
        printlog("Bad opcode\n");
        return INVALID_HEADER;
    }
    if (!hasmask) {
        // remove opcode
        printlog("Message not masked\n");
        return INVALID_HEADER;
    }
    if (length <= 125) {
        // get mask
        skip = 6; // 2 + 0 + 4
        frame->size = length;
        memcpy(frame->mask, buffer + 2, sizeof(frame->mask));
    } else if (length == 126) {
        printlog("%s\n", "size = 126 extended");
        // 2 byte length
        uint16_t len16;
        memcpy(&len16, buffer + 2, sizeof(uint16_t));
        // get mask
        skip = 8; // 2 + 2 + 4
        frame->size = ntohs(len16);
        memcpy(frame->mask, buffer + 4, sizeof(frame->mask));
    } else if (length == 127) {
        printlog("%s\n", "size = 127 extended");
        // 8 byte length
        uint64_t len64;
        memcpy(&len64, buffer + 2, sizeof(uint64_t));
        // get mask
        skip = 14; // 2 + 8 + 4
        frame->size = ntohl64(len64);
        memcpy(frame->mask, buffer + 10, sizeof(frame->mask));
    }

    if (frame->size >= 8192) {
        printlog("Message too long\n");
        return MESSAGE_TOO_LONG;
    }

    frame->message = malloc(frame->size+1); // warning: memory leakage
    memset(frame->message, '\0', frame->size+1);
    memcpy(frame->message, buffer + skip, frame->size);

    // printf("expected msg len: %llu\n", frame->size);

    // remove mask from data
    for (uint64_t i=0; i<frame->size; i++){
        frame->message[i] = frame->message[i] ^ frame->mask[i % 4];
    }
    return SUCCESS;
}

void broadcast(List *all_users, Node *this, char *message, int flag) {
    http_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.opcode = 129;
    frame.message = message;
    frame.size = strlen(frame.message);
    if (map(this, sendMessage, &frame, flag) < 0) {
        removeNode(all_users, this);
        pthread_exit(NULL);
    }
}

int sendMessage(Node *this, void *frame_void) {
    User *user = (User*)(this->data);
    http_frame *frame = (http_frame*)frame_void;
    if (wsSend(this, frame) < 0) {
        return -1;
    }
    return 0;
}

void sendStatus(List *all_users) {
    json_t *json, *username, *username_list;
    json_error_t json_err;
    User *user;
    char *message;

    if (all_users->len == 0) {
        return;
    }

    username_list = json_array();
    Node *cursor = all_users->head;

    for (int i=all_users->len; i>0; i--) {
        user = (User*)cursor->data;
        username = json_string(user->name);
        json_array_append(username_list, username);
        cursor = cursor->next;
    }

    json = json_pack("{s:s, s:i, s:o}", "type", "online",
                                        "count", all_users->len,
                                        "users", username_list);
    message = json_dumps(json, JSON_COMPACT);
    printf("%s\n", message);
    broadcast(all_users, cursor, message, ALL);
    free(json);
    free(message);
}

void removeNode(List *list, Node *this) {
    removeUser(this->data);
    delete(list, this);
    sendStatus(list);
}

void removeUser(User *user) {
    close(user->socket);
    free(user->name);
    free(user);
}
