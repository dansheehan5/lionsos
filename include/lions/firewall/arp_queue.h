#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/timer/client.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>

typedef enum {
    ARP_ERR_OKAY = 0,   /* No error */
	ARP_ERR_FULL  /* Data structure is full */
} fw_arp_error_t;

typedef enum {
    ARP_STATE_INVALID,                  /* Whether this entry is valid entry in the table */
    ARP_STATE_PENDING,                  /* Whether this entry is still pending a response */
    ARP_STATE_UNREACHABLE,              /* Whether this ip is reachable and listed mac has meaning */
    ARP_STATE_REACHABLE
} fw_arp_entry_state_t;

typedef struct fw_arp_entry {
    fw_arp_entry_state_t state;                    /* State of this entry */
    uint32_t ip;                                /* IP of entry */
    uint8_t mac_addr[ETH_HWADDR_LEN];           /* MAC address of IP */
    uint8_t client;                             /* Bitmap of clients that initiated the request */
    uint8_t num_retries;                        /* Number of times we have sent out an arp request */
    uint64_t timestamp;                         /* Time of insertion */
} fw_arp_entry_t;

typedef struct fw_arp_hash {
    uint16_t (*hash)(uint32_t, uint16_t);
    fw_arp_entry_t *entries;
    uint16_t capacity;
} fw_arp_hash_t;

typedef struct fw_arp_table {
    fw_arp_hash_t tables[2];
    uint16_t capacity;
} fw_arp_table_t;

typedef struct fw_arp_request {
    uint32_t ip;                        /* Requested IP */
    uint8_t mac_addr[ETH_HWADDR_LEN];   /* Zero filled or MAC of IP */
    fw_arp_entry_state_t state;            /* State of this ARP entry */
} fw_arp_request_t;

typedef struct fw_arp_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
   /* arp array */
    fw_arp_request_t queue[FW_MAX_ARP_QUEUE_CAPACITY];
} fw_arp_queue_t;

typedef struct fw_arp_queue_handle {
    /* arp requests */
    fw_arp_queue_t request;
    /* responses to arp requests */
    fw_arp_queue_t response;
    /* capacity of the queues */
    uint32_t capacity;
} fw_arp_queue_handle_t;

/* Hash functions for use in the cuckoo table*/
static uint16_t hash1(uint32_t ip, uint16_t table_size) {
    return (uint16_t)(ip % table_size);
}

static uint16_t hash2(uint32_t ip, uint16_t table_size) {
    return (uint16_t)((ip * table_size) % table_size);
}

static void fw_arp_hash_init(fw_arp_hash_t *table,
                             fw_arp_entry_t *entries,
                             uint16_t capacity,
                             uint16_t (*hash)(uint32_t, uint16_t)) {
    table->entries = entries;
    table->capacity = capacity;
    table->hash = hash;
}

/* Find an arp entry for an IP */
static fw_arp_entry_t *fw_arp_table_find_entry(fw_arp_table_t *table, uint32_t ip) {
    fw_arp_hash_t *t1 = &(table->tables[0]);
    uint16_t h1 = t1->hash(ip, t1->capacity);

    fw_arp_entry_t *entry = t1->entries + h1;

    if (entry->ip == ip && entry->state != ARP_STATE_INVALID) {
        return entry;
    }

    fw_arp_hash_t *t2 = &(table->tables[1]);
    uint16_t h2 = t2->hash(ip, t2->capacity);

    entry = t2->entries + h2;

    if (entry->ip == ip && entry->state != ARP_STATE_INVALID) {
        return entry;
    }

    return NULL;
}

/* Create an arp response from an arp entry */
static fw_arp_request_t fw_arp_response_from_entry(fw_arp_entry_t *entry) {
    fw_arp_request_t response = {entry->ip, {0}, entry->state};
    if (entry->state == ARP_STATE_REACHABLE) {
        memcpy(&response.mac_addr, &entry->mac_addr, ETH_HWADDR_LEN);
    }
    return response;
}

static fw_arp_error_t fw_arp_table_add_entry(fw_arp_table_t *table,
                                       uint8_t timer_ch,
                                       fw_arp_entry_state_t state,
                                       uint32_t ip,
                                       uint8_t *mac_addr,
                                       uint8_t client)
{
    uint8_t t_time = timer_ch;
    fw_arp_entry_state_t t_state = state;
    uint32_t t_ip = ip;
    uint8_t *t_mac = mac_addr;
    uint8_t t_client = client;

    fw_arp_entry_t *slot = fw_arp_table_find_entry(table, t_ip);
    if (slot != NULL) {
        return ARP_ERR_OKAY;
    }

    fw_arp_entry_t tmp0;
    fw_arp_entry_t tmp1;
    tmp1.ip = ip;
    for (uint16_t i = 0; i < table->capacity; i++) {

        /* Check for cycles */
        fw_arp_hash_t *t1 = &(table->tables[0]);
        uint16_t h1 = t1->hash(tmp1.ip, t1->capacity);

        /* Check if the first hash table slot for the IP is in use */
        slot = t1->entries + h1;
        memset(&tmp0, 0,sizeof(fw_arp_entry_t));
        if (slot->state != ARP_STATE_INVALID) {
            /* Store what's in the valid slot*/
            memcpy(&tmp0, slot, sizeof(fw_arp_entry_t));
        }

        /* Insert into hash table slot */
        memcpy(slot, &tmp1, sizeof(fw_arp_entry_t));

        if (tmp0.state == ARP_STATE_INVALID) {
            return ARP_ERR_OKAY;
        }

        fw_arp_hash_t *t2 = &(table->tables[1]);
        uint16_t h2 = t2->hash(t_ip, t2->capacity);

        /* Check if the second hash table slot for evicted IP is in use */
        slot = t2->entries + h2;
        memset(&tmp1, 0,sizeof(fw_arp_entry_t));
        if (slot->state != ARP_STATE_INVALID) {
            /* Store what's in the valid slot */
            memcpy(&tmp1, slot, sizeof(fw_arp_entry_t));
        }

        /* Story evicted entry from first hash in second hash table */
        memcpy(slot, &tmp0, sizeof(fw_arp_entry_t));

        if (tmp1.state == ARP_STATE_INVALID) {
            return ARP_ERR_OKAY;
        }

        /* Check for cycles */
        if (t1->hash(tmp1.ip, t1->capacity) == h1) {
            break;
        }
    }

    /* evict entry if it's not what you're trying to add, return */
    /* otherwise, get rid of a random entry and re-insert */
    if (t_ip != ip) {
        return ARP_ERR_OKAY;
    } else {
        bool found = 0;
        while (!found) {
            uint16_t entry_ind = rand() % table->capacity;
            fw_arp_hash_t *t = &(table->tables[entry_ind / (table->capacity / 2)]);
            fw_arp_entry_t *evict = t->entries + (entry_ind % t->capacity);
            if (evict->ip != ip) {
                fw_arp_entry_t **ev_addr = &evict;
                *ev_addr = NULL;
                found = 1;
            }
        }

        fw_arp_table_add_entry(table, timer_ch, state, ip, mac_addr, client);
        return ARP_ERR_OKAY;
    }

}

