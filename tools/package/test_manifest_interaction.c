/* SPDX-License-Identifier: MIT */
#include "pxa/package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file=fopen(argv[1],"rb");assert(file);
    unsigned char encoded[16384];size_t size=fread(encoded,1,sizeof(encoded),file);
    assert(size && !ferror(file) && fgetc(file)==EOF);fclose(file);
    pxa_package_limits_t limits;pxa_package_limits_init(&limits);
    size_t capacity=pxa_package_manifest_workspace_size(&limits);
    void *workspace=malloc(capacity);assert(workspace);
    pxa_package_manifest_t *manifest=NULL;
    assert(pxa_package_manifest_parse(workspace,capacity,(pxa_bytes_t){encoded,size},&limits,&manifest)==PXA_STATUS_OK);
    assert(manifest && manifest->component_count==1);
    free(workspace);
    puts("Existing Core accepts signed optional interaction extension");
    return 0;
}
