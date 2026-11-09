#include "pxa/display_rotation.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint32_t shapes[][2] = {{2,3},{3,2},{3,3},{1,1},{480,854},{854,480}};
    for (size_t s = 0; s < sizeof(shapes)/sizeof(shapes[0]); ++s) {
        const uint32_t w = shapes[s][0], h = shapes[s][1];
        for (uint8_t r = 0; r < 4; ++r) {
            const uint32_t lw = pxa_display_logical_width(w,h,r);
            const uint32_t lh = pxa_display_logical_height(w,h,r);
            for (uint32_t y = 0; y < lh; ++y) for (uint32_t x = 0; x < lw; ++x) {
                uint32_t nx,ny,lx,ly;
                pxa_display_logical_to_native(w,h,r,x,y,&nx,&ny);
                assert(nx<w && ny<h);
                pxa_display_native_to_logical(w,h,r,nx,ny,&lx,&ly);
                assert(lx==x && ly==y);
            }
            for (uint8_t request = 0; request <= PXA_ORIENTATION_LOCKED; ++request) {
                uint8_t actual, again;
                assert(pxa_display_resolve_rotation(w,h,0,r,request,15,&actual));
                assert(actual<4);
                assert(pxa_display_resolve_rotation(w,h,0,actual,request,15,&again));
                assert(again==actual);
                if (request>=1 && request<=4) {
                    if (w==h) assert(actual==r);
                    else {
                        const uint8_t shape=pxa_display_shape(
                            pxa_display_logical_width(w,h,actual),
                            pxa_display_logical_height(w,h,actual));
                        assert(shape==((request==1 || request==3)?1:2));
                    }
                }
            }
        }
    }
    uint8_t r=42;
    assert(!pxa_display_resolve_rotation(480,854,0,0,PXA_ORIENTATION_LANDSCAPE,1,&r) && r==42);
    assert(pxa_display_resolve_rotation(480,854,0,0,PXA_ORIENTATION_LANDSCAPE,9,&r) && r==3);
    assert(!pxa_display_resolve_rotation(0,854,0,0,0,15,&r));
    assert(!pxa_display_resolve_rotation(480,854,0,0,11,15,&r));
    const uint16_t source[]={1,2,3,4,5,6};
    uint16_t native[6];
    pxa_display_rgb565_to_native(source,native,2,3,1);
    const uint16_t expected[]={4,1,5,2,6,3};
    for (unsigned i=0;i<6;++i) assert(native[i]==expected[i]);
    const float device[]={2,3,4}; float view[3];
    const float rotated[][3]={{2,3,4},{-3,2,4},{-2,-3,4},{3,-2,4}};
    for (r=0;r<4;++r) {
        pxa_display_motion_to_screen(device,r,view);
        for (unsigned i=0;i<3;++i) assert(view[i]==rotated[r][i]);
    }
    puts("display rotation: tall/wide/square, all requests, idempotence, supported masks, every pixel inverse, RGB565 and motion axes passed");
    return 0;
}