/* Initialise the arp table data structure */
static void fw_arp_table_init(fw_arp_table_t *table,
    void *entries,
    uint16_t capacity)
{
    uint16_t hash_capacity = capacity / 2;
    fw_arp_hash_init(&table->tables[0], entries, hash_capacity, hash1);
    fw_arp_hash_init(&table->tables[1], entries + sizeof(fw_arp_entry_t) * hash_capacity, hash_capacity, hash2);
    table->capacity = capacity;
}

/**
 * Get the number of requests/responses enqueued into a queue.
 *
 * @param queue queue handle for the queue to get the length of.
 *
 * @return number of queue enqueued into a queue.
 */
static inline uint16_t fw_arp_queue_length(fw_arp_queue_t *queue)
{
    return queue->tail - queue->head;
}

/**
 * Check if the request queue is empty.
 *
 * @param queue queue handle for the request queue to check.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool fw_arp_queue_empty_request(fw_arp_queue_handle_t *queue)
{
    return queue->request.tail - queue->request.head == 0;
}

/**
 * Check if the response queue is empty.
 *
 * @param queue queue handle for the response queue to check.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool fw_arp_queue_empty_response(fw_arp_queue_handle_t *queue)
{
    return queue->response.tail - queue->response.head == 0;
}

/**
 * Check if the request queue is full.
 *
 * @param queue queue handle for the request queue to check.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool fw_arp_queue_full_request(fw_arp_queue_handle_t *queue)
{
    return queue->request.tail - queue->request.head == queue->capacity;
}

/**
 * Check if the response queue is full.
 *
 * @param queue queue handle for the response queue to check.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool fw_arp_queue_full_response(fw_arp_queue_handle_t *queue)
{
    return queue->response.tail - queue->response.head == queue->capacity;
}

/**
 * Enqueue an element into a request queue.
 *
 * @param queue queue to enqueue into.
 * @param request request to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int fw_arp_enqueue_request(fw_arp_queue_handle_t *queue, fw_arp_request_t request)
{
    if (fw_arp_queue_full_request(queue)) {
        return -1;
    }

    memcpy(&queue->request.queue[queue->request.tail % queue->capacity], &request, sizeof(fw_arp_request_t));
    queue->request.tail++;

    return 0;
}

/**
 * Enqueue an element into an response queue.
 *
 * @param queue queue to enqueue into.
 * @param response response to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int fw_arp_enqueue_response(fw_arp_queue_handle_t *queue, fw_arp_request_t response)
{
    if (fw_arp_queue_full_response(queue)) {
        return -1;
    }

    memcpy(&queue->response.queue[queue->response.tail % queue->capacity], &response, sizeof(fw_arp_request_t));
    queue->response.tail++;

    return 0;
}

/**
 * Dequeue an element from the request queue.
 *
 * @param queue queue handle to dequeue from.
 * @param request pointer to request to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int fw_arp_dequeue_request(fw_arp_queue_handle_t *queue, fw_arp_request_t *request)
{
    if (fw_arp_queue_empty_request(queue)) {
        return -1;
    }

    memcpy(request, &queue->request.queue[queue->request.head % queue->capacity], sizeof(fw_arp_request_t));
    queue->request.head++;

    return 0;
}

/**
 * Dequeue an element from the response queue.
 *
 * @param queue queue handle to dequeue from.
 * @param buffer pointer to response to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int fw_arp_dequeue_response(fw_arp_queue_handle_t *queue, fw_arp_request_t *response)
{
    if (fw_arp_queue_empty_response(queue)) {
        return -1;
    }

    memcpy(response, &queue->response.queue[queue->response.head % queue->capacity], sizeof(fw_arp_request_t));
    queue->response.head++;

    return 0;
}

/**
 * Initialise the shared queue.
 *
 * @param queue queue handle to use.
 * @param capacity capacity of the free and active queues.
 */
static inline void fw_arp_handle_init(fw_arp_queue_handle_t *queue, uint32_t capacity)
{
    queue->capacity = capacity;
}
