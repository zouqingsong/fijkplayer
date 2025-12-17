#ifndef SURFACE_TEXTURE_JNI_H
#define SURFACE_TEXTURE_JNI_H

#include <stdbool.h>

/**
 * Wait for frame update from SurfaceTexture
 * Returns true if frame was updated, false if timeout
 */
bool wait_for_frame_update(int timeout_ms);

/**
 * Reset frame update state
 */
void reset_frame_update_state(void);

#endif // SURFACE_TEXTURE_JNI_H
