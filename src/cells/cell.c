#include "grid.h"
#include "../utils.h"
#include "../graphics/resources.h"
#include "../api/api.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

tsc_cell_id_pool_t builtin;

void tsc_init_builtin_ids() {
    builtin.push = tsc_registerCell("push", "Push", "Can be pushed from all directions");
    builtin.slide = tsc_registerCell("slide", "Slide", "Can be pushed horizontally");
    builtin.mover = tsc_registerCell("mover", "Mover", "Moves forward one tile per tick");
    builtin.trash = tsc_registerCell("trash", "Trash", "Deletes anything that moves into it");
    builtin.enemy = tsc_registerCell("enemy", "Enemy", "Like Trash but also dies in the process");
    builtin.generator = tsc_registerCell("generator", "Generator", "Duplicates the cell behind it");
    builtin.placeable = tsc_registerCell("place", "Placeable", "Meant to represent areas the player may modify.\nMostly used in puzzles and vaults.");
    builtin.rotator_cw = tsc_registerCell("rotator_cw", "Rotator CW", "Rotates its adjacent neighbours 90 degrees clockwise per tick.");
    builtin.rotator_ccw = tsc_registerCell("rotator_ccw", "Rotator CCW", "Rotates its adjacent neighbours 90 degrees counter-clockwise per tick.");
    builtin.empty = tsc_registerCell("empty", "Empty", "Literally pure nothingness");
    builtin.wall = tsc_registerCell("wall", "Wall", "Immobile");

    tsc_celltable *placeTable = tsc_cell_newTable(builtin.placeable);
    placeTable->flags = TSC_FLAGS_PLACEABLE;

    builtin.textures.icon = tsc_strintern("icon");
    builtin.textures.copy = tsc_strintern("copy");
    builtin.textures.cut = tsc_strintern("cut");
    builtin.textures.del = tsc_strintern("delete");
    builtin.textures.setinitial = tsc_strintern("setinitial");
    builtin.textures.restoreinitial = tsc_strintern("restoreinitial");

    builtin.audio.destroy = tsc_strintern("destroy");
    builtin.audio.explosion = tsc_strintern("explosion");
    builtin.audio.move = tsc_strintern("move");

    builtin.optimizations.gens[0] = tsc_allocOptimization("gen0");
    builtin.optimizations.gens[1] = tsc_allocOptimization("gen1");
    builtin.optimizations.gens[2] = tsc_allocOptimization("gen2");
    builtin.optimizations.gens[3] = tsc_allocOptimization("gen3");
}

tsc_cell __attribute__((hot)) tsc_cell_create(const char *id, char rot) {
    tsc_cell cell;
    cell.id = id;
    cell.texture = NULL;
    cell.rot = rot % 4;
    cell.data = NULL;
    cell.updated = false;
    cell.celltable = NULL;
    cell.flags = 0;
    // -1 means no interpolation please
    cell.lx = -1;
    cell.ly = -1;
    cell.addedRot = 0;
    return cell;
}

tsc_cell __attribute__((hot)) tsc_cell_clone(tsc_cell *cell) {
    tsc_cell copy = *cell;
    if(cell->data == NULL) return copy;

    copy.data = malloc(sizeof(tsc_cellreg));
    copy.data->len = cell->data->len;
    copy.data->keys = malloc(sizeof(const char *) * copy.data->len);
    memcpy(copy.data->keys, cell->data->keys, sizeof(const char *) * copy.data->len);
    copy.data->values = malloc(sizeof(const char *) * copy.data->len);
    for(size_t i = 0; i < cell->data->len; i++) {
        copy.data->values[i] = tsc_strdup(cell->data->values[i]);
    }
    return copy;
}

void __attribute__((hot)) tsc_cell_destroy(tsc_cell cell) {
    if(cell.data != NULL) {
        for(size_t i = 0; i < cell.data->len; i++) {
            free(cell.data->values[i]);
        }
        free(cell.data->keys);
        free(cell.data->values);
        free(cell.data);
    }
}

