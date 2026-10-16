#ifndef PXA_WAMR_AOT_READER_H
#define PXA_WAMR_AOT_READER_H

#include "pxa/wamr/pxa_wamr_engine.h"
#include "wasm_export.h"

/* Internal loader bridge. A custom section can borrow its source in WAMR:
 * report buffered=1 before allocating anything and use the existing loader.
 * Ordinary six-section AOT needs no retained source allocation. */
pxa_status_t pxa_wamr_read_exact(pxa_wamr_artifact_reader_t *reader,
                                uint64_t offset, uint8_t *output, size_t size);
pxa_status_t pxa_wamr_read_aot(pxa_wamr_artifact_reader_t *reader,
                              wasm_module_t *module, int *buffered,
                              char *error, uint32_t error_size);
#endif
