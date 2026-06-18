#pragma once
#include <stdint.h>
#include <string.h>

/* Dragonfly/Bedrock → SHANKPIT block ID mapping.
 * Matches the Go constants in packages2/common/block_map.go.
 * block_id 0 == air (skip rendering). */

/* SHANKPIT canonical block IDs (must stay in sync with protocol.h VOXEL_BLOCK_*) */
#define BLOCK_STONE  1
#define BLOCK_GRASS  2
#define BLOCK_DIRT   3
#define BLOCK_LOG    17
#define BLOCK_LEAF   18

typedef struct { const char *name; uint16_t block_id; } block_map_entry_t;

static const block_map_entry_t block_map_table[] = {
    { "minecraft:stone",              BLOCK_STONE },
    { "minecraft:cobblestone",        BLOCK_STONE },
    { "minecraft:stone_bricks",       BLOCK_STONE },
    { "minecraft:mossy_cobblestone",  BLOCK_STONE },
    { "minecraft:grass",              BLOCK_GRASS },
    { "minecraft:grass_block",        BLOCK_GRASS },
    { "minecraft:dirt",               BLOCK_DIRT  },
    { "minecraft:podzol",             BLOCK_DIRT  },
    { "minecraft:coarse_dirt",        BLOCK_DIRT  },
    { "minecraft:oak_log",            BLOCK_LOG   },
    { "minecraft:birch_log",          BLOCK_LOG   },
    { "minecraft:spruce_log",         BLOCK_LOG   },
    { "minecraft:jungle_log",         BLOCK_LOG   },
    { "minecraft:acacia_log",         BLOCK_LOG   },
    { "minecraft:dark_oak_log",       BLOCK_LOG   },
    { "minecraft:log",                BLOCK_LOG   },
    { "minecraft:oak_leaves",         BLOCK_LEAF  },
    { "minecraft:birch_leaves",       BLOCK_LEAF  },
    { "minecraft:spruce_leaves",      BLOCK_LEAF  },
    { "minecraft:jungle_leaves",      BLOCK_LEAF  },
    { "minecraft:acacia_leaves",      BLOCK_LEAF  },
    { "minecraft:dark_oak_leaves",    BLOCK_LEAF  },
    { "minecraft:leaves",             BLOCK_LEAF  },
    { "minecraft:leaves2",            BLOCK_LEAF  },
    { NULL, 0 }
};

/* Returns SHANKPIT block ID for a Dragonfly block name, 0 (air) if not mapped. */
static inline uint16_t dragonfly_name_to_block_id(const char *name) {
    for (int i = 0; block_map_table[i].name != NULL; i++) {
        if (strcmp(block_map_table[i].name, name) == 0)
            return block_map_table[i].block_id;
    }
    return 0;
}
