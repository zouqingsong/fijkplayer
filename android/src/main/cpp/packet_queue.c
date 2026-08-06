#include "packet_queue.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "PacketQueue"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

int packet_queue_init(PacketQueue* q) {
    if (!q) {
        return -1;
    }
    
    memset(q, 0, sizeof(PacketQueue));
    
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        LOGE("Failed to initialize queue mutex");
        return -1;
    }
    
    if (pthread_cond_init(&q->cond, NULL) != 0) {
        LOGE("Failed to initialize queue condition variable");
        pthread_mutex_destroy(&q->mutex);
        return -1;
    }
    
    q->abort_request = 0;
    
    return 0;
}

int packet_queue_put(PacketQueue* q, FFPacket* pkt) {
    if (!q || !pkt) {
        return -1;
    }
    
    pthread_mutex_lock(&q->mutex);
    
    if (q->abort_request) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    
    // Allocate new list node
    PacketList* pkt_node = (PacketList*)malloc(sizeof(PacketList));
    if (!pkt_node) {
        pthread_mutex_unlock(&q->mutex);
        LOGE("Failed to allocate packet list node");
        return -1;
    }
    
    pkt_node->pkt = pkt;
    pkt_node->next = NULL;
    
    // Add to queue tail
    if (!q->last_pkt) {
        q->first_pkt = pkt_node;
    } else {
        q->last_pkt->next = pkt_node;
    }
    q->last_pkt = pkt_node;
    q->nb_packets++;
    q->size += pkt->size;
    
    // Signal waiting threads
    pthread_cond_signal(&q->cond);
    
    pthread_mutex_unlock(&q->mutex);
    
    return 0;
}

int packet_queue_get(PacketQueue* q, FFPacket** pkt, int block) {
    if (!q || !pkt) {
        return -1;
    }
    
    pthread_mutex_lock(&q->mutex);
    
    while (1) {
        if (q->abort_request) {
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
        
        PacketList* pkt_node = q->first_pkt;
        if (pkt_node) {
            // Remove from queue head
            q->first_pkt = pkt_node->next;
            if (!q->first_pkt) {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt_node->pkt->size;
            
            *pkt = pkt_node->pkt;
            free(pkt_node);
            
            pthread_mutex_unlock(&q->mutex);
            return 0;
        } else if (!block) {
            // Non-blocking mode and no packets available
            pthread_mutex_unlock(&q->mutex);
            return -1;
        } else {
            // Wait for packets
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }
}

void packet_queue_flush(PacketQueue* q) {
    if (!q) {
        return;
    }
    
    pthread_mutex_lock(&q->mutex);
    
    PacketList* pkt = q->first_pkt;
    while (pkt) {
        PacketList* next = pkt->next;
        if (pkt->pkt) {
            ff_packet_free(pkt->pkt);
        }
        free(pkt);
        pkt = next;
    }
    
    q->first_pkt = NULL;
    q->last_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    
    pthread_mutex_unlock(&q->mutex);
}

int packet_queue_drop_to_latest_keyframe(PacketQueue* q, int max_packets) {
    if (!q || max_packets < 1) {
        return 0;
    }

    pthread_mutex_lock(&q->mutex);

    if (q->nb_packets <= max_packets) {
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    // Find the newest keyframe currently in the queue.
    PacketList* last_key = NULL;
    for (PacketList* n = q->first_pkt; n; n = n->next) {
        if (n->pkt && n->pkt->is_key_frame) {
            last_key = n;
        }
    }

    // Nothing to jump to, or the queue already starts at the newest keyframe.
    if (!last_key || last_key == q->first_pkt) {
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    // Drop everything before the newest keyframe.
    int dropped = 0;
    PacketList* n = q->first_pkt;
    while (n && n != last_key) {
        PacketList* next = n->next;
        q->nb_packets--;
        if (n->pkt) {
            q->size -= n->pkt->size;
            ff_packet_free(n->pkt);
        }
        free(n);
        n = next;
        dropped++;
    }
    q->first_pkt = last_key;

    pthread_mutex_unlock(&q->mutex);

    if (dropped > 0) {
        LOGW("Dropped %d stale video packets to latest keyframe (backlog guard)", dropped);
    }
    return dropped;
}

void packet_queue_abort(PacketQueue* q) {
    if (!q) {
        return;
    }
    
    pthread_mutex_lock(&q->mutex);
    q->abort_request = 1;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

void packet_queue_start(PacketQueue* q) {
    if (!q) {
        return;
    }
    
    pthread_mutex_lock(&q->mutex);
    q->abort_request = 0;
    pthread_mutex_unlock(&q->mutex);
}

void packet_queue_destroy(PacketQueue* q) {
    if (!q) {
        return;
    }
    
    packet_queue_flush(q);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

int packet_queue_get_nb_packets(PacketQueue* q) {
    if (!q) {
        return 0;
    }
    
    pthread_mutex_lock(&q->mutex);
    int nb = q->nb_packets;
    pthread_mutex_unlock(&q->mutex);
    
    return nb;
}