const char *tsc_cell_get(const tsc_cell *cell, const char *key) {
    if(cell->data == NULL) return NULL;
    for(size_t i = 0; i < cell->data->len; i++) {
        if(tsc_streql(cell->data->keys[i], key)) {
            return cell->data->values[i];
        }
    }
    return NULL;
}

const char *tsc_cell_nthKey(const tsc_cell *cell, size_t idx) {
    if(cell->data == NULL) return NULL;
    if(idx >= cell->data->len) return NULL;
    return cell->data->keys[idx];
}

void tsc_cell_set(tsc_cell *cell, const char *key, const char *value) {
    if(value == NULL) {
        if(cell->data == NULL) return;
        // Remove
        size_t j = 0;
        for(size_t i = 0; i < cell->data->len; i++) {
            if(tsc_streql(cell->data->keys[i], key)) {
                free(cell->data->values[i]);
                continue;
            }
            cell->data->keys[j] = cell->data->keys[i];
            cell->data->values[j] = cell->data->values[i];
            j++;
        }
        if(j != cell->data->len) {
            // Something was removed
            cell->data->len = j;
            cell->data->keys = realloc(cell->data->keys, sizeof(const char *) * j);
            cell->data->values = realloc(cell->data->values, sizeof(const char *) * j);
        }
        return;
    }
    if(cell->data == NULL) {
        cell->data = malloc(sizeof(tsc_cellreg));
        cell->data->len = 0;
        cell->data->keys = NULL;
        cell->data->values = NULL;
    }
    for(size_t i = 0; i < cell->data->len; i++) {
        if(tsc_streql(cell->data->keys[i], key)) {
            free(cell->data->values[i]);
            cell->data->values[i] = tsc_strdup(value);
            return;
        }
    }
    size_t idx = cell->data->len++;
    cell->data->keys = realloc(cell->data->keys, sizeof(const char *) * cell->data->len);
    cell->data->keys[idx] = tsc_strintern(key);
    cell->data->values = realloc(cell->data->values, sizeof(const char *) * cell->data->len);
    cell->data->values[idx] = tsc_strdup(value);
}

void tsc_cell_rotate(tsc_cell *cell, signed char amount) {
	cell->rot += amount;
	cell->rot %= 4;
	while(cell->rot < 0) cell->rot += 4;
	cell->addedRot += amount;
}

void tsc_cell_swap(tsc_cell *a, tsc_cell *b) {
    tsc_cell c = *a;
    *a = *b;
    *b = c;
}

typedef struct tsc_cell_table_arr {
    tsc_celltable **tables;
    const char **ids;
    size_t tablec;
} tsc_cell_table_arr;

static tsc_cell_table_arr cell_table_arr = {NULL, NULL, 0};

tsc_celltable *tsc_cell_newTable(const char *id) {
    tsc_celltable *table = malloc(sizeof(tsc_celltable));
    tsc_celltable empty = {NULL, NULL, NULL};
    *table = empty;
    size_t idx = cell_table_arr.tablec++;
    cell_table_arr.tables = realloc(cell_table_arr.tables, sizeof(tsc_celltable *) * cell_table_arr.tablec);
    cell_table_arr.ids = realloc(cell_table_arr.ids, sizeof(const char *) * cell_table_arr.tablec);
    cell_table_arr.tables[idx] = table;
    cell_table_arr.ids[idx] = id;
    return table;
}

// HIGH priority for optimization
// While this is "technically" not thread-safe, you can use it as if it is.
tsc_celltable *tsc_cell_getTable(tsc_cell *cell) {
    // Locally cached VTable (id should never be assigned to so this is fine)
    if(cell->celltable != NULL) return cell->celltable;

    for(size_t i = 0; i < cell_table_arr.tablec; i++) {
        if(cell_table_arr.ids[i] == cell->id) {
            tsc_celltable *table = cell_table_arr.tables[i];
            cell->celltable = table;
            return table;
        }
    }

    // Table not found.
    return NULL;
}

