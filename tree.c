// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
//   "<mode-as-ascii-octal> <name>\0<32-byte-binary-hash>"
//
// Example single entry (conceptual):
//   "100644 hello.txt\0" followed by 32 raw bytes of SHA-256
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
// Returns 0 on success, -1 on parse error.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        // 1. Safely find the space character for the mode
        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1; // Malformed data

        // Parse mode into an isolated buffer
        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1; // Skip space

        // 2. Safely find the null terminator for the name
        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1; // Malformed data

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; // Ensure null-terminated

        ptr = null_byte + 1; // Skip null byte

        // 3. Read the 32-byte binary hash
        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Estimate max size: (6 bytes mode + 1 byte space + 256 bytes name + 1 byte null + 32 bytes hash) per entry
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    // Create a mutable copy to sort entries (Git requirement)
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        // Write mode and name (%o writes octal correctly for Git standards)
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 to step over the null terminator written by sprintf
        
        // Write binary hash
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

static int write_tree_recursive(IndexEntry *entries, int count, int depth, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;

    for (int i = 0; i < count; i++) {
        char *slash = strchr(entries[i].path + depth, '/');

        if (slash == NULL) {
            // It's a file in the current directory level
            TreeEntry *e = &tree.entries[tree.count++];
            e->mode = entries[i].mode;
            
            // Get just the filename (everything after the last slash)
            char *filename = strrchr(entries[i].path, '/');
            strncpy(e->name, filename ? filename + 1 : entries[i].path, sizeof(e->name) - 1);
            
            e->hash = entries[i].hash;
        } else {
            // It's a subdirectory. Group all files in this subdirectory.
            char dir_name[256] = {0};
            size_t dir_name_len = slash - (entries[i].path + depth);
            strncpy(dir_name, entries[i].path + depth, dir_name_len);

            // Find how many subsequent entries belong to this same directory
            int sub_count = 0;
            while (i + sub_count < count) {
                if (strncmp(entries[i + sub_count].path + depth, dir_name, dir_name_len) != 0 ||
                    entries[i + sub_count].path[depth + dir_name_len] != '/') {
                    break;
                }
                sub_count++;
            }

            // Recursive call to create the sub-tree object
            TreeEntry *e = &tree.entries[tree.count++];
            e->mode = MODE_DIR;
            strncpy(e->name, dir_name, sizeof(e->name) - 1);
            if (write_tree_recursive(entries + i, sub_count, depth + dir_name_len + 1, &e->hash) != 0) {
                return -1;
            }

            // Skip the entries we just processed in the sub-tree
            i += sub_count - 1;
        }
    }

    // Serialize and write the current tree object to the store
    void *raw_data;
    size_t raw_len;
    if (tree_serialize(&tree, &raw_data, &raw_len) != 0) return -1;
    if (object_write(OBJ_TREE, raw_data, raw_len, id_out) != 0) {
        free(raw_data);
        return -1;
    }
    free(raw_data);
    return 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
//
// HINTS - Useful functions and concepts for this phase:
//   - index_load      : load the staged files into memory
//   - strchr          : find the first '/' in a path to separate directories from files
//   - strncmp         : compare prefixes to group files belonging to the same subdirectory
//   - Recursion       : you will likely want to create a recursive helper function 
//                       (e.g., `write_tree_level(entries, count, depth)`) to handle nested dirs.
//   - tree_serialize  : convert your populated Tree struct into a binary buffer
//   - object_write    : save that binary buffer to the store as OBJ_TREE
//
// Returns 0 on success, -1 on error.
int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) {
        return -1;
    }

    if (index.count == 0) {
        // Handle empty repository case if necessary, 
        // though Git usually doesn't allow empty trees.
        return -1;
    }

    // The index must be sorted for grouping to work. 
    // index_save usually handles this, but we ensure it here.
    // (Assuming compare_entries from previous steps is available)
    // qsort(index.entries, index.count, sizeof(IndexEntry), compare_entries);

    return write_tree_recursive(index.entries, index.count, 0, id_out);
}
