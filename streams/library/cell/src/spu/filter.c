/*-----------------------------------------------------------------------------
 * filter.c
 *
 * Filter command implementation (SPU).
 *---------------------------------------------------------------------------*/

#include "defs.h"
#include "depend.h"
#include "filter.h"
#include "dma.h"
#include "stats.h"

/*-----------------------------------------------------------------------------
 * run_call_func
 *
 * Command handler.
 *---------------------------------------------------------------------------*/
void
run_call_func(CALL_FUNC_CMD *cmd)
{
  (*(void (*)(void))cmd->func)();

  dep_complete_command();
}

/*-----------------------------------------------------------------------------
 * run_load_data
 *
 * Command handler.
 *---------------------------------------------------------------------------*/
void
run_load_data(LOAD_DATA_CMD *cmd)
{
  switch (cmd->state) {
  case 0:
    // Initialization.

    // Validate alignment/padding.
    pcheck((((uintptr_t)cmd->dest_lsa & QWORD_MASK) == 0) &&
          ((cmd->src_addr & QWORD_MASK) == 0) &&
          ((cmd->num_bytes & QWORD_MASK) == 0));

    // Reserve tag for transfers.
    cmd->tag = dma_reserve_tag();
    check(cmd->tag != INVALID_TAG);

    cmd->state = 1;

  case 1: {
    // Copy next piece of data.

    uint32_t copy_bytes;

    // Wait for MFC slot.
    if (!dma_query_avail(1)) {
      dma_wait_avail();
      return;
    }

    // Limit transfer size.
    if (cmd->num_bytes <= MAX_DMA_SIZE) {
      // Last piece.
      copy_bytes = cmd->num_bytes;
      cmd->state = 2;
    } else {
      copy_bytes = MAX_DMA_SIZE;
    }

    // Start transfer for this piece.
    dma_get(cmd->tag, cmd->dest_lsa, cmd->src_addr, cmd->num_bytes);

    // Advance to next piece.
    cmd->dest_lsa += copy_bytes;
    cmd->src_addr += copy_bytes;
    cmd->num_bytes -= copy_bytes;

    // Wait for transfer to complete.
    dma_wait_complete(cmd->tag);
    return;
  }

  case 2:
    // Finished copying all pieces. Release tag and complete.

    dma_release_tag(cmd->tag);

    dep_complete_command();
    return;

  default:
    unreached();
  }
}

/*-----------------------------------------------------------------------------
 * run_filter_load
 *
 * Command handler.
 *---------------------------------------------------------------------------*/
void
run_filter_load(FILTER_LOAD_CMD *cmd)
{
  FILTER_CB *filt = (FILTER_CB *)cmd->filt;

  switch (cmd->state) {
  case 0: {
    // Initialize filter CB.

    uint32_t tape_data_size;
#if CHECK
    vec4_uint32_t *tape_data;
#endif

    pcheck((((uintptr_t)cmd->filt & QWORD_MASK) == 0) &&
           ((cmd->desc.state_size == 0) ||
            (((cmd->desc.state_size & QWORD_MASK) == 0) &&
             ((cmd->desc.state_addr & QWORD_MASK) == 0))));

    stats_start_filter_load();

    // Copy filter description.
    filt->desc = cmd->desc;

    // Initialize pointers to input/output tape arrays.
    filt->inputs = (BUFFER_CB **)filt->data;
    filt->outputs = filt->inputs + filt->desc.num_inputs;

    // Total size of area storing tape pointers (this is padded up to a qword).
    tape_data_size =
      ROUND_UP((filt->desc.num_inputs + filt->desc.num_outputs) *
                 sizeof(BUFFER_CB *),
               QWORD_SIZE);
    // Initialize pointer to filter state.
    filt->state = filt->data + tape_data_size;

#if CHECK
    // Clear input/output tape pointers.
    tape_data = (vec4_uint32_t *)filt->data;
    while (tape_data_size != 0) {
      *tape_data++ = VEC_SPLAT_U32((uint32_t)buf_get_cb(NULL));
      tape_data_size -= QWORD_SIZE;
    }
#endif

#if CHECK
    // Initialize debug flags.
    filt->busy = FALSE;
    filt->attached_inputs = 0;
    filt->attached_outputs = 0;
#endif

    // Nothing more to do for stateless filters.
    if (filt->desc.state_size == 0) {
      dep_complete_command();
      return;
    }

    IF_CHECK(filt->busy = TRUE);
    cmd->state_lsa = filt->state;

    // Reserve tag for loading filter state.
    cmd->tag = dma_reserve_tag();
    check(cmd->tag != INVALID_TAG);

    cmd->state = 1;
  }

  case 1: {
    // Copy filter state in pieces.

    uint32_t copy_bytes;

    // Wait for MFC slot.
    if (!dma_query_avail(1)) {
      dma_wait_avail();
      return;
    }

    // Limit transfer size.
    if (cmd->desc.state_size <= MAX_DMA_SIZE) {
      // Last piece.
      copy_bytes = cmd->desc.state_size;
      cmd->state = 2;
    } else {
      copy_bytes = MAX_DMA_SIZE;
    }

    // Start transfer for this piece.
    dma_get(cmd->tag, cmd->state_lsa, cmd->desc.state_addr, copy_bytes);

    // Advance to next peice.
    cmd->state_lsa += copy_bytes;
    cmd->desc.state_addr += copy_bytes;
    cmd->desc.state_size -= copy_bytes;

    // Wait for transfer to complete.
    dma_wait_complete(cmd->tag);
    return;
  }

  case 2:
    // Finished loading filter state. Release tag and complete.

    dma_release_tag(cmd->tag);

    IF_CHECK(filt->busy = FALSE);

    dep_complete_command();
    return;

  default:
    unreached();
  }
}