size_t tsc_cell_getTableFlags(tsc_cell *cell) {
    tsc_celltable *table = tsc_cell_getTable(cell);
    if(table == NULL) return 0;
    return table->flags;
}

int tsc_cell_canMove(tsc_grid *grid, tsc_cell *cell, int x, int y, char dir, const char *forceType, double force) {
    if(cell->id == builtin.wall) return 0;
    if(cell->id == builtin.slide) return  dir % 2 == cell->rot % 2;

    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return 1;
    if(celltable->canMove == NULL) return 1;
    return celltable->canMove(grid, cell, x, y, dir, forceType, force, celltable->payload);
}

float tsc_cell_getBias(tsc_grid *grid, tsc_cell *cell, int x, int y, char dir, const char *forceType, double force) {
    if(cell->id == builtin.mover && tsc_streql(forceType, "push")) {
        if(cell->rot == dir) return 1;
        if((cell->rot + 2) % 4 == dir) return -1;

        return 0;
    }

    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return 0;
    if(celltable->getBias == NULL) return 0;
    return celltable->getBias(grid, cell, x, y, dir, forceType, force, celltable->payload);
}

int tsc_cell_canGenerate(tsc_grid *grid, tsc_cell *cell, int x, int y, tsc_cell *generator, int gx, int gy, char dir) {
    // Can't generate air
    if(cell->id == builtin.empty) return 0;
    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return 1;
    if(celltable->canGenerate == NULL) return 1;
    return celltable->canGenerate(grid, cell, x, y, generator, gx, gy, dir, celltable->payload);
}

int tsc_cell_isTrash(tsc_grid *grid, tsc_cell *cell, int x, int y, char dir, const char *forceType, double force, tsc_cell *eating) {
    if(cell->id == builtin.trash) return 1;
    if(cell->id == builtin.enemy) return 1;
    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return 0;
    if(celltable->isTrash == NULL) return 0;
    return celltable->isTrash(grid, cell, x, y, dir, forceType, force, eating, celltable->payload);
}

void tsc_cell_onTrash(tsc_grid *grid, tsc_cell *cell, int x, int y, char dir, const char *forceType, double force, tsc_cell *eating) {
    if(cell->id == builtin.enemy) {
        tsc_cell empty = tsc_cell_create(builtin.empty, 0);
        tsc_grid_set(grid, x, y, &empty);
        tsc_sound_play(builtin.audio.explosion);
    }
    if(cell->id == builtin.trash) {
        tsc_sound_play(builtin.audio.destroy);
    }
    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return;
    if(celltable->onTrash == NULL) return;
    return celltable->onTrash(grid, cell, x, y, dir, forceType, force, eating, celltable->payload);
}

int tsc_cell_isAcid(tsc_grid *grid, tsc_cell *cell, char dir, const char *forceType, double force, tsc_cell *dissolving, int dx, int dy) {
    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return 0;
    if(celltable->onAcid == NULL) return 0;
    return celltable->isAcid(grid, cell, dir, forceType, force, dissolving, dx, dy, celltable->payload);
}

void tsc_cell_onAcid(tsc_grid *grid, tsc_cell *cell, char dir, const char *forceType, double force, tsc_cell *dissolving, int dx, int dy) {
    tsc_celltable *celltable = tsc_cell_getTable(cell);
    if(celltable == NULL) return;
    if(celltable->onAcid == NULL) return;
    return celltable->onAcid(grid, cell, dir, forceType, force, dissolving, dx, dy, celltable->payload);
}

char *tsc_cell_signal(tsc_cell *cell, int x, int y, const char *protocol, const char *data, tsc_cell *sender, int sx, int sy) {
    tsc_celltable *table = tsc_cell_getTable(cell);
    if(table == NULL) return NULL;
    if(table->signal == NULL) return NULL;
    return table->signal(cell, x, y, protocol, data, sender, sx, sy, table->payload);
}
