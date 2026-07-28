#include <glib.h>

#include "losles-cache-policy.h"

static gboolean
collect_index(guint index, gpointer user_data)
{
  GArray *indices = user_data;
  g_array_append_val(indices, index);
  return TRUE;
}

static GArray *
collect_preload(guint current,
                guint n_items,
                guint distance,
                gint direction)
{
  GArray *indices = g_array_new(FALSE, FALSE, sizeof(guint));
  losles_cache_policy_foreach_preload(current,
                                      n_items,
                                      distance,
                                      direction,
                                      collect_index,
                                      indices);
  return indices;
}

static GArray *
collect_eviction(guint current,
                 guint n_items,
                 guint distance,
                 gint direction)
{
  GArray *indices = g_array_new(FALSE, FALSE, sizeof(guint));
  losles_cache_policy_foreach_eviction(current,
                                       n_items,
                                       distance,
                                       direction,
                                       collect_index,
                                       indices);
  return indices;
}

static void
assert_indices(GArray *actual, const guint *expected, guint n_expected)
{
  g_assert_cmpuint(actual->len, ==, n_expected);
  for (guint i = 0; i < n_expected; i++)
    g_assert_cmpuint(g_array_index(actual, guint, i), ==, expected[i]);
}

static void
test_preload_order(void)
{
  g_autoptr(GArray) right = collect_preload(5, 12, 3, 1);
  const guint expected_right[] = {6, 4, 7, 3, 8, 2};
  assert_indices(right, expected_right, G_N_ELEMENTS(expected_right));

  g_autoptr(GArray) left = collect_preload(5, 12, 3, -1);
  const guint expected_left[] = {4, 6, 3, 7, 2, 8};
  assert_indices(left, expected_left, G_N_ELEMENTS(expected_left));
}

static void
test_preload_boundaries(void)
{
  g_autoptr(GArray) first = collect_preload(0, 4, 5, 1);
  const guint expected_first[] = {1, 2, 3};
  assert_indices(first, expected_first, G_N_ELEMENTS(expected_first));

  g_autoptr(GArray) last = collect_preload(3, 4, 5, -1);
  const guint expected_last[] = {2, 1, 0};
  assert_indices(last, expected_last, G_N_ELEMENTS(expected_last));

  g_autoptr(GArray) invalid = collect_preload(4, 4, 5, 1);
  g_assert_cmpuint(invalid->len, ==, 0);

  g_autoptr(GArray) saturated =
    collect_preload(0, 4, G_MAXUINT, 1);
  assert_indices(saturated, expected_first, G_N_ELEMENTS(expected_first));
}

static void
test_eviction_order(void)
{
  g_autoptr(GArray) right = collect_eviction(5, 12, 3, 1);
  const guint expected_right[] = {2, 8, 3, 7, 4, 6};
  assert_indices(right, expected_right, G_N_ELEMENTS(expected_right));

  g_autoptr(GArray) left = collect_eviction(5, 12, 3, -1);
  const guint expected_left[] = {8, 2, 7, 3, 6, 4};
  assert_indices(left, expected_left, G_N_ELEMENTS(expected_left));
}

typedef struct {
  guint calls;
  guint stop_after;
} StopState;

static gboolean
stop_early(guint index, gpointer user_data)
{
  (void)index;
  StopState *state = user_data;
  state->calls++;
  return state->calls < state->stop_after;
}

static void
test_callback_can_stop(void)
{
  StopState state = {.stop_after = 3};
  losles_cache_policy_foreach_preload(5,
                                      12,
                                      5,
                                      1,
                                      stop_early,
                                      &state);
  g_assert_cmpuint(state.calls, ==, 3);
}

typedef struct {
  gsize sizes[11];
  GArray *evicted;
} AdmissionState;

static gsize
admission_size(guint index, gpointer user_data)
{
  AdmissionState *state = user_data;
  return state->sizes[index];
}

