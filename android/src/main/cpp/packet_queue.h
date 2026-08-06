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
 * (Re)activate the queue for use after an abort/stop: clears the abort flag so
 * packet_queue_put/get work again. Must be called before restarting playback.
 */
void packet_queue_start(PacketQueue* q);

/**
 * Live-latency guard: if the queue holds more than max_packets, drop the oldest
 * packets up to (but keeping) the most recent keyframe, so a slow decoder cannot
 * accumulate latency over time. Does nothing if the queue is within budget or if
 * there is no newer keyframe to jump to.
 * Returns the number of packets dropped.
 */
int packet_queue_drop_to_latest_keyframe(PacketQueue* q, int max_packets);

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
