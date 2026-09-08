#include "pxa/net.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static int host_character(uint8_t value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '.' || value == '-';
}

static int numeric_host(const uint8_t *host, size_t size) {
    size_t index;
    if (host == NULL || size == 0) return 0;
    for (index = 0; index < size; ++index) {
        if ((host[index] < '0' || host[index] > '9') && host[index] != '.') {
            return 0;
        }
    }
    return 1;
}

static int ipv4_host(const uint8_t *host, size_t size) {
    size_t index = 0;
    uint8_t segment;
    if (host == NULL || size < 7u || size > 15u) return 0;
    for (segment = 0; segment < 4u; ++segment) {
        uint32_t value = 0;
        size_t digits = 0;
        if (index >= size || host[index] < '0' || host[index] > '9') return 0;
        while (index < size && host[index] >= '0' && host[index] <= '9') {
            if (digits == 3u) return 0;
            value = value * 10u + (uint32_t)(host[index++] - '0');
            ++digits;
        }
        if (value > 255u || (digits > 1u && host[index - digits] == '0'))
            return 0;
        if (segment + 1u == 4u) return index == size;
        if (index >= size || host[index++] != '.') return 0;
    }
    return 0;
}

static int hex_character(uint8_t value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'F') ||
           (value >= 'a' && value <= 'f');
}

static int header_token_character(uint8_t value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '!' || value == '#' ||
           value == '$' || value == '%' || value == '&' || value == '\'' ||
           value == '*' || value == '+' || value == '-' || value == '.' ||
           value == '^' || value == '_' || value == '`' || value == '|' ||
           value == '~';
}

static int bytes_equal_literal(pxa_bytes_t value, const char *literal) {
    size_t size = strlen(literal);
    return value.size == size && memcmp(value.data, literal, size) == 0;
}

int pxa_net_header_name_valid(pxa_bytes_t name, int request_header) {
    size_t index;
    if (name.data == NULL || name.size == 0 ||
        name.size > PXA_NET_MAX_HEADER_NAME_BYTES) {
        return 0;
    }
    for (index = 0; index < name.size; ++index) {
        if (!header_token_character(name.data[index])) return 0;
    }
    if (!request_header) return 1;
    return !bytes_equal_literal(name, "connection") &&
           !bytes_equal_literal(name, "content-length") &&
           !bytes_equal_literal(name, "host") &&
           !bytes_equal_literal(name, "proxy-connection") &&
           !bytes_equal_literal(name, "te") &&
           !bytes_equal_literal(name, "trailer") &&
           !bytes_equal_literal(name, "transfer-encoding") &&
           !bytes_equal_literal(name, "upgrade");
}

int pxa_net_header_value_valid(pxa_bytes_t value) {
    size_t index;
    if ((value.data == NULL && value.size != 0) ||
        value.size > PXA_NET_MAX_HEADER_VALUE_BYTES) {
        return 0;
    }
    for (index = 0; index < value.size; ++index) {
        if (value.data[index] != '\t' &&
            (value.data[index] < UINT8_C(0x20) ||
             value.data[index] > UINT8_C(0x7e))) {
            return 0;
        }
    }
    return 1;
}

static int parse_url(pxa_bytes_t url, pxa_bytes_t *origin,
                     const uint8_t *prefix, size_t prefix_size,
                     int allow_numeric_host) {
    size_t authority_start = prefix_size;
    size_t authority_end;
    size_t colon = SIZE_MAX;
    size_t index;
    size_t host_end;
    size_t label_start;
    if (origin == NULL) return 0;
    origin->data = NULL;
    origin->size = 0;
    if (url.data == NULL || url.size <= authority_start ||
        url.size > PXA_NET_MAX_URL_BYTES ||
        memcmp(url.data, prefix, authority_start) != 0) {
        return 0;
    }
    authority_end = url.size;
    for (index = authority_start; index < url.size; ++index) {
        if (url.data[index] == '/' || url.data[index] == '?') {
            authority_end = index;
            break;
        }
    }
    if (authority_end == authority_start) return 0;
    for (index = authority_start; index < authority_end; ++index) {
        if (url.data[index] == ':') {
            if (colon != SIZE_MAX) return 0;
            colon = index;
        }
    }
    host_end = colon == SIZE_MAX ? authority_end : colon;
    if (host_end == authority_start || host_end - authority_start > 253u ||
        url.data[authority_start] == '.' || url.data[host_end - 1] == '.' ||
        url.data[authority_start] == '-' || url.data[host_end - 1] == '-') {
        return 0;
    }
    label_start = authority_start;
    for (index = authority_start; index < host_end; ++index) {
        if (!host_character(url.data[index])) return 0;
        if (url.data[index] == '.') {
            size_t label_size = index - label_start;
            if (label_size == 0 || label_size > 63u ||
                url.data[label_start] == '-' || url.data[index - 1u] == '-') {
                return 0;
            }
            label_start = index + 1u;
        }
    }
    if (host_end - label_start == 0 || host_end - label_start > 63u ||
        url.data[label_start] == '-' || url.data[host_end - 1u] == '-') {
        return 0;
    }
    if (numeric_host(url.data + authority_start, host_end - authority_start)) {
        if (!allow_numeric_host ||
            !ipv4_host(url.data + authority_start, host_end - authority_start)) {
            return 0;
        }
    }
    if (colon != SIZE_MAX) {
        uint32_t port = 0;
        size_t digits = authority_end - colon - 1u;
        if (digits == 0 || digits > 5 ||
            (digits > 1 && url.data[colon + 1u] == '0'))
            return 0;
        for (index = colon + 1u; index < authority_end; ++index) {
            if (url.data[index] < '0' || url.data[index] > '9') return 0;
            port = port * 10u + (uint32_t)(url.data[index] - '0');
        }
        if (port == 0 || port > 65535u) return 0;
    }
    for (index = authority_end; index < url.size; ++index) {
        uint8_t value = url.data[index];
        if (value < UINT8_C(0x21) || value > UINT8_C(0x7e) || value == '#' ||
            value == '\\') {
            return 0;
        }
        if (value == '%' &&
            (index + 2u >= url.size || !hex_character(url.data[index + 1u]) ||
             !hex_character(url.data[index + 2u])))
            return 0;
    }
    origin->data = url.data;
    origin->size = authority_end;
    return 1;
}

int pxa_net_parse_https_url(pxa_bytes_t url, pxa_bytes_t *origin) {
    static const uint8_t prefix[] = "https://";
    return parse_url(url, origin, prefix, sizeof(prefix) - 1u, 0);
}

int pxa_net_parse_web_url(pxa_bytes_t url, pxa_bytes_t *origin) {
    static const uint8_t https_prefix[] = "https://";
    static const uint8_t http_prefix[] = "http://";
    if (url.data == NULL) return 0;
    if (url.size >= sizeof(https_prefix) - 1u &&
        memcmp(url.data, https_prefix, sizeof(https_prefix) - 1u) == 0) {
        return parse_url(url, origin, https_prefix, sizeof(https_prefix) - 1u,
                         0);
    }
    if (url.size >= sizeof(http_prefix) - 1u &&
        memcmp(url.data, http_prefix, sizeof(http_prefix) - 1u) == 0) {
        return parse_url(url, origin, http_prefix, sizeof(http_prefix) - 1u,
                         1);
    }
    if (origin != NULL) {
        origin->data = NULL;
        origin->size = 0;
    }
    return 0;
}
