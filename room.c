/**
 * File: room.c
 * ----------------------------
 *   room structure organization
 */

#include "room.h"

List *getRoom(List *room_list, char *last) {
    char *name;
    name = strtok_r(NULL, ".", &last);
    printf("name: %s\nremaining: %s\n", name, last);
    if (last == NULL) {
        return room_list;
    }
    List *rooms = getRoom(room_list, last);
    Node *room = NULL;
    printf("Starting: %s\nLevel: %d\n", name, rooms->level);
    if (rooms->head != NULL) {
        printf("Head found\n");
        room = get(rooms, name);
    }
    if (room == NULL) {
        printf("Room not found: %s\n", name);
        printf("Creating: %s\n", name);
        room = create(NULL);
        strcpy(room->name, name);
        room->superlist = rooms;
        room->sublist = newList();
        room->users = newList();
        room->sublist->level = rooms->level + 1;
        // room->sublist->from = room;
        append(rooms, room);
    }
    return room->sublist;
}

// Room *getRoom(List room_list, char *names, int levels) {
//     if (*names == NULL) {
//         return node
//     }
//     Node *room_cursor = room_list->head;
//     for (int i=levels; i; i--) {
//         find(room_list, names);

//     }
//     return getRoom(*names++);
// }