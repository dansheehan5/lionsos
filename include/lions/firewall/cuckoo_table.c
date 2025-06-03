#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>


/* This is just to ensure I got implementing the operations of a hash table correctly 
   When done, will modify it so it supports the arp table entry */

uint32_t TABLE_CAPACITY = 22; /* if this isn't double a prime i will be very unhappy */
uint32_t MAX_ITERS = 11;

typedef struct hash_table {
    uint16_t (*hash)(uint16_t);
    uint16_t entries[11];
} hash_table_t;

typedef struct cuckoo_table {
    hash_table_t tables[2];
    uint32_t capacity;
} cuckoo_table_t;

uint16_t hash1(uint16_t key) {
    return key % 11;
}

uint16_t hash2(uint16_t key) {
    return (key / 11) % 11;
}

uint16_t *get_entry(cuckoo_table_t *cuckoo_table, uint16_t *entry) {
    hash_table_t t1 = cuckoo_table->tables[0];
    uint16_t (*h1)(uint16_t) = t1.hash;

    uint16_t *found = &t1.entries[h1(*entry)];
    
    if (found != NULL && found == entry) {
        return found;
    }

    hash_table_t t2 = cuckoo_table->tables[1];
    uint16_t (*h2)(uint16_t) = t2.hash;

    found = &t2.entries[h2(*entry)];

    if (found != NULL && found == entry) {
        return found;
    }
    // TODO: Error message
    return NULL;
}

void evict(cuckoo_table_t *cuckoo_table) {

}

void insert(cuckoo_table_t *cuckoo_table, uint16_t *entry) {
    if (get_entry(cuckoo_table, entry) != NULL) {
        return;
    }
    
    printf("bla\n");

    hash_table_t t1 = cuckoo_table->tables[0];
    hash_table_t t2 = cuckoo_table->tables[1];
    printf("yabadabado\n");

    uint16_t *x = entry;
    
    uint16_t h1 = t1.hash(*x);
    uint16_t h2 = t2.hash(*x);

    for (int i = 0; i < MAX_ITERS; i++) {
        if (&t1.entries[h1] == NULL) {
            t1.entries[h1] = *x;
            return;
        }

        uint16_t *tmp = x;
        x = &t1.entries[h1];
        t1.entries[h1] = *tmp;

        if (&t2.entries[h2] == NULL) {
            t2.entries[h2] = *x;
            return;
        }

        tmp = x;
        x = &t2.entries[h2];
        t2.entries[h2] = *tmp;
    }

    evict(cuckoo_table);
    insert(cuckoo_table, entry);
}

void update(cuckoo_table_t *cuckoo_table, uint16_t *new_entry) {
    uint16_t *old_entry = get_entry(cuckoo_table, new_entry);
    
    if (old_entry == NULL) {
        insert(cuckoo_table, new_entry);
    }



}

void init_ht(hash_table_t *t, uint16_t (*hash)(uint16_t)) {
    t->hash = hash;
}

void init_tbl(cuckoo_table_t *table) {
    table->capacity = TABLE_CAPACITY;

    hash_table_t *t1 = malloc(sizeof(hash_table_t));
    hash_table_t *t2 = malloc(sizeof(hash_table_t));

    init_ht(t1, &hash1);
    init_ht(t2, &hash2);

    table->tables[0] = *t1;
    table->tables[1] = *t2;
}



int main(int argc, char *argv[]) {

    cuckoo_table_t *test = malloc(sizeof(cuckoo_table_t));
    init_tbl(test);

    uint16_t in = 100;

    insert(test, &in);
    printf("%d\n", *(get_entry(test, &in)));



    return 0;
}