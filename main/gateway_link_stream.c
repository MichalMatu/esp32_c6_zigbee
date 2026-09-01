#include "gateway_link_stream.h"

#include <string.h>

void gateway_link_stream_init(gateway_link_stream_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

gateway_link_stream_event_t gateway_link_stream_feed(
    gateway_link_stream_decoder_t *decoder,
    uint8_t byte,
    gateway_link_frame_t *frame,
    gateway_link_result_t *decode_result)
{
    if (decoder == NULL || frame == NULL || decode_result == NULL) {
        return GATEWAY_LINK_STREAM_DROPPED;
    }

    if (byte != 0U) {
        if (!decoder->overflow) {
            if (decoder->length < sizeof(decoder->encoded)) {
                decoder->encoded[decoder->length++] = byte;
            } else {
                decoder->overflow = true;
            }
        }
        return GATEWAY_LINK_STREAM_NONE;
    }

    if (decoder->overflow) {
        decoder->length = 0U;
        decoder->overflow = false;
        *decode_result = GATEWAY_LINK_BUFFER_TOO_SMALL;
        return GATEWAY_LINK_STREAM_DROPPED;
    }
    if (decoder->length == 0U) {
        return GATEWAY_LINK_STREAM_NONE;
    }

    *decode_result = gateway_link_decode_frame(decoder->encoded, decoder->length, frame);
    decoder->length = 0U;
    return *decode_result == GATEWAY_LINK_OK ?
        GATEWAY_LINK_STREAM_FRAME : GATEWAY_LINK_STREAM_DROPPED;
}
