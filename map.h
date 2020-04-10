#pragma once

typedef struct KeyValue KeyValue;
typedef struct Map Map;

extern Map *new_map();
extern int map_size(Map *map);
extern KeyValue *map_insert(Map *map, const char *key, void *item);
extern KeyValue *map_lookup(Map *map, const char *key);
extern const char *kv_key(KeyValue *kv);
extern void *kv_value(KeyValue *kv);
