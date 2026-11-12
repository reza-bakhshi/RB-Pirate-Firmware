/* Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT */
#include <assert.h>
#include "pirate/hstx_dvi.h"
int main(void) {
    assert(!hstx_dvi_init());
    assert(!hstx_dvi_is_supported());
    hstx_dvi_enable(true);
    assert(!hstx_dvi_is_enabled());
    hstx_dvi_service();
    assert(!hstx_dvi_init());
    return 0;
}
