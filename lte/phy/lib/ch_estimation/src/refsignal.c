/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */


#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <complex.h>

#include "liblte/phy/common/phy_common.h"
#include "liblte/phy/ch_estimation/refsignal.h"
#include "liblte/phy/utils/vector.h"
#include "liblte/phy/utils/debug.h"
#include "liblte/phy/common/sequence.h"

#define idx(x, y) (l*nof_refs_x_symbol+i)

int refsignal_v(uint32_t port_id, uint32_t ns, uint32_t symbol_id) {
  int v=-1;
  switch(port_id) {
  case 0:
    if (symbol_id == 0) {
      v=0;
    } else {
      v=3;
    }
    break;
  case 1:
    if (symbol_id == 0) {
      v=3;
    } else {
      v=0;
    }
    break;
  case 2:
    v=3*(ns%2);
    break;
  case 3:
    v=3+3*(ns%2);
    break;
  }
  return v;
}

int refsignal_k(int m, int v, uint32_t cell_id) {
  return 6*m+((v+(cell_id%6))%6);
}

int refsignal_put(refsignal_t *q, cf_t *slot_symbols) {
  int i;
  uint32_t fidx, tidx;
  if (q                 != NULL &&
      slot_symbols      != NULL) 
  {
    for (i=0;i<q->nof_refs;i++) {
      fidx = q->refs[i].freq_idx; // reference frequency index
      tidx = q->refs[i].time_idx; // reference time index
      slot_symbols[SAMPLE_IDX(q->nof_prb, tidx, fidx)] = q->refs[i].simbol;
    }
    return LIBLTE_SUCCESS;
  } else {
    return LIBLTE_ERROR_INVALID_INPUTS;
  }
}

/** Initializes refsignal_t object according to 3GPP 36.211 6.10.1
 *
 */
int refsignal_init_LTEDL(refsignal_t *q, uint32_t port_id, uint32_t nslot,
    lte_cell_t cell) {

  uint32_t c_init;
  uint32_t ns, l, lp[2];
  int N_cp;
  int i;
  int ret = LIBLTE_ERROR_INVALID_INPUTS;
  sequence_t seq;
  int v;
  int mp;
  uint32_t nof_refs_x_symbol, nof_ref_symbols;

  if (q         != NULL          && 
      port_id   < MAX_PORTS      && 
      nslot     < NSLOTS_X_FRAME &&
      lte_cell_isvalid(&cell))
  {
  
    bzero(q, sizeof(refsignal_t));
    bzero(&seq, sizeof(sequence_t));

    if (CP_ISNORM(cell.cp)) {
      N_cp = 1;
    } else {
      N_cp = 0;
    }

    if (port_id < 2) {
      nof_ref_symbols = 2;
      lp[0] = 0;
      lp[1] = CP_NSYMB(cell.cp) - 3;
    } else {
      nof_ref_symbols = 1;
      lp[0] = 1;
    }
    nof_refs_x_symbol = 2 * cell.nof_prb;

    q->nof_refs = nof_refs_x_symbol * nof_ref_symbols;
    q->nsymbols = nof_ref_symbols;
    q->voffset = cell.id%6;
    q->nof_prb = cell.nof_prb;

    q->symbols_ref = malloc(sizeof(uint32_t) * nof_ref_symbols);
    if (!q->symbols_ref) {
      perror("malloc");
      goto free_and_exit;
    }

    memcpy(q->symbols_ref, lp, sizeof(uint32_t) * nof_ref_symbols);

    q->refs = vec_malloc(q->nof_refs * sizeof(ref_t));
    if (!q->refs) {
      goto free_and_exit;
    }
    q->ch_est = vec_malloc(q->nof_refs * sizeof(cf_t));
    if (!q->ch_est) {
      goto free_and_exit;
    }

    ns = nslot;
    for (l = 0; l < nof_ref_symbols; l++) {

      c_init = 1024 * (7 * (ns + 1) + lp[l] + 1) * (2 * cell.id + 1)
          + 2 * cell.id + N_cp;
      ret = sequence_LTEPRS(&seq, 2 * 2 * MAX_PRB, c_init);
      if (ret != LIBLTE_SUCCESS) {
        goto free_and_exit;
      }

      v = refsignal_v(port_id, ns, lp[l]);

      for (i = 0; i < nof_refs_x_symbol; i++) {
        mp = i + MAX_PRB - cell.nof_prb;

        /* generate signal */
        __real__ q->refs[idx(l,i)].simbol = (1 - 2 * (float) seq.c[2 * mp]) / sqrt(2);
        __imag__ q->refs[idx(l,i)].simbol = (1 - 2 * (float) seq.c[2 * mp + 1]) / sqrt(2);

        /* mapping to resource elements */
        q->refs[idx(l,i)].freq_idx = refsignal_k(i, v, cell.id);
        q->refs[idx(l,i)].time_idx = lp[l];
      }
    }
    ret = LIBLTE_SUCCESS;
  }
free_and_exit:
  if (ret != LIBLTE_ERROR_INVALID_INPUTS) {
    sequence_free(&seq);    
  }
  if (ret == LIBLTE_ERROR) {
    refsignal_free(q);
  }
  return ret;
}

void refsignal_free(refsignal_t *q) {
  if (q->symbols_ref) {
    free(q->symbols_ref);
  }
  if (q->refs) {
    free(q->refs);
  }
  if (q->ch_est) {
    free(q->ch_est);
  }
  bzero(q, sizeof(refsignal_t));
}


