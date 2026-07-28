#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef gboolean (*LoslesCacheIndexFunc)(guint index, gpointer user_data);
typedef gsize (*LoslesCacheIndexSizeFunc)(guint index, gpointer user_data);

/*
 * Return ten percent of total_memory_bytes, truncated to whole bytes and
 * clamped to the addressable gsize range.
 */
gsize losles_cache_policy_limit_for_memory(guint64 total_memory_bytes);

/*
 * Visit eligible neighbors nearest-first. At each distance, the side selected
 * by direction is visited first. Iteration stops when func returns FALSE.
 */
void losles_cache_policy_foreach_preload(guint current_index,
                                         guint n_items,
                                         guint distance,
                                         gint direction,
                                         LoslesCacheIndexFunc func,
                                         gpointer user_data);

/*
 * Visit eligible neighbors farthest-first. At each distance, the side
 * opposite direction is visited first so navigation look-ahead is retained.
 * The current item is never visited.
 */
void losles_cache_policy_foreach_eviction(guint current_index,
                                          guint n_items,
                                          guint distance,
                                          gint direction,
                                          LoslesCacheIndexFunc func,
                                          gpointer user_data);

/*
 * Admit a candidate within the eligible window. current_size excludes the
 * candidate. Only lower-priority entries are considered for eviction. A
 * background candidate is rejected without evicting anything unless enough
 * space can be recovered; a foreground candidate may exceed the soft limit.
 */
gboolean losles_cache_policy_admit(guint current_index,
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
                                   gpointer user_data);

G_END_DECLS