static gboolean
record_eviction(guint index, gpointer user_data)
{
  AdmissionState *state = user_data;
  g_array_append_val(state->evicted, index);
  return TRUE;
}

static void
test_admission_prefers_closer_candidate(void)
{
  AdmissionState state = {
    .sizes = {[0] = 30, [5] = 40, [10] = 30},
    .evicted = g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  const gboolean admitted =
    losles_cache_policy_admit(5,
                              11,
                              5,
                              1,
                              6,
                              100,
                              30,
                              100,
                              FALSE,
                              admission_size,
                              record_eviction,
                              &state);
  g_assert_true(admitted);
  const guint expected[] = {0};
  assert_indices(state.evicted, expected, G_N_ELEMENTS(expected));
  g_array_unref(state.evicted);
}

static void
test_admission_rejects_lower_priority_without_eviction(void)
{
  AdmissionState state = {
    .sizes = {[0] = 30, [5] = 40, [10] = 30},
    .evicted = g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  const gboolean admitted =
    losles_cache_policy_admit(5,
                              11,
                              5,
                              1,
                              0,
                              100,
                              30,
                              100,
                              FALSE,
                              admission_size,
                              record_eviction,
                              &state);
  g_assert_false(admitted);
  g_assert_cmpuint(state.evicted->len, ==, 0);
  g_array_unref(state.evicted);
}

static void
test_admission_rejects_outside_window(void)
{
  AdmissionState state = {
    .sizes = {[0] = 30, [5] = 40, [10] = 30},
    .evicted = g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  const gboolean admitted =
    losles_cache_policy_admit(5,
                              11,
                              2,
                              1,
                              10,
                              100,
                              1,
                              200,
                              FALSE,
                              admission_size,
                              record_eviction,
                              &state);
  g_assert_false(admitted);
  g_assert_cmpuint(state.evicted->len, ==, 0);
  g_array_unref(state.evicted);
}

static void
test_admission_does_not_flush_when_candidate_cannot_fit(void)
{
  AdmissionState state = {
    .sizes = {[0] = 10, [5] = 80, [10] = 10},
    .evicted = g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  const gboolean admitted =
    losles_cache_policy_admit(5,
                              11,
                              5,
                              1,
                              6,
                              100,
                              30,
                              100,
                              FALSE,
                              admission_size,
                              record_eviction,
                              &state);
  g_assert_false(admitted);
  g_assert_cmpuint(state.evicted->len, ==, 0);
  g_array_unref(state.evicted);
}

static void
test_foreground_admission_is_soft_limited(void)
{
  AdmissionState state = {
    .sizes = {[0] = 50, [10] = 50},
    .evicted = g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  const gboolean admitted =
    losles_cache_policy_admit(5,
                              11,
                              5,
                              1,
                              5,
                              100,
                              150,
                              100,
                              TRUE,
                              admission_size,
                              record_eviction,
                              &state);
  g_assert_true(admitted);
  const guint expected[] = {0, 10};
  assert_indices(state.evicted, expected, G_N_ELEMENTS(expected));
  g_array_unref(state.evicted);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/cache-policy/preload-order", test_preload_order);
  g_test_add_func("/cache-policy/preload-boundaries",
                  test_preload_boundaries);
  g_test_add_func("/cache-policy/eviction-order", test_eviction_order);
  g_test_add_func("/cache-policy/callback-stop", test_callback_can_stop);
  g_test_add_func("/cache-policy/admission/closer-displaces-farther",
                  test_admission_prefers_closer_candidate);
  g_test_add_func("/cache-policy/admission/lower-priority-rejected",
                  test_admission_rejects_lower_priority_without_eviction);
  g_test_add_func("/cache-policy/admission/outside-window-rejected",
                  test_admission_rejects_outside_window);
  g_test_add_func("/cache-policy/admission/no-destructive-rejection",
                  test_admission_does_not_flush_when_candidate_cannot_fit);
  g_test_add_func("/cache-policy/admission/foreground-soft-limit",
                  test_foreground_admission_is_soft_limited);
  return g_test_run();
}
