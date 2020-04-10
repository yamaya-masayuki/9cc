#include "9cc.h"
#include <stdlib.h>
#include <string.h>

struct KeyValue {
    const char *key;
    void *value;
};

struct Map {
    Vector *data;
};

Map *new_map()
{
    Map *map = malloc(sizeof(Map));
    map->data = new_vec();
    return map;
}

int map_size(Map *map) { return vec_size(map->data); }

KeyValue *map_insert(Map *map, const char *key, void *item)
{
    KeyValue *kv = malloc(sizeof(KeyValue));
    kv->key = key;
    kv->value = item;
    vec_push(map->data, kv);
    return kv;
}

KeyValue *map_lookup(Map *map, const char *key)
{
    int i;

    D("n:%d, key:%s", map->data->len, key);
    for (i = 0; i < vec_size(map->data); i++) {
        KeyValue *kv = (KeyValue *)vec_get(map->data, i);
        if (strcmp(kv->key, key) == 0) return kv;
    }

    return NULL;
}

const char *kv_key(KeyValue *kv)
{
    if (kv == NULL) return NULL;
    return kv->key;
}

void *kv_value(KeyValue *kv)
{
    if (kv == NULL) return NULL;
    return kv->value;
}