/*-----------------------------------------------------------------------------
 * run_filter_unload
 *
 * Command handler.
 *---------------------------------------------------------------------------*/
void
run_filter_unload(FILTER_UNLOAD_CMD *cmd)
{
  FILTER_CB *filt = (FILTER_CB *)cmd->filt;

  switch (cmd->state) {
  case 0:
    // Initialization.

    pcheck(((uintptr_t)cmd->filt & QWORD_MASK) == 0);
    // Make sure filter is not running.
    check(!filt->busy);

#if CHECK
    if (cmd->detach_only) {
      uint32_t tape_data_count;
      vec4_uint32_t *tape_data;

      filt->attached_inputs = 0;
      filt->attached_outputs = 0;

      tape_data_count =
        (filt->desc.num_inputs + filt->desc.num_outputs + 3) >> 2;
      tape_data = (vec4_uint32_t *)filt->data;

      while (tape_data_count != 0) {
        *tape_data++ = VEC_SPLAT_U32((uint32_t)buf_get_cb(NULL));
        tape_data_count--;
      }

      dep_complete_command();
      return;
    }

    // Mark as unloaded (busy flag is never unset).
    filt->busy = TRUE;
#endif

    stats_start_filter_unload();

    // Nothing to do for stateless filters.
    if (filt->desc.state_size == 0) {
      dep_complete_command();
      return;
    }

    // Reserve tag for writing back filter state.
    cmd->tag = dma_reserve_tag();
    check(cmd->tag != INVALID_TAG);

    cmd->state = 1;

  case 1: {
    // Write back filter state in pieces. The filter CB state fields are
    // modified as pieces are copied.

    uint32_t copy_bytes;

    // Wait for MFC slot.
    if (!dma_query_avail(1)) {
      dma_wait_avail();
      return;
    }

    // Limit transfer size.
    if (filt->desc.state_size <= MAX_DMA_SIZE) {
      // Last piece.
      copy_bytes = filt->desc.state_size;
      cmd->state = 2;
    } else {
      copy_bytes = MAX_DMA_SIZE;
    }

    // Start transfer for this piece.
    dma_put(cmd->tag, filt->desc.state_addr, filt->state, copy_bytes);

    // Advance to next peice.
    filt->desc.state_addr += copy_bytes;
    filt->state += copy_bytes;
    filt->desc.state_size -= copy_bytes;

    // Wait for transfer to complete.
    dma_wait_complete(cmd->tag);
    return;
  }

  case 2:
    // Finished writing back filter state. Release tag and complete.

    dma_release_tag(cmd->tag);

    dep_complete_command();
    return;

  default:
    unreached();
  }
}

/*-----------------------------------------------------------------------------
 * run_filter_attach_input
 *
 * Command handler. Attaches/detaches an input tape of a filter from the front
 * end of a buffer.
 *
 * If attaching, the buffer currently attached to the tape is automatically
 * detached. Detaching is okay even if tape is not attached.
 *---------------------------------------------------------------------------*/
