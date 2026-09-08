#ifndef PXA_MANIFEST_INTERNAL_H
#define PXA_MANIFEST_INTERNAL_H

#include "pxa/package.h"

typedef struct {
    pxa_package_manifest_t *manifest;
    const pxa_package_limits_t *limits;
} pxa_manifest_parser_t;

pxa_status_t pxa_manifest_decode_record(
    pxa_manifest_parser_t *parser, const pxa_record_view_t *record,
    uint32_t *singleton_seen);
pxa_status_t pxa_manifest_decode_finish(
    pxa_manifest_parser_t *parser, uint32_t singleton_seen);

#endif
