#ifndef FIJKPLAYER_PACKET_QUEUE_H
#define FIJKPLAYER_PACKET_QUEUE_H

#include <pthread.h>
#include "ffmpeg_demuxer.h"

/**
 * Thread-safe packet queue for audio/video packets
 * Based on ijkplayer's packet_queue architecture
 */

typedef struct PacketList {
    FFPacket* pkt;
    struct PacketList* next;
} PacketList;

typedef struct PacketQueue {
    PacketList* first_pkt;
    PacketList* last_pkt;
    int nb_packets;       // Number of packets in queue
    int size;            // Total byte size of packets
    int abort_request;   // Flag to abort blocking operations
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} PacketQueue;

/**
 * Initialize packet queue
 * Returns 0 on success, negative on error
 */
int packet_queue_init(PacketQueue* q);

/**
 * Put a packet into the queue
 * Returns 0 on success, negative on error
 */
int packet_queue_put(PacketQueue* q, FFPacket* pkt);

/**
 * Get a packet from the queue (blocking)
 * Returns 0 on success, negative on error (abort/EOF)
 * Caller is responsible for freeing the packet with ff_packet_free()
 */
int packet_queue_get(PacketQueue* q, FFPacket** pkt, int block);

/**
 * Flush all packets from queue
 */
void packet_queue_flush(PacketQueue* q);

/**
 * Abort any blocking operations
 */
void packet_queue_abort(PacketQueue* q);

/**
 * Destroy queue and free resources
 */
void packet_queue_destroy(PacketQueue* q);

/**
 * Get number of packets in queue
 */
int packet_queue_get_nb_packets(PacketQueue* q);

#endif // FIJKPLAYER_PACKET_QUEUE_H
