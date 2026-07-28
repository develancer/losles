#include "losles-cache-policy.h"

static gboolean
visit_offset(guint current_index,
             guint n_items,
             guint offset,
             gint side,
             LoslesCacheIndexFunc func,
             gpointer user_data)
{
  guint index = 0;
  if (side < 0) {
    if (current_index < offset)
      return TRUE;
    index = current_index - offset;
  } else {
    if (offset > G_MAXUINT - current_index)
      return TRUE;
    index = current_index + offset;
    if (index >= n_items)
      return TRUE;
  }

  return func(index, user_data);
}

void
losles_cache_policy_foreach_preload(guint current_index,
                                    guint n_items,
                                    guint distance,
                                    gint direction,
                                    LoslesCacheIndexFunc func,
                                    gpointer user_data)
{
  g_return_if_fail(func != NULL);
  if (n_items == 0 || current_index >= n_items)
    return;

  const gint preferred_side = direction < 0 ? -1 : 1;
  const guint max_offset = MIN(distance, n_items - 1);
  for (guint offset = 1; offset <= max_offset; offset++) {
    if (!visit_offset(current_index,
                      n_items,
                      offset,
                      preferred_side,
                      func,
                      user_data) ||
        !visit_offset(current_index,
                      n_items,
                      offset,
                      -preferred_side,
                      func,
                      user_data))
      return;
  }
}

void
losles_cache_policy_foreach_eviction(guint current_index,
                                     guint n_items,
                                     guint distance,
                                     gint direction,
                                     LoslesCacheIndexFunc func,
                                     gpointer user_data)
{
  g_return_if_fail(func != NULL);
  if (n_items == 0 || current_index >= n_items)
    return;

  const gint preferred_side = direction < 0 ? -1 : 1;
  const guint max_offset = MIN(distance, n_items - 1);
  for (guint offset = max_offset; offset > 0; offset--) {
    if (!visit_offset(current_index,
                      n_items,
                      offset,
                      -preferred_side,
                      func,
                      user_data) ||
        !visit_offset(current_index,
                      n_items,
                      offset,
                      preferred_side,
                      func,
                      user_data))
      return;
  }
}

typedef struct {
  guint candidate_index;
  gsize needed;
  gsize available;
  LoslesCacheIndexSizeFunc size_func;
  gpointer user_data;
} AdmissionProbe;

static gboolean
probe_admission(guint index, gpointer user_data)
{
  AdmissionProbe *probe = user_data;
  if (index == probe->candidate_index)
    return FALSE;

  const gsize size = probe->size_func(index, probe->user_data);
  if (G_MAXSIZE - probe->available < size)
    probe->available = G_MAXSIZE;
  else
    probe->available += size;
  return probe->available < probe->needed;
}

typedef struct {
  guint candidate_index;
  gsize needed;
  gsize freed;
  LoslesCacheIndexSizeFunc size_func;
  LoslesCacheIndexFunc evict_func;
  gpointer user_data;
} AdmissionEviction;

static gboolean
evict_for_admission(guint index, gpointer user_data)
{
  AdmissionEviction *eviction = user_data;
  if (index == eviction->candidate_index)
    return FALSE;

  const gsize size =
    eviction->size_func(index, eviction->user_data);
  if (size > 0) {
    eviction->evict_func(index, eviction->user_data);
    if (G_MAXSIZE - eviction->freed < size)
      eviction->freed = G_MAXSIZE;
    else
      eviction->freed += size;
  }
  return eviction->freed < eviction->needed;
}

gboolean
losles_cache_policy_admit(guint current_index,
                          guint n_items,
                          guint distance,
                          gint direction,
                          guint candidate_index,
                          gsize current_size,
                          gsize candidate_size,
                          gsize limit,
                          gboolean foreground,
                          LoslesCacheIndexSizeFunc size_func,
                          LoslesCacheIndexFunc evict_func,
                          gpointer user_data)
{
  g_return_val_if_fail(size_func != NULL, FALSE);
  g_return_val_if_fail(evict_func != NULL, FALSE);
  if (n_items == 0 ||
      current_index >= n_items ||
      candidate_index >= n_items)
    return FALSE;

  const guint candidate_distance =
    candidate_index > current_index
      ? candidate_index - current_index
      : current_index - candidate_index;
  if (candidate_distance > distance)
    return FALSE;

  const gsize total =
    G_MAXSIZE - current_size < candidate_size
      ? G_MAXSIZE
      : current_size + candidate_size;
  if (total <= limit)
    return TRUE;

  AdmissionProbe probe = {
    .candidate_index = candidate_index,
    .needed = total - limit,
    .size_func = size_func,
    .user_data = user_data,
  };
  losles_cache_policy_foreach_eviction(current_index,
                                       n_items,
                                       distance,
                                       direction,
                                       probe_admission,
                                       &probe);
  if (!foreground && probe.available < probe.needed)
    return FALSE;

  AdmissionEviction eviction = {
    .candidate_index = candidate_index,
    .needed = probe.needed,
    .size_func = size_func,
    .evict_func = evict_func,
    .user_data = user_data,
  };
  losles_cache_policy_foreach_eviction(current_index,
                                       n_items,
                                       distance,
                                       direction,
                                       evict_for_admission,
                                       &eviction);
  return foreground || eviction.freed >= eviction.needed;
}