void
run_filter_attach_input(FILTER_ATTACH_INPUT_CMD *cmd)
{
  FILTER_CB *filt = (FILTER_CB *)cmd->filt;

  pcheck((((uintptr_t)cmd->filt & QWORD_MASK) == 0) &&
         (cmd->tape_id < filt->desc.num_inputs) &&
         (((uintptr_t)cmd->buf_data & QWORD_MASK) == 0));
  check(!filt->busy);

#if CHECK
  if (filt->inputs[cmd->tape_id] == buf_get_cb(NULL)) {
    filt->attached_inputs++;
  }

  if (cmd->buf_data == NULL) {
    filt->attached_inputs--;
  }
#endif

  filt->inputs[cmd->tape_id] = buf_get_cb(cmd->buf_data);
  dep_complete_command();
}

/*-----------------------------------------------------------------------------
 * run_filter_attach_output
 *
 * Command handler. Attaches/detaches an output tape of a filter from the back
 * end of a buffer.
 *
 * Similar behavior as filter_attach_input.
 *---------------------------------------------------------------------------*/
void
run_filter_attach_output(FILTER_ATTACH_OUTPUT_CMD *cmd)
{
  FILTER_CB *filt = (FILTER_CB *)cmd->filt;

  pcheck((((uintptr_t)cmd->filt & QWORD_MASK) == 0) &&
         (cmd->tape_id < filt->desc.num_outputs) &&
         (((uintptr_t)cmd->buf_data & QWORD_MASK) == 0));
  check(!filt->busy);

#if CHECK
  if (filt->outputs[cmd->tape_id] == buf_get_cb(NULL)) {
    filt->attached_outputs++;
  }

  if (cmd->buf_data == NULL) {
    filt->attached_outputs--;
  }
#endif

  filt->outputs[cmd->tape_id] = buf_get_cb(cmd->buf_data);
  dep_complete_command();
}

/*-----------------------------------------------------------------------------
 * run_filter_run
 *
 * Command handler.
 *---------------------------------------------------------------------------*/
void
run_filter_run(FILTER_RUN_CMD *cmd)
{
  FILTER_CB *filt = (FILTER_CB *)cmd->filt;
  uint32_t loop_iters;
#if STATS_ENABLE
  uint32_t work_start;
#endif

#if CHECK
  if (cmd->state == 0) {
    pcheck((((uintptr_t)cmd->filt & QWORD_MASK) == 0) &&
           (cmd->loop_iters != 0));
    // Make sure filter isn't unloaded or already running and mark as running.
    check(!filt->busy);
    filt->busy = TRUE;

    // Make sure all input/output tapes are attached to buffers.
    check((filt->attached_inputs == filt->desc.num_inputs) &&
          (filt->attached_outputs == filt->desc.num_outputs));

    for (uint8_t i = 0; i < filt->desc.num_inputs; i++) {
      BUFFER_CB *buf = filt->inputs[i];
      check(buf->front_action == BUFFER_ACTION_NONE);
      buf->front_action = BUFFER_ACTION_RUN;
    }

    for (uint8_t i = 0; i < filt->desc.num_outputs; i++) {
      BUFFER_CB *buf = filt->outputs[i];
      check(buf->back_action == BUFFER_ACTION_NONE);
      buf->back_action = BUFFER_ACTION_RUN;
    }

    cmd->state = 1;
  }
#endif

  /*
   * TODO: Figure out convention for ihead/otail
   */

  // Run work function.
  loop_iters = (LIKELY(cmd->iters >= cmd->loop_iters) ?
                cmd->loop_iters : cmd->iters);
  cmd->iters -= loop_iters;

#if STATS_ENABLE
  work_start = spu_read_decrementer();
#endif
  (*(FILTER_WORK_FUNC *)filt->desc.work_func)
    (filt->desc.param, filt->state, filt->inputs, filt->outputs, loop_iters);
#if STATS_ENABLE
  stats_work_ticks += work_start - spu_read_decrementer();
#endif

#if CHECK
  for (uint8_t i = 0; i < filt->desc.num_inputs; i++) {
    BUFFER_CB *buf = filt->inputs[i];
    buf->ihead = buf->head;
  }

  for (uint8_t i = 0; i < filt->desc.num_outputs; i++) {
    BUFFER_CB *buf = filt->outputs[i];
    buf->otail = buf->tail;
  }
#endif

  if (cmd->iters == 0) {
    // Done running.
#if CHECK
    filt->busy = FALSE;

    for (uint8_t i = 0; i < filt->desc.num_inputs; i++) {
      filt->inputs[i]->front_action = BUFFER_ACTION_NONE;
    }

    for (uint8_t i = 0; i < filt->desc.num_outputs; i++) {
      filt->outputs[i]->back_action = BUFFER_ACTION_NONE;
    }
#endif

    dep_complete_command();
    stats_done_filter_run();
  }
}
