#include "websocket.h"
#include <assert.h>
#include <stddef.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


#ifdef __GNUC__
# define EXPECTED(X) __builtin_expect(!!(X), 1)
# define UNEXPECTED(X) __builtin_expect(!!(X), 0)
#else
# define EXPECTED(X) (X)
# define UNEXPECTED(X) (X)
#endif

#define OPCODE_MASK 0xF

#define NOTIFY_CB(FOR)                                                 \
do {                                                                   \
  if (settings->on_##FOR) {                                            \
    if (settings->on_##FOR(parser) != 0) {                             \
      return i;                                                        \
    }                                                                  \
  }                                                                    \
} while (0)

#define EMIT_DATA_CB(FOR, ptr, len)                                    \
do {                                                                   \
  if (settings->on_##FOR) {                                            \
    if (settings->on_##FOR(parser, ptr, len) != 0) {                   \
      return i;                                                        \
    }                                                                  \
  }                                                                    \
} while (0)

enum state {
    s_start,
    s_head,
    s_length,
    s_mask,
    s_body,
};

void websocket_parser_init(websocket_parser * parser) {
    void *data = parser->data; /* preserve application data */
    memset(parser, 0, sizeof(*parser));
    parser->data = data;
    parser->state = s_start;
    parser->error = 0;
}

void websocket_parser_settings_init(websocket_parser_settings *settings) {
    memset(settings, 0, sizeof(*settings));
}

size_t websocket_parser_execute(websocket_parser *parser, const websocket_parser_settings *settings, const char *data, size_t len) {
    size_t i = 0;
    size_t r = 0;
    char c;

    while(i < len) {
        c = data[i];
        switch(parser->state) {
            case s_start:
                parser->length = 0;
                parser->flags  = (uint32_t) (c & WS_OP_MASK);
                c >>= 7;
                if(c & 1) {
                    parser->flags |= WS_FIN;
                }
                parser->state = s_head;
                break;
            case s_head:
                parser->length  = (size_t)c & 0x7F;
                if(c & 0x80) {
                    parser->flags |= WS_MASKED;
                }
                if(EXPECTED(parser->length >= 126)) {
                    if(EXPECTED(parser->length == 127)) {
                        if(EXPECTED(len - i > 8)) {
                            parser->length = (uint16_t)data[i+1];
                            i += 8;
                        } else {
                            parser->require = 8;
                            parser->state = s_length;
                        }
                    } else {
                        if(EXPECTED(len - i >= 2)) {
                            parser->length = (uint16_t)data[i+1];
                            i += 2;
                        } else {
                            parser->require = 2;
                        }
                    }
                } else {
                }
                if(EXPECTED(parser->require)) {
                    parser->state = s_length;
                } else {
                    if(EXPECTED(parser->flags & WS_MASKED)) {
                        parser->state = s_mask;
                        parser->require = 4;
                    } else if(parser->require) {
                        parser->require = parser->length;
                        NOTIFY_CB(frame_header);
                        parser->state = s_body;
                    } else {
                        NOTIFY_CB(frame_header);
                        NOTIFY_CB(frame_end);
                        parser->state = s_start;
                    }
                }
                break;
            case s_length:
                for(; i < len, parser->require; i++, parser->require--) {
                    parser->length <<= 8;
                    parser->length |= (unsigned char)data[i];
                }
                if(!UNEXPECTED(parser->require)) {
                    parser->require = parser->length;
                    NOTIFY_CB(frame_header);
                    parser->state = s_body;
                }
                break;
            case s_mask:
                for(; i < len, parser->require; i++, parser->require--) {
                    parser->mask[4 - parser->require] = data[i];
                }
                i--;
                if(!UNEXPECTED(parser->require)) {
                    parser->require = parser->length;
                    NOTIFY_CB(frame_header);
                    parser->state = s_body;
                }
                break;
            case s_body:
                if(parser->require) {
                    r = parser->require;
                    parser->require -= len - i;
                    EMIT_DATA_CB(frame_body, &data[i], len - i);
                    i+=r;
                }
                if(!UNEXPECTED(parser->require)) {
                    NOTIFY_CB(frame_end);
//                    parser->flags |= WS_COMPLETE;
                    parser->state = s_start;
                }
                break;
            default:
                parser->error = ERR_UNKNOWN_STATE;
                return i;
        }
        i++;
    }

    return i;
}

void websocket_parser_apply_mask(const char * src, char * dst, size_t len, websocket_parser * parser) {
    size_t i = 0;
    for(; i < len; i++) {
        dst[i] = src[i] ^ parser->mask[(i + parser->mask_offset) % 4];
    }

    parser->mask_offset = (uint8_t) ((i + parser->mask_offset + 1) % 4);
}