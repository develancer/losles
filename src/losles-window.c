#include "losles-window.h"

#include "formats/losles-format-registry.h"
#include "formats/losles-format.h"
#include "losles-cache-policy.h"
#include "losles-color-manager.h"
#include "losles-config.h"
#include "losles-image.h"
#include "losles-platform.h"
#include "losles-rendered-image.h"

#define CACHE_LIMIT_FALLBACK ((gsize)512 * 1024 * 1024)
#define PRELOAD_DISTANCE 5
#define MAX_CONCURRENT_DECODES 2
#define MAX_CONCURRENT_RENDERS 2
#define CROP_HANDLE_SIZE 10.0
#define CROP_HIT_TOLERANCE 8.0
#define CROP_MIN_SIZE 2.0
#define ZOOM_STEP 1.25
#define ZOOM_MAX 16.0
#define LOADING_SPINNER_SIZE 64
#define MOUSE_BUTTON_BACK 8
#define MOUSE_BUTTON_FORWARD 9

typedef struct {
  LoslesFormatRegistry *registry;
  GFile *file;
  gchar *uri;
  guint generation;
} LoadJob;

typedef struct {
  GFile *opened_file;
  GFile *directory;
  guint generation;
} ScanJob;

typedef struct {
  LoslesColorTarget *target;
  LoslesImage *image;
  gchar *uri;
  guint generation;
} RenderJob;

typedef struct {
  LoslesRenderedImage *rendered;
  GdkTexture *texture;
  gsize size;
} RenderCacheEntry;

typedef enum {
  CACHE_SOURCE,
  CACHE_RENDER,
} CacheKind;

typedef struct {
  GHashTable *entries;
  GHashTable *capacity_blocked;
  gsize *size;
  gsize limit;
} CacheState;

typedef enum {
  EDIT_ROTATE,
  EDIT_NORMALIZE_ORIENTATION,
  EDIT_CROP,
} EditKind;

typedef enum {
  CROP_DRAG_NONE,
  CROP_DRAG_NEW,
  CROP_DRAG_MOVE,
  CROP_DRAG_LEFT,
  CROP_DRAG_RIGHT,
  CROP_DRAG_TOP,
  CROP_DRAG_BOTTOM,
  CROP_DRAG_TOP_LEFT,
  CROP_DRAG_TOP_RIGHT,
  CROP_DRAG_BOTTOM_LEFT,
  CROP_DRAG_BOTTOM_RIGHT,
} CropDragMode;

typedef struct {
  gdouble image_width;
  gdouble image_height;
  gdouble scale;
  gdouble left;
  gdouble top;
  gdouble shown_width;
  gdouble shown_height;
} CropGeometry;

typedef struct {
  LoslesImage *image;
  GFile *destination;
  EditKind kind;
  LoslesRotation rotation;
  LoslesCrop crop;
  LoslesFormatEditFlags flags;
} EditJob;

typedef struct {
  LoslesWindow *window;
  EditJob *job;
} EditConfirmation;

typedef struct {
  GFile *file;
  GFile *directory;
  GFile *preferred_next;
} DeleteJob;

typedef struct {
  GPtrArray *files;
  GError *scan_error;
} DeleteResult;

typedef struct {
  LoslesWindow *window;
  guint32 timestamp;
} DropFocusRequest;

struct _LoslesWindow {
  GtkApplicationWindow parent_instance;

  LoslesFormatRegistry *registry;
  LoslesColorManager *color_manager;

  GtkHeaderBar *header_bar;
  GtkFixed *zoom_view;
  GtkPicture *picture;
  GtkDrawingArea *crop_area;
  GtkSpinner *spinner;
  GtkLabel *status;
  GtkButton *open_button;
  GtkButton *previous_button;
  GtkButton *next_button;
  GtkButton *rotate_left_button;
  GtkButton *rotate_right_button;
  GtkWidget *normalize_orientation_tooltip_area;
  GtkButton *normalize_orientation_button;
  GtkToggleButton *info_button;
  GtkToggleButton *crop_button;
  GtkButton *apply_crop_button;
  GtkWindow *about_dialog;
  GdkTexture *application_icon;

  GPtrArray *files;
  guint current_index;
  GHashTable *cache;
  GHashTable *inflight;
  GHashTable *decode_failed;
  GHashTable *decode_capacity_blocked;
  gsize cache_size;
  gsize source_cache_limit;
  guint generation;

  GHashTable *render_cache;
  GHashTable *render_inflight;
  GHashTable *render_failed;
  GHashTable *render_capacity_blocked;
  gchar *render_profile_id;
  gsize render_cache_size;
  gsize render_cache_limit;
  guint render_generation;
  gint navigation_direction;

  LoslesImage *current_image;
  GdkTexture *current_texture;
  GCancellable *load_cancellable;
  GCancellable *render_cancellable;

  gboolean monitor_signals_connected;
  gboolean operation_in_progress;
  gboolean foreground_loading;
  gboolean reset_zoom_on_next_display;
  gboolean preserve_zoom_if_same_dimensions;
  gdouble zoom_scale;
  gdouble zoom_center_x;
  gdouble zoom_center_y;
  gdouble zoom_picture_x;
  gdouble zoom_picture_y;
  gdouble zoom_picture_width;
  gdouble zoom_picture_height;
  gdouble zoom_pointer_x;
  gdouble zoom_pointer_y;
  gdouble zoom_drag_center_x;
  gdouble zoom_drag_center_y;
  gint zoom_view_width;
  gint zoom_view_height;
  gboolean zoom_pointer_inside;
  gboolean zoom_dragging;
  CropDragMode crop_drag_mode;
  gdouble crop_drag_origin_x;
  gdouble crop_drag_origin_y;
  LoslesCrop crop_drag_initial;
  LoslesCrop crop;
  gboolean crop_valid;
};

G_DEFINE_FINAL_TYPE(LoslesWindow, losles_window, GTK_TYPE_APPLICATION_WINDOW)

static void show_index(LoslesWindow *self,
                       guint index,
                       gboolean preserve_zoom_if_same_dimensions);
static void start_render(LoslesWindow *self);
static void start_render_for_image(LoslesWindow *self,
                                   LoslesImage *image,
                                   gboolean foreground);
static void invalidate_render_cache(LoslesWindow *self);
static void update_controls(LoslesWindow *self);
static void preload_neighbors(LoslesWindow *self);
static void preload_rendered_neighbors(LoslesWindow *self);
static void apply_zoom_layout(LoslesWindow *self);
static void reset_zoom(LoslesWindow *self);
static void queue_edit(LoslesWindow *self, EditJob *job);

static void
load_job_free(LoadJob *job)
{
  g_clear_object(&job->registry);
  g_clear_object(&job->file);
  g_clear_pointer(&job->uri, g_free);
  g_free(job);
}

static void
scan_job_free(ScanJob *job)
{
  g_clear_object(&job->opened_file);
  g_clear_object(&job->directory);
  g_free(job);
}

static void
render_job_free(RenderJob *job)
{
  g_clear_pointer(&job->target, losles_color_target_unref);
  g_clear_object(&job->image);
  g_clear_pointer(&job->uri, g_free);
  g_free(job);
}

static void
render_cache_entry_free(RenderCacheEntry *entry)
{
  if (!entry)
    return;
  g_clear_object(&entry->texture);
  g_clear_pointer(&entry->rendered, losles_rendered_image_free);
  g_free(entry);
}

static void
edit_job_free(EditJob *job)
{
  g_clear_object(&job->image);
  g_clear_object(&job->destination);
  g_free(job);
}

static EditJob *
edit_job_copy(EditJob *job)
{
  EditJob *copy = g_new0(EditJob, 1);
  copy->image = g_object_ref(job->image);
  copy->destination = g_object_ref(job->destination);
  copy->kind = job->kind;
  copy->rotation = job->rotation;
  copy->crop = job->crop;
  copy->flags = job->flags;
  return copy;
}

static void
edit_confirmation_free(EditConfirmation *confirmation)
{
  g_clear_object(&confirmation->window);
  g_clear_pointer(&confirmation->job, edit_job_free);
  g_free(confirmation);
}

static void
delete_job_free(DeleteJob *job)
{
  g_clear_object(&job->file);
  g_clear_object(&job->directory);
  g_clear_object(&job->preferred_next);
  g_free(job);
}

static void
delete_result_free(DeleteResult *result)
{
  g_clear_pointer(&result->files, g_ptr_array_unref);
  g_clear_error(&result->scan_error);
  g_free(result);
}

static void
drop_focus_request_free(DropFocusRequest *request)
{
  g_clear_object(&request->window);
  g_free(request);
}

static GFile *
current_file(LoslesWindow *self)
{
  if (!self->files || self->current_index >= self->files->len)
    return NULL;
  return g_ptr_array_index(self->files, self->current_index);
}

static gchar *
file_uri(GFile *file)
{
  return g_file_get_uri(file);
}

static gboolean
is_current_file(LoslesWindow *self, GFile *file)
{
  GFile *current = current_file(self);
  return current && g_file_equal(current, file);
}

static void
set_status(LoslesWindow *self, const gchar *text)
{
  gtk_label_set_text(self->status, text ? text : "");
}

static void
show_error(LoslesWindow *self, const gchar *primary, const GError *error)
{
  g_autofree gchar *message =
    error ? g_strdup_printf("%s: %s", primary, error->message)
          : g_strdup(primary);
  set_status(self, message);

  GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", primary);
  if (error)
    gtk_alert_dialog_set_detail(dialog, error->message);
  gtk_alert_dialog_show(dialog, GTK_WINDOW(self));
  g_object_unref(dialog);
}

static gboolean
zoom_dimensions(LoslesWindow *self,
                gdouble scale,
                gdouble *width,
                gdouble *height)
{
  if (!self->current_texture)
    return FALSE;

  const gint view_width = self->zoom_view_width;
  const gint view_height = self->zoom_view_height;
  const gint texture_width =
    gdk_texture_get_width(self->current_texture);
  const gint texture_height =
    gdk_texture_get_height(self->current_texture);
  if (view_width <= 0 || view_height <= 0 ||
      texture_width <= 0 || texture_height <= 0)
    return FALSE;

  const gdouble fit =
    MIN((gdouble)view_width / texture_width,
        (gdouble)view_height / texture_height);
  *width =
    (gint)CLAMP(texture_width * fit * scale + 0.5,
                1.0,
                (gdouble)G_MAXINT);
  *height =
    (gint)CLAMP(texture_height * fit * scale + 0.5,
                1.0,
                (gdouble)G_MAXINT);
  return TRUE;
}

static void
update_zoom_cursor(LoslesWindow *self)
{
  const gboolean over_image =
    self->zoom_pointer_inside &&
    self->zoom_pointer_x >= self->zoom_picture_x &&
    self->zoom_pointer_y >= self->zoom_picture_y &&
    self->zoom_pointer_x <=
      self->zoom_picture_x + self->zoom_picture_width &&
    self->zoom_pointer_y <=
      self->zoom_picture_y + self->zoom_picture_height;
  const gchar *cursor = NULL;
  if (self->zoom_dragging)
    cursor = "grabbing";
  else if (self->zoom_scale > 1.0 && over_image)
    cursor = "grab";
  gtk_widget_set_cursor_from_name(GTK_WIDGET(self->zoom_view), cursor);
}

static void
apply_zoom_layout(LoslesWindow *self)
{
  gdouble picture_width = 0;
  gdouble picture_height = 0;
  if (!zoom_dimensions(self,
                       self->zoom_scale,
                       &picture_width,
                       &picture_height)) {
    self->zoom_picture_x = 0;
    self->zoom_picture_y = 0;
    self->zoom_picture_width = 0;
    self->zoom_picture_height = 0;
    gtk_widget_set_size_request(GTK_WIDGET(self->picture), 1, 1);
    gtk_fixed_move(self->zoom_view, GTK_WIDGET(self->picture), 0, 0);
    update_zoom_cursor(self);
    return;
  }

  const gdouble view_width = self->zoom_view_width;
  const gdouble view_height = self->zoom_view_height;
  gdouble picture_x =
    view_width / 2.0 - self->zoom_center_x * picture_width;
  gdouble picture_y =
    view_height / 2.0 - self->zoom_center_y * picture_height;
  /*
   * Permit cursor anchoring within a letterboxed axis, but never move the
   * complete image outside the viewport or expose space beyond an overflow
   * edge.
   */
  if (picture_width <= view_width)
    picture_x = CLAMP(picture_x, 0, view_width - picture_width);
  else
    picture_x = CLAMP(picture_x, view_width - picture_width, 0);
  if (picture_height <= view_height)
    picture_y = CLAMP(picture_y, 0, view_height - picture_height);
  else
    picture_y = CLAMP(picture_y, view_height - picture_height, 0);

  self->zoom_center_x =
    (view_width / 2.0 - picture_x) / picture_width;
  self->zoom_center_y =
    (view_height / 2.0 - picture_y) / picture_height;
  self->zoom_picture_x = picture_x;
  self->zoom_picture_y = picture_y;
  self->zoom_picture_width = picture_width;
  self->zoom_picture_height = picture_height;

  gtk_widget_set_size_request(GTK_WIDGET(self->picture),
                              (gint)picture_width,
                              (gint)picture_height);
  gtk_fixed_move(self->zoom_view,
                 GTK_WIDGET(self->picture),
                 picture_x,
                 picture_y);
  update_zoom_cursor(self);
}

static void
reset_zoom(LoslesWindow *self)
{
  self->zoom_scale = 1.0;
  self->zoom_center_x = 0.5;
  self->zoom_center_y = 0.5;
  self->zoom_dragging = FALSE;
  apply_zoom_layout(self);
}

static gsize
detect_cache_limit(void)
{
  guint64 total_memory = 0;
  g_autoptr(GError) error = NULL;
  if (!losles_platform_get_total_memory(&total_memory, &error)) {
    g_warning("%s; using a 512 MiB cache limit",
              error ? error->message
                    : "Could not determine total system memory");
    return CACHE_LIMIT_FALLBACK;
  }

  const gsize limit =
    losles_cache_policy_limit_for_memory(total_memory);
  g_debug("Cache capacity is %" G_GSIZE_FORMAT
          " bytes per cache (10%% of %" G_GUINT64_FORMAT
          " bytes of system memory)",
          limit,
          total_memory);
  return limit;
}

static void
clear_current_image(LoslesWindow *self)
{
  self->foreground_loading = FALSE;
  self->reset_zoom_on_next_display = FALSE;
  self->preserve_zoom_if_same_dimensions = FALSE;
  g_clear_object(&self->current_image);
  g_clear_object(&self->current_texture);
  gtk_picture_set_paintable(self->picture, NULL);
  reset_zoom(self);
  self->crop_valid = FALSE;
  gtk_toggle_button_set_active(self->crop_button, FALSE);
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
  update_controls(self);
}

static void
prepare_for_image_load(LoslesWindow *self,
                       gboolean preserve_zoom_if_same_dimensions)
{
  /*
   * The decoded source belongs to the newly selected file, while the texture
   * may keep showing the previous file until the replacement is ready.
   * Keeping those states separate avoids a black flash without permitting an
   * edit to act on a file other than the one visible in the window.
   */
  self->foreground_loading = TRUE;
  self->reset_zoom_on_next_display = TRUE;
  self->preserve_zoom_if_same_dimensions =
    preserve_zoom_if_same_dimensions;
  g_clear_object(&self->current_image);
  self->zoom_dragging = FALSE;
  self->crop_valid = FALSE;
  gtk_toggle_button_set_active(self->crop_button, FALSE);
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
  update_zoom_cursor(self);
  update_controls(self);
}

static gint
find_file_index(GPtrArray *files, GFile *file)
{
  for (guint i = 0; i < files->len; i++) {
    if (g_file_equal(g_ptr_array_index(files, i), file))
      return (gint)i;
  }
  return -1;
}

static CacheState
cache_kind_state(LoslesWindow *self, CacheKind kind)
{
  if (kind == CACHE_SOURCE) {
    return (CacheState){
      .entries = self->cache,
      .capacity_blocked = self->decode_capacity_blocked,
      .size = &self->cache_size,
      .limit = self->source_cache_limit,
    };
  }

  return (CacheState){
    .entries = self->render_cache,
    .capacity_blocked = self->render_capacity_blocked,
    .size = &self->render_cache_size,
    .limit = self->render_cache_limit,
  };
}

static gsize
cache_entry_size(CacheKind kind, gpointer value)
{
  if (kind == CACHE_SOURCE)
    return losles_image_get_memory_size(value);
  return ((RenderCacheEntry *)value)->size;
}

static gpointer
cache_entry_at_index(LoslesWindow *self,
                     CacheKind kind,
                     guint index)
{
  if (!self->files || index >= self->files->len)
    return NULL;

  const CacheState state = cache_kind_state(self, kind);
  g_autofree gchar *uri =
    file_uri(g_ptr_array_index(self->files, index));
  return g_hash_table_lookup(state.entries, uri);
}

static void
evict_cache_index(LoslesWindow *self,
                  CacheKind kind,
                  guint index)
{
  if (!self->files || index >= self->files->len)
    return;

  const CacheState state = cache_kind_state(self, kind);
  g_autofree gchar *uri =
    file_uri(g_ptr_array_index(self->files, index));
  gpointer entry = g_hash_table_lookup(state.entries, uri);
  if (entry) {
    *state.size -= cache_entry_size(kind, entry);
    g_hash_table_remove(state.entries, uri);
    g_hash_table_add(state.capacity_blocked, g_strdup(uri));
  }
}

typedef struct {
  LoslesWindow *window;
  CacheKind kind;
  gsize limit;
} PruneContext;

static gboolean
prune_cache_index(guint index, gpointer user_data)
{
  PruneContext *context = user_data;
  const CacheState state =
    cache_kind_state(context->window, context->kind);

  if (*state.size <= context->limit)
    return FALSE;
  evict_cache_index(context->window, context->kind, index);
  return *state.size > context->limit;
}

static void
prune_cache_kind(LoslesWindow *self, CacheKind kind)
{
  if (!self->files || self->files->len == 0)
    return;

  const CacheState state = cache_kind_state(self, kind);

  g_autoptr(GHashTable) allowed =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  const guint first =
    self->current_index > PRELOAD_DISTANCE
      ? self->current_index - PRELOAD_DISTANCE
      : 0;
  const guint last =
    MIN(self->files->len - 1,
        self->current_index + PRELOAD_DISTANCE);
  for (guint i = first; i <= last; i++) {
    g_autofree gchar *uri =
      file_uri(g_ptr_array_index(self->files, i));
    g_hash_table_add(allowed, g_steal_pointer(&uri));
  }

  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, state.entries);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    if (!g_hash_table_contains(allowed, key)) {
      *state.size -= cache_entry_size(kind, value);
      g_hash_table_iter_remove(&iter);
    }
  }

  if (*state.size <= state.limit)
    return;

  PruneContext context = {
    .window = self,
    .kind = kind,
    .limit = state.limit,
  };
  losles_cache_policy_foreach_eviction(self->current_index,
                                       self->files->len,
                                       PRELOAD_DISTANCE,
                                       self->navigation_direction,
                                       prune_cache_index,
                                       &context);
}

typedef struct {
  LoslesWindow *window;
  CacheKind kind;
} CachePolicyContext;

static gsize
cache_policy_index_size(guint index, gpointer user_data)
{
  CachePolicyContext *context = user_data;
  gpointer entry =
    cache_entry_at_index(context->window, context->kind, index);
  return entry ? cache_entry_size(context->kind, entry) : 0;
}

static gboolean
cache_policy_evict_index(guint index, gpointer user_data)
{
  CachePolicyContext *context = user_data;
  evict_cache_index(context->window, context->kind, index);
  return TRUE;
}

static gboolean
prepare_cache_admission(LoslesWindow *self,
                        CacheKind kind,
                        guint candidate_index,
                        const gchar *candidate_uri,
                        gsize candidate_size,
                        gboolean foreground)
{
  prune_cache_kind(self, kind);

  if (!self->files || candidate_index >= self->files->len)
    return FALSE;
  const guint distance =
    candidate_index > self->current_index
      ? candidate_index - self->current_index
      : self->current_index - candidate_index;
  if (distance > PRELOAD_DISTANCE)
    return FALSE;

  CachePolicyContext context = {
    .window = self,
    .kind = kind,
  };
  const CacheState state = cache_kind_state(self, kind);
  const gboolean admitted =
    losles_cache_policy_admit(self->current_index,
                              self->files->len,
                              PRELOAD_DISTANCE,
                              self->navigation_direction,
                              candidate_index,
                              *state.size,
                              candidate_size,
                              state.limit,
                              foreground,
                              cache_policy_index_size,
                              cache_policy_evict_index,
                              &context);
  if (!admitted)
    g_hash_table_add(state.capacity_blocked,
                     g_strdup(candidate_uri));
  return admitted;
}

static void
prune_cache(LoslesWindow *self)
{
  prune_cache_kind(self, CACHE_SOURCE);
}

static void
prune_render_cache(LoslesWindow *self)
{
  prune_cache_kind(self, CACHE_RENDER);
}

static gboolean
cache_image(LoslesWindow *self,
            const gchar *uri,
            LoslesImage *image,
            gboolean foreground)
{
  LoslesImage *old = g_hash_table_lookup(self->cache, uri);
  if (old) {
    self->cache_size -= losles_image_get_memory_size(old);
    g_hash_table_remove(self->cache, uri);
  }

  const gint candidate_index =
    find_file_index(self->files, losles_image_get_file(image));
  const gsize size = losles_image_get_memory_size(image);
  if (candidate_index < 0 ||
      !prepare_cache_admission(self,
                               CACHE_SOURCE,
                               (guint)candidate_index,
                               uri,
                               size,
                               foreground))
    return FALSE;

  self->cache_size += size;
  g_hash_table_replace(self->cache, g_strdup(uri), g_object_ref(image));
  g_hash_table_remove(self->decode_capacity_blocked, uri);
  return TRUE;
}

static RenderCacheEntry *
cache_rendered(LoslesWindow *self,
               const gchar *uri,
               LoslesImage *image,
               LoslesRenderedImage *rendered,
               gboolean foreground)
{
  RenderCacheEntry *old =
    g_hash_table_lookup(self->render_cache, uri);
  if (old) {
    self->render_cache_size -= old->size;
    g_hash_table_remove(self->render_cache, uri);
  }

  const gsize size = g_bytes_get_size(rendered->pixels);
  const gint candidate_index =
    find_file_index(self->files, losles_image_get_file(image));
  if (candidate_index < 0 ||
      !prepare_cache_admission(self,
                               CACHE_RENDER,
                               (guint)candidate_index,
                               uri,
                               size,
                               foreground))
    return NULL;

  RenderCacheEntry *entry = g_new0(RenderCacheEntry, 1);
  entry->rendered = rendered;
  entry->texture = losles_rendered_image_create_texture(rendered);
  entry->size = size;
  self->render_cache_size += size;
  g_hash_table_replace(self->render_cache, g_strdup(uri), entry);
  g_hash_table_remove(self->render_capacity_blocked, uri);
  return entry;
}

static void
load_worker(GTask *task,
            gpointer source_object,
            gpointer task_data,
            GCancellable *cancellable)
{
  (void)source_object;
  LoadJob *job = task_data;
  g_autoptr(GError) error = NULL;
  LoslesImage *image =
    losles_format_registry_load(job->registry,
                                job->file,
                                cancellable,
                                &error);
  if (!image)
    g_task_return_error(task, g_steal_pointer(&error));
  else
    g_task_return_pointer(task, image, g_object_unref);
}

static void
update_title(LoslesWindow *self, GFile *file)
{
  g_autofree gchar *basename = g_file_get_basename(file);
  g_autofree gchar *title =
    g_strdup_printf("%s — losles", basename ? basename : "Image");
  gtk_window_set_title(GTK_WINDOW(self), title);
}

static void
set_current_image(LoslesWindow *self, LoslesImage *image)
{
  if (self->current_image == image)
    return;

  g_set_object(&self->current_image, image);
  self->crop_valid = FALSE;
  gtk_toggle_button_set_active(self->crop_button, FALSE);
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
  update_controls(self);
  start_render(self);
}

static void
load_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;
  LoslesWindow *self = LOSLES_WINDOW(source_object);
  GTask *task = G_TASK(result);
  LoadJob *job = g_task_get_task_data(task);
  g_autoptr(GError) error = NULL;
  g_autoptr(LoslesImage) image = g_task_propagate_pointer(task, &error);

  if (job->generation != self->generation)
    return;

  g_hash_table_remove(self->inflight, job->uri);
  if (!image) {
    g_hash_table_add(self->decode_failed, g_strdup(job->uri));
    if (is_current_file(self, job->file)) {
      clear_current_image(self);
      gtk_spinner_stop(self->spinner);
      gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
      show_error(self, "Could not open the image", error);
    }
    preload_neighbors(self);
    return;
  }

  /*
   * A low-priority neighbor load can become the foreground load while it is
   * in flight.  Decide from the current file, not from how the task started.
  */
  const gboolean foreground = is_current_file(self, job->file);
  const gboolean cached =
    cache_image(self, job->uri, image, foreground);
  if (foreground)
    set_current_image(self, image);
  else if (cached)
    start_render_for_image(self, image, FALSE);
  preload_neighbors(self);
}

static void
start_load(LoslesWindow *self, GFile *file, gboolean foreground)
{
  g_autofree gchar *uri = file_uri(file);
  LoslesImage *cached = g_hash_table_lookup(self->cache, uri);
  if (cached) {
    if (foreground)
      set_current_image(self, cached);
    return;
  }

  if (g_hash_table_contains(self->inflight, uri))
    return;
  if (g_hash_table_contains(self->decode_failed, uri)) {
    if (!foreground)
      return;
    g_hash_table_remove(self->decode_failed, uri);
  }
  if (g_hash_table_contains(self->decode_capacity_blocked, uri)) {
    if (!foreground)
      return;
    g_hash_table_remove(self->decode_capacity_blocked, uri);
  }
  if (!foreground &&
      g_hash_table_size(self->inflight) >= MAX_CONCURRENT_DECODES)
    return;

  g_hash_table_add(self->inflight, g_strdup(uri));
  LoadJob *job = g_new0(LoadJob, 1);
  job->registry = g_object_ref(self->registry);
  job->file = g_object_ref(file);
  job->uri = g_strdup(uri);
  job->generation = self->generation;

  GTask *task =
    g_task_new(self, self->load_cancellable, load_done, NULL);
  g_task_set_task_data(task, job, (GDestroyNotify)load_job_free);
  g_task_set_priority(task,
                      foreground ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW);
  g_task_run_in_thread(task, load_worker);
  g_object_unref(task);
}

static void
clear_capacity_blocks(LoslesWindow *self)
{
  g_hash_table_remove_all(self->decode_capacity_blocked);
  g_hash_table_remove_all(self->render_capacity_blocked);
}

typedef struct {
  LoslesWindow *window;
} PreloadContext;

static gboolean
preload_source_index(guint index, gpointer user_data)
{
  PreloadContext *context = user_data;
  start_load(context->window,
             g_ptr_array_index(context->window->files, index),
             FALSE);
  return TRUE;
}

static void
preload_neighbors(LoslesWindow *self)
{
  if (!self->files)
    return;

  PreloadContext context = {.window = self};
  losles_cache_policy_foreach_preload(self->current_index,
                                      self->files->len,
                                      PRELOAD_DISTANCE,
                                      self->navigation_direction,
                                      preload_source_index,
                                      &context);
  preload_rendered_neighbors(self);
}

static gint
compare_files(gconstpointer a, gconstpointer b)
{
  GFile *file_a = *(GFile *const *)a;
  GFile *file_b = *(GFile *const *)b;
  g_autofree gchar *name_a = g_file_get_basename(file_a);
  g_autofree gchar *name_b = g_file_get_basename(file_b);
  return g_utf8_collate(name_a ? name_a : "", name_b ? name_b : "");
}

static GPtrArray *
scan_supported_files(GFile *directory,
                     GCancellable *cancellable,
                     GError **error)
{
  g_autoptr(GFileEnumerator) enumerator =
    g_file_enumerate_children(directory,
                              G_FILE_ATTRIBUTE_STANDARD_NAME ","
                              G_FILE_ATTRIBUTE_STANDARD_TYPE,
                              G_FILE_QUERY_INFO_NONE,
                              cancellable,
                              error);
  if (!enumerator)
    return NULL;

  GPtrArray *files =
    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  while (TRUE) {
    g_autoptr(GFileInfo) info =
      g_file_enumerator_next_file(enumerator, cancellable, error);
    if (!info)
      break;
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
      continue;

    GFile *child =
      g_file_get_child(directory, g_file_info_get_name(info));
    if (losles_format_registry_supports_file(child))
      g_ptr_array_add(files, child);
    else
      g_object_unref(child);
  }

  if (error && *error) {
    g_ptr_array_unref(files);
    return NULL;
  }

  g_ptr_array_sort(files, compare_files);
  return files;
}

static void
scan_worker(GTask *task,
            gpointer source_object,
            gpointer task_data,
            GCancellable *cancellable)
{
  (void)source_object;
  ScanJob *job = task_data;
  g_autoptr(GError) error = NULL;
  GPtrArray *files =
    scan_supported_files(job->directory,
                         cancellable,
                         &error);
  if (!files) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  g_task_return_pointer(task, files, (GDestroyNotify)g_ptr_array_unref);
}

static void
delete_worker(GTask *task,
              gpointer source_object,
              gpointer task_data,
              GCancellable *cancellable)
{
  (void)source_object;
  DeleteJob *job = task_data;
  g_autoptr(GError) error = NULL;
  if (!losles_platform_trash_file(job->file,
                                  cancellable,
                                  &error)) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  DeleteResult *result = g_new0(DeleteResult, 1);
  if (job->directory) {
    result->files =
      scan_supported_files(job->directory,
                           cancellable,
                           &result->scan_error);
  } else {
    result->files =
      g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  }
  g_task_return_pointer(task,
                        result,
                        (GDestroyNotify)delete_result_free);
}

static void
scan_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;
  LoslesWindow *self = LOSLES_WINDOW(source_object);
  GTask *task = G_TASK(result);
  ScanJob *job = g_task_get_task_data(task);
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) files =
    g_task_propagate_pointer(task, &error);

  if (job->generation != self->generation)
    return;
  if (!files) {
    g_debug("Could not scan image directory: %s", error->message);
    return;
  }

  const gint index = find_file_index(files, job->opened_file);
  if (index < 0)
    g_ptr_array_add(files, g_object_ref(job->opened_file));

  g_clear_pointer(&self->files, g_ptr_array_unref);
  self->files = g_steal_pointer(&files);
  self->current_index =
    index >= 0 ? (guint)index : self->files->len - 1;
  update_controls(self);
  clear_capacity_blocks(self);
  prune_cache(self);
  prune_render_cache(self);
  preload_neighbors(self);
}

static void
scan_directory(LoslesWindow *self, GFile *file)
{
  g_autoptr(GFile) directory = g_file_get_parent(file);
  if (!directory)
    return;

  ScanJob *job = g_new0(ScanJob, 1);
  job->opened_file = g_object_ref(file);
  job->directory = g_object_ref(directory);
  job->generation = self->generation;

  GTask *task =
    g_task_new(self, self->load_cancellable, scan_done, NULL);
  g_task_set_task_data(task, job, (GDestroyNotify)scan_job_free);
  g_task_set_priority(task, G_PRIORITY_LOW);
  g_task_run_in_thread(task, scan_worker);
  g_object_unref(task);
}

static GdkMonitor *
current_monitor(LoslesWindow *self)
{
  GdkSurface *surface =
    gtk_native_get_surface(GTK_NATIVE(self));
  if (!surface)
    return NULL;
  return gdk_display_get_monitor_at_surface(gtk_widget_get_display(
                                              GTK_WIDGET(self)),
                                            surface);
}

static void
render_worker(GTask *task,
              gpointer source_object,
              gpointer task_data,
              GCancellable *cancellable)
{
  (void)source_object;
  RenderJob *job = task_data;
  g_autoptr(GError) error = NULL;
  LoslesRenderedImage *rendered =
    losles_color_target_render(job->target,
                               job->image,
                               cancellable,
                               &error);
  if (!rendered)
    g_task_return_error(task, g_steal_pointer(&error));
  else
    g_task_return_pointer(task,
                          rendered,
                          (GDestroyNotify)losles_rendered_image_free);
}

static void
display_rendered_image(LoslesWindow *self,
                       LoslesImage *image,
                       LoslesRenderedImage *rendered,
                       GdkTexture *cached_texture)
{
  if (!self->current_image ||
      !is_current_file(self, losles_image_get_file(image)))
    return;

  gtk_spinner_stop(self->spinner);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);

  const gboolean preserve_zoom =
    self->reset_zoom_on_next_display &&
    self->preserve_zoom_if_same_dimensions &&
    self->current_texture &&
    (guint)gdk_texture_get_width(self->current_texture) == rendered->width &&
    (guint)gdk_texture_get_height(self->current_texture) == rendered->height;

  g_clear_object(&self->current_texture);
  if (cached_texture)
    self->current_texture = g_object_ref(cached_texture);
  else
    self->current_texture =
      losles_rendered_image_create_texture(rendered);
  gtk_picture_set_paintable(self->picture,
                            GDK_PAINTABLE(self->current_texture));
  if (self->reset_zoom_on_next_display && !preserve_zoom)
    reset_zoom(self);
  else
    apply_zoom_layout(self);
  self->reset_zoom_on_next_display = FALSE;
  self->preserve_zoom_if_same_dimensions = FALSE;
  self->foreground_loading = FALSE;

  const LoslesPixelFormat source_format =
    losles_image_get_pixel_format(image);
  const gchar *source_description =
    rendered->used_embedded_profile
      ? "embedded ICC"
      : (source_format == LOSLES_PIXEL_FORMAT_G8 ||
         source_format == LOSLES_PIXEL_FORMAT_GA8)
          ? "assumed D65 gray"
          : "assumed sRGB";
  g_autofree gchar *status =
    g_strdup_printf("%s  •  %u×%u  •  %s → %s%s",
                    losles_image_get_format_name(image),
                    losles_image_get_display_width(image),
                    losles_image_get_display_height(image),
                    source_description,
                    rendered->display_profile_name,
                    g_str_equal(rendered->display_profile_id,
                                "builtin-srgb")
                      ? " (no display profile found)"
                      : "");
  set_status(self, status);
  update_controls(self);
}

static void
render_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;
  LoslesWindow *self = LOSLES_WINDOW(source_object);
  GTask *task = G_TASK(result);
  RenderJob *job = g_task_get_task_data(task);
  g_autoptr(GError) error = NULL;
  g_autoptr(LoslesRenderedImage) rendered =
    g_task_propagate_pointer(task, &error);

  if (job->generation != self->render_generation)
    return;

  g_hash_table_remove(self->render_inflight, job->uri);
  const gboolean foreground =
    is_current_file(self, losles_image_get_file(job->image));
  if (!rendered) {
    g_hash_table_add(self->render_failed, g_strdup(job->uri));
    if (foreground &&
        !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      clear_current_image(self);
      gtk_spinner_stop(self->spinner);
      gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
      show_error(self, "Could not color-manage the image", error);
    }
    preload_rendered_neighbors(self);
    return;
  }

  RenderCacheEntry *entry =
    cache_rendered(self,
                   job->uri,
                   job->image,
                   rendered,
                   foreground);
  if (entry)
    g_steal_pointer(&rendered);

  if (foreground) {
    if (entry)
      display_rendered_image(self,
                             job->image,
                             entry->rendered,
                             entry->texture);
    else
      display_rendered_image(self, job->image, rendered, NULL);
  }
  preload_rendered_neighbors(self);
}

static void
start_render_for_image(LoslesWindow *self,
                       LoslesImage *image,
                       gboolean foreground)
{
  if (!gtk_widget_get_mapped(GTK_WIDGET(self)))
    return;

  GdkSurface *surface =
    gtk_native_get_surface(GTK_NATIVE(self));
  g_autoptr(LoslesColorTarget) target =
    losles_color_manager_get_target(self->color_manager,
                                    current_monitor(self),
                                    surface);
  if (!target) {
    if (foreground) {
      clear_current_image(self);
      gtk_spinner_stop(self->spinner);
      gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
      set_status(self, "No usable display color profile");
    }
    return;
  }
  const gchar *target_profile_id =
    losles_color_target_get_id(target);
  if (self->render_profile_id &&
      g_strcmp0(self->render_profile_id, target_profile_id) != 0)
    invalidate_render_cache(self);
  if (g_strcmp0(self->render_profile_id, target_profile_id) != 0) {
    g_free(self->render_profile_id);
    self->render_profile_id = g_strdup(target_profile_id);
  }

  g_autofree gchar *uri =
    file_uri(losles_image_get_file(image));
  RenderCacheEntry *cached =
    g_hash_table_lookup(self->render_cache, uri);
  if (cached) {
    if (g_strcmp0(cached->rendered->display_profile_id,
                  target_profile_id) == 0) {
      if (foreground)
        display_rendered_image(self,
                               image,
                               cached->rendered,
                               cached->texture);
      return;
    }

    /*
     * A platform profile changed without a monitor-enter/leave signal. Drop
     * every converted result because all of them target the old profile.
     */
    invalidate_render_cache(self);
  }

  if (g_hash_table_contains(self->render_inflight, uri)) {
    if (foreground) {
      self->foreground_loading = TRUE;
      gtk_widget_set_visible(GTK_WIDGET(self->spinner), TRUE);
      gtk_spinner_start(self->spinner);
      set_status(self, "Finishing prefetched color conversion…");
      update_controls(self);
    }
    return;
  }
  if (g_hash_table_contains(self->render_failed, uri)) {
    if (!foreground)
      return;
    g_hash_table_remove(self->render_failed, uri);
  }
  if (g_hash_table_contains(self->render_capacity_blocked, uri)) {
    if (!foreground)
      return;
    g_hash_table_remove(self->render_capacity_blocked, uri);
  }
  if (!foreground &&
      g_hash_table_size(self->render_inflight) >=
        MAX_CONCURRENT_RENDERS)
    return;

  if (foreground) {
    self->foreground_loading = TRUE;
    gtk_widget_set_visible(GTK_WIDGET(self->spinner), TRUE);
    gtk_spinner_start(self->spinner);
    set_status(self, "Converting to the active display profile…");
    update_controls(self);
  }

  RenderJob *job = g_new0(RenderJob, 1);
  job->target = losles_color_target_ref(target);
  job->image = g_object_ref(image);
  job->uri = g_strdup(uri);
  job->generation = self->render_generation;

  g_hash_table_add(self->render_inflight, g_strdup(uri));
  GTask *task =
    g_task_new(self, self->render_cancellable, render_done, NULL);
  g_task_set_task_data(task, job, (GDestroyNotify)render_job_free);
  g_task_set_priority(task,
                      foreground ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW);
  g_task_run_in_thread(task, render_worker);
  g_object_unref(task);
}

static gboolean
preload_render_index(guint index, gpointer user_data)
{
  PreloadContext *context = user_data;
  GFile *file =
    g_ptr_array_index(context->window->files, index);
  g_autofree gchar *uri = file_uri(file);
  LoslesImage *image =
    g_hash_table_lookup(context->window->cache, uri);
  if (image)
    start_render_for_image(context->window, image, FALSE);
  return TRUE;
}

static void
preload_rendered_neighbors(LoslesWindow *self)
{
  if (!self->files || !gtk_widget_get_mapped(GTK_WIDGET(self)))
    return;

  PreloadContext context = {.window = self};
  losles_cache_policy_foreach_preload(self->current_index,
                                      self->files->len,
                                      PRELOAD_DISTANCE,
                                      self->navigation_direction,
                                      preload_render_index,
                                      &context);
}

static void
start_render(LoslesWindow *self)
{
  if (!self->current_image)
    return;
  start_render_for_image(self, self->current_image, TRUE);
  preload_rendered_neighbors(self);
}

static void
invalidate_render_cache(LoslesWindow *self)
{
  if (self->render_cancellable)
    g_cancellable_cancel(self->render_cancellable);
  g_clear_object(&self->render_cancellable);
  self->render_cancellable = g_cancellable_new();
  self->render_generation++;
  g_hash_table_remove_all(self->render_inflight);
  g_hash_table_remove_all(self->render_failed);
  g_hash_table_remove_all(self->render_capacity_blocked);
  g_hash_table_remove_all(self->render_cache);
  self->render_cache_size = 0;
}

static void
reset_content_pipeline(LoslesWindow *self)
{
  self->navigation_direction = 1;
  self->generation++;
  if (self->load_cancellable)
    g_cancellable_cancel(self->load_cancellable);
  g_clear_object(&self->load_cancellable);
  self->load_cancellable = g_cancellable_new();
  g_hash_table_remove_all(self->inflight);
  g_hash_table_remove_all(self->decode_failed);
  g_hash_table_remove_all(self->decode_capacity_blocked);
  g_hash_table_remove_all(self->cache);
  self->cache_size = 0;
  invalidate_render_cache(self);
}

static void
advance_pipeline_after_delete(LoslesWindow *self, GFile *deleted_file)
{
  g_autofree gchar *uri = file_uri(deleted_file);

  self->navigation_direction = 1;
  self->generation++;
  if (self->load_cancellable)
    g_cancellable_cancel(self->load_cancellable);
  g_clear_object(&self->load_cancellable);
  self->load_cancellable = g_cancellable_new();
  g_hash_table_remove_all(self->inflight);
  g_hash_table_remove_all(self->decode_failed);
  g_hash_table_remove_all(self->decode_capacity_blocked);

  LoslesImage *cached = g_hash_table_lookup(self->cache, uri);
  if (cached)
    self->cache_size -= losles_image_get_memory_size(cached);
  g_hash_table_remove(self->cache, uri);

  if (self->render_cancellable)
    g_cancellable_cancel(self->render_cancellable);
  g_clear_object(&self->render_cancellable);
  self->render_cancellable = g_cancellable_new();
  self->render_generation++;
  g_hash_table_remove_all(self->render_inflight);
  g_hash_table_remove_all(self->render_failed);
  g_hash_table_remove_all(self->render_capacity_blocked);

  RenderCacheEntry *rendered =
    g_hash_table_lookup(self->render_cache, uri);
  if (rendered)
    self->render_cache_size -= rendered->size;
  g_hash_table_remove(self->render_cache, uri);
}

static void
show_no_picture(LoslesWindow *self)
{
  g_clear_pointer(&self->files, g_ptr_array_unref);
  self->files =
    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  self->current_index = 0;
  self->navigation_direction = 1;
  clear_current_image(self);
  gtk_spinner_stop(self->spinner);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
  gtk_window_set_title(GTK_WINDOW(self), "losles");
  set_status(self, "No picture opened");
  update_controls(self);
}

static void
show_index(LoslesWindow *self,
           guint index,
           gboolean preserve_zoom_if_same_dimensions)
{
  if (!self->files || index >= self->files->len)
    return;

  if (index < self->current_index)
    self->navigation_direction = -1;
  else if (index > self->current_index)
    self->navigation_direction = 1;
  self->current_index = index;
  clear_capacity_blocks(self);
  prune_cache(self);
  prune_render_cache(self);

  GFile *file = current_file(self);
  update_title(self, file);
  prepare_for_image_load(self, preserve_zoom_if_same_dimensions);
  g_autofree gchar *uri = file_uri(file);
  LoslesImage *cached_image =
    g_hash_table_lookup(self->cache, uri);
  RenderCacheEntry *cached_render =
    g_hash_table_lookup(self->render_cache, uri);
  if (cached_image && cached_render) {
    g_set_object(&self->current_image, cached_image);
    display_rendered_image(self,
                           cached_image,
                           cached_render->rendered,
                           cached_render->texture);
  } else {
    gtk_widget_set_visible(GTK_WIDGET(self->spinner), TRUE);
    gtk_spinner_start(self->spinner);
    set_status(self, "Loading…");
    start_load(self, file, TRUE);
  }
  update_controls(self);
  preload_neighbors(self);
}

static void
previous_image(LoslesWindow *self)
{
  if (!self->operation_in_progress && self->current_index > 0)
    show_index(self, self->current_index - 1, TRUE);
}

static void
next_image(LoslesWindow *self)
{
  if (!self->operation_in_progress &&
      self->files &&
      self->current_index + 1 < self->files->len)
    show_index(self, self->current_index + 1, TRUE);
}

static gboolean
is_mouse_navigation_button(guint button,
                           gboolean *back)
{
  /*
   * X11 and Wayland conventionally expose the two thumb buttons as 8 and 9.
   * GTK's Win32 backend has also exposed XBUTTON1/2 as 4 and 5, so accept
   * both pairs there. Wheel input is delivered as a scroll event, not one of
   * these button events.
   */
#ifdef G_OS_WIN32
  if (button == 4 || button == 5) {
    *back = button == 4;
    return TRUE;
  }
#endif
  if (button == MOUSE_BUTTON_BACK ||
      button == MOUSE_BUTTON_FORWARD) {
    *back = button == MOUSE_BUTTON_BACK;
    return TRUE;
  }
  return FALSE;
}

static gboolean
mouse_navigation_event(GtkEventControllerLegacy *controller,
                       GdkEvent *event,
                       LoslesWindow *self)
{
  (void)controller;
  const GdkEventType event_type = gdk_event_get_event_type(event);
  if (event_type != GDK_BUTTON_PRESS &&
      event_type != GDK_BUTTON_RELEASE)
    return FALSE;

  gboolean back = FALSE;
  if (!is_mouse_navigation_button(gdk_button_event_get_button(event),
                                  &back))
    return FALSE;

  if (event_type == GDK_BUTTON_PRESS) {
    if (back)
      previous_image(self);
    else
      next_image(self);
  }
  return TRUE;
}

static gboolean
crop_geometry(LoslesWindow *self,
              gdouble area_width,
              gdouble area_height,
              CropGeometry *geometry)
{
  if (!self->current_image)
    return FALSE;

  const gdouble image_width =
    losles_image_get_display_width(self->current_image);
  const gdouble image_height =
    losles_image_get_display_height(self->current_image);
  if (area_width <= 0 || area_height <= 0 ||
      image_width <= 0 || image_height <= 0)
    return FALSE;

  geometry->image_width = image_width;
  geometry->image_height = image_height;
  geometry->scale =
    MIN(area_width / image_width, area_height / image_height);
  geometry->shown_width = image_width * geometry->scale;
  geometry->shown_height = image_height * geometry->scale;
  geometry->left = (area_width - geometry->shown_width) / 2.0;
  geometry->top = (area_height - geometry->shown_height) / 2.0;
  return TRUE;
}

static gboolean
widget_to_image(LoslesWindow *self,
                gdouble widget_x,
                gdouble widget_y,
                gdouble *image_x,
                gdouble *image_y)
{
  CropGeometry geometry = {0};
  if (!crop_geometry(self,
                     gtk_widget_get_width(GTK_WIDGET(self->crop_area)),
                     gtk_widget_get_height(GTK_WIDGET(self->crop_area)),
                     &geometry))
    return FALSE;

  const gboolean inside =
    widget_x >= geometry.left && widget_y >= geometry.top &&
    widget_x <= geometry.left + geometry.shown_width &&
    widget_y <= geometry.top + geometry.shown_height;
  *image_x =
    CLAMP((widget_x - geometry.left) / geometry.scale,
          0,
          geometry.image_width);
  *image_y =
    CLAMP((widget_y - geometry.top) / geometry.scale,
          0,
          geometry.image_height);
  return inside;
}

static CropDragMode
crop_hit_test(LoslesWindow *self, gdouble widget_x, gdouble widget_y)
{
  CropGeometry geometry = {0};
  if (!crop_geometry(self,
                     gtk_widget_get_width(GTK_WIDGET(self->crop_area)),
                     gtk_widget_get_height(GTK_WIDGET(self->crop_area)),
                     &geometry))
    return CROP_DRAG_NONE;

  const gboolean inside_image =
    widget_x >= geometry.left && widget_y >= geometry.top &&
    widget_x <= geometry.left + geometry.shown_width &&
    widget_y <= geometry.top + geometry.shown_height;
  if (!self->crop_valid)
    return inside_image ? CROP_DRAG_NEW : CROP_DRAG_NONE;

  const gdouble left =
    geometry.left + self->crop.x * geometry.scale;
  const gdouble top =
    geometry.top + self->crop.y * geometry.scale;
  const gdouble right =
    left + self->crop.width * geometry.scale;
  const gdouble bottom =
    top + self->crop.height * geometry.scale;
  const gboolean near_left =
    ABS(widget_x - left) <= CROP_HIT_TOLERANCE;
  const gboolean near_right =
    ABS(widget_x - right) <= CROP_HIT_TOLERANCE;
  const gboolean near_top =
    ABS(widget_y - top) <= CROP_HIT_TOLERANCE;
  const gboolean near_bottom =
    ABS(widget_y - bottom) <= CROP_HIT_TOLERANCE;
  const gboolean within_x =
    widget_x >= left - CROP_HIT_TOLERANCE &&
    widget_x <= right + CROP_HIT_TOLERANCE;
  const gboolean within_y =
    widget_y >= top - CROP_HIT_TOLERANCE &&
    widget_y <= bottom + CROP_HIT_TOLERANCE;

  if (near_left && near_top)
    return CROP_DRAG_TOP_LEFT;
  if (near_right && near_top)
    return CROP_DRAG_TOP_RIGHT;
  if (near_left && near_bottom)
    return CROP_DRAG_BOTTOM_LEFT;
  if (near_right && near_bottom)
    return CROP_DRAG_BOTTOM_RIGHT;
  if (near_left && within_y)
    return CROP_DRAG_LEFT;
  if (near_right && within_y)
    return CROP_DRAG_RIGHT;
  if (near_top && within_x)
    return CROP_DRAG_TOP;
  if (near_bottom && within_x)
    return CROP_DRAG_BOTTOM;
  if (widget_x >= left && widget_x <= right &&
      widget_y >= top && widget_y <= bottom)
    return CROP_DRAG_MOVE;
  return inside_image ? CROP_DRAG_NEW : CROP_DRAG_NONE;
}

static const gchar *
crop_cursor_name(CropDragMode mode)
{
  switch (mode) {
  case CROP_DRAG_MOVE:
    return "move";
  case CROP_DRAG_LEFT:
  case CROP_DRAG_RIGHT:
    return "ew-resize";
  case CROP_DRAG_TOP:
  case CROP_DRAG_BOTTOM:
    return "ns-resize";
  case CROP_DRAG_TOP_LEFT:
  case CROP_DRAG_BOTTOM_RIGHT:
    return "nwse-resize";
  case CROP_DRAG_TOP_RIGHT:
  case CROP_DRAG_BOTTOM_LEFT:
    return "nesw-resize";
  case CROP_DRAG_NEW:
    return "crosshair";
  case CROP_DRAG_NONE:
  default:
    return NULL;
  }
}

static void
set_crop_cursor(LoslesWindow *self, CropDragMode mode)
{
  gtk_widget_set_cursor_from_name(GTK_WIDGET(self->crop_area),
                                  crop_cursor_name(mode));
}

static gboolean
crop_is_perfect(LoslesWindow *self, const LoslesCrop *crop)
{
  LoslesFormat *format =
    LOSLES_FORMAT(losles_image_get_format(self->current_image));
  LoslesCrop adjusted = {0};
  return losles_format_adjust_crop(format,
                                   self->current_image,
                                   crop,
                                   &adjusted,
                                   NULL) &&
         adjusted.x == crop->x &&
         adjusted.y == crop->y &&
         adjusted.width == crop->width &&
         adjusted.height == crop->height;
}

static guint
nearest_crop_boundary(gdouble position,
                      guint lower,
                      guint upper,
                      guint maximum,
                      guint fallback)
{
  const gboolean lower_valid = lower <= maximum;
  const gboolean upper_valid = upper <= maximum;
  if (!lower_valid && !upper_valid)
    return fallback;
  if (!lower_valid)
    return upper;
  if (!upper_valid)
    return lower;
  return ABS(position - lower) <= ABS(upper - position) ? lower : upper;
}

static LoslesCrop
crop_snap_move(LoslesWindow *self,
               gdouble desired_left,
               gdouble desired_top)
{
  const LoslesCrop initial = self->crop_drag_initial;
  const guint image_width =
    losles_image_get_display_width(self->current_image);
  const guint image_height =
    losles_image_get_display_height(self->current_image);
  const guint maximum_x = image_width - initial.width;
  const guint maximum_y = image_height - initial.height;

  LoslesFormat *format =
    LOSLES_FORMAT(losles_image_get_format(self->current_image));
  LoslesCrop point = {
    .x = MIN((guint)(desired_left + 0.5), image_width - 1),
    .y = MIN((guint)(desired_top + 0.5), image_height - 1),
    .width = 1,
    .height = 1,
  };
  LoslesCrop cell = {0};
  if (!losles_format_adjust_crop(format,
                                 self->current_image,
                                 &point,
                                 &cell,
                                 NULL))
    return initial;

  const guint candidate_x =
    nearest_crop_boundary(desired_left,
                          cell.x,
                          cell.x + cell.width,
                          maximum_x,
                          initial.x);
  const guint candidate_y =
    nearest_crop_boundary(desired_top,
                          cell.y,
                          cell.y + cell.height,
                          maximum_y,
                          initial.y);

  LoslesCrop snapped = initial;
  LoslesCrop candidate = initial;
  candidate.x = candidate_x;
  candidate.y = candidate_y;
  if (crop_is_perfect(self, &candidate))
    return candidate;

  candidate = initial;
  candidate.x = candidate_x;
  if (crop_is_perfect(self, &candidate))
    snapped.x = candidate_x;

  candidate = snapped;
  candidate.y = candidate_y;
  if (crop_is_perfect(self, &candidate))
    snapped.y = candidate_y;
  return snapped;
}

static void
crop_set_bounds(LoslesWindow *self,
                gdouble left,
                gdouble top,
                gdouble right,
                gdouble bottom)
{
  if (!self->current_image)
    return;

  const guint image_width =
    losles_image_get_display_width(self->current_image);
  const guint image_height =
    losles_image_get_display_height(self->current_image);
  const guint integer_left =
    (guint)(CLAMP(left, 0, image_width) + 0.5);
  const guint integer_top =
    (guint)(CLAMP(top, 0, image_height) + 0.5);
  const guint integer_right =
    (guint)(CLAMP(right, 0, image_width) + 0.5);
  const guint integer_bottom =
    (guint)(CLAMP(bottom, 0, image_height) + 0.5);

  LoslesCrop requested = {
    .x = MIN(integer_left, integer_right),
    .y = MIN(integer_top, integer_bottom),
  };
  requested.width =
    MAX(integer_left, integer_right) - requested.x;
  requested.height =
    MAX(integer_top, integer_bottom) - requested.y;

  self->crop = requested;
  self->crop_valid = FALSE;
  if (requested.width >= CROP_MIN_SIZE &&
      requested.height >= CROP_MIN_SIZE) {
    LoslesFormat *format =
      LOSLES_FORMAT(losles_image_get_format(self->current_image));
    LoslesCrop adjusted = {0};
    if (losles_format_adjust_crop(format,
                                  self->current_image,
                                  &requested,
                                  &adjusted,
                                  NULL)) {
      self->crop = adjusted;
      self->crop_valid =
        adjusted.width >= CROP_MIN_SIZE &&
        adjusted.height >= CROP_MIN_SIZE;
    }
  }
  gtk_widget_set_sensitive(GTK_WIDGET(self->apply_crop_button),
                           self->crop_valid);
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
}

static void
crop_update_from_pointer(LoslesWindow *self,
                         gdouble image_x,
                         gdouble image_y)
{
  if (!self->current_image || self->crop_drag_mode == CROP_DRAG_NONE)
    return;

  const gdouble image_width =
    losles_image_get_display_width(self->current_image);
  const gdouble image_height =
    losles_image_get_display_height(self->current_image);
  gdouble left = self->crop_drag_initial.x;
  gdouble top = self->crop_drag_initial.y;
  gdouble right = left + self->crop_drag_initial.width;
  gdouble bottom = top + self->crop_drag_initial.height;

  switch (self->crop_drag_mode) {
  case CROP_DRAG_NEW:
    left = MIN(self->crop_drag_origin_x, image_x);
    top = MIN(self->crop_drag_origin_y, image_y);
    right = MAX(self->crop_drag_origin_x, image_x);
    bottom = MAX(self->crop_drag_origin_y, image_y);
    break;
  case CROP_DRAG_MOVE: {
    const gdouble desired_left =
      CLAMP(left + image_x - self->crop_drag_origin_x,
            0,
            image_width - self->crop_drag_initial.width);
    const gdouble desired_top =
      CLAMP(top + image_y - self->crop_drag_origin_y,
            0,
            image_height - self->crop_drag_initial.height);
    self->crop = crop_snap_move(self, desired_left, desired_top);
    self->crop_valid = TRUE;
    gtk_widget_set_sensitive(GTK_WIDGET(self->apply_crop_button), TRUE);
    gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
    return;
  }
  case CROP_DRAG_LEFT:
  case CROP_DRAG_TOP_LEFT:
  case CROP_DRAG_BOTTOM_LEFT:
    left = CLAMP(image_x, 0, right - CROP_MIN_SIZE);
    break;
  case CROP_DRAG_RIGHT:
  case CROP_DRAG_TOP_RIGHT:
  case CROP_DRAG_BOTTOM_RIGHT:
    right = CLAMP(image_x, left + CROP_MIN_SIZE, image_width);
    break;
  case CROP_DRAG_NONE:
  case CROP_DRAG_TOP:
  case CROP_DRAG_BOTTOM:
    break;
  }

  switch (self->crop_drag_mode) {
  case CROP_DRAG_TOP:
  case CROP_DRAG_TOP_LEFT:
  case CROP_DRAG_TOP_RIGHT:
    top = CLAMP(image_y, 0, bottom - CROP_MIN_SIZE);
    break;
  case CROP_DRAG_BOTTOM:
  case CROP_DRAG_BOTTOM_LEFT:
  case CROP_DRAG_BOTTOM_RIGHT:
    bottom = CLAMP(image_y, top + CROP_MIN_SIZE, image_height);
    break;
  case CROP_DRAG_NONE:
  case CROP_DRAG_NEW:
  case CROP_DRAG_MOVE:
  case CROP_DRAG_LEFT:
  case CROP_DRAG_RIGHT:
    break;
  }

  crop_set_bounds(self, left, top, right, bottom);
}

static void
crop_drag_begin(GtkGestureDrag *gesture,
                gdouble start_x,
                gdouble start_y,
                LoslesWindow *self)
{
  (void)gesture;
  gdouble image_x = 0;
  gdouble image_y = 0;
  const gboolean inside_image =
    widget_to_image(self, start_x, start_y, &image_x, &image_y);
  const CropDragMode mode = crop_hit_test(self, start_x, start_y);
  if (!inside_image && mode == CROP_DRAG_NONE) {
    self->crop_drag_mode = CROP_DRAG_NONE;
    return;
  }

  self->crop_drag_mode = mode;
  self->crop_drag_origin_x = image_x;
  self->crop_drag_origin_y = image_y;
  self->crop_drag_initial = self->crop;
  if (self->crop_drag_mode == CROP_DRAG_NEW) {
    self->crop_drag_initial =
      (LoslesCrop){.x = (guint)(image_x + 0.5),
                   .y = (guint)(image_y + 0.5)};
    crop_set_bounds(self, image_x, image_y, image_x, image_y);
  }
  set_crop_cursor(self, self->crop_drag_mode);
}

static void
crop_drag_update(GtkGestureDrag *gesture,
                 gdouble offset_x,
                 gdouble offset_y,
                 LoslesWindow *self)
{
  if (self->crop_drag_mode == CROP_DRAG_NONE || !self->current_image)
    return;

  gdouble start_widget_x = 0;
  gdouble start_widget_y = 0;
  gtk_gesture_drag_get_start_point(gesture,
                                   &start_widget_x,
                                   &start_widget_y);
  gdouble image_x = 0;
  gdouble image_y = 0;
  widget_to_image(self,
                  start_widget_x + offset_x,
                  start_widget_y + offset_y,
                  &image_x,
                  &image_y);
  crop_update_from_pointer(self, image_x, image_y);
}

static void
crop_drag_end(GtkGestureDrag *gesture,
              gdouble offset_x,
              gdouble offset_y,
              LoslesWindow *self)
{
  gdouble start_widget_x = 0;
  gdouble start_widget_y = 0;
  gtk_gesture_drag_get_start_point(gesture,
                                   &start_widget_x,
                                   &start_widget_y);
  gdouble image_x = 0;
  gdouble image_y = 0;
  widget_to_image(self,
                  start_widget_x + offset_x,
                  start_widget_y + offset_y,
                  &image_x,
                  &image_y);
  crop_update_from_pointer(self, image_x, image_y);
  self->crop_drag_mode = CROP_DRAG_NONE;
  set_crop_cursor(self,
                  crop_hit_test(self,
                                start_widget_x + offset_x,
                                start_widget_y + offset_y));
}

static void
crop_motion(GtkEventControllerMotion *controller,
            gdouble x,
            gdouble y,
            LoslesWindow *self)
{
  (void)controller;
  if (self->crop_drag_mode == CROP_DRAG_NONE)
    set_crop_cursor(self, crop_hit_test(self, x, y));
}

static void
crop_pointer_left(GtkEventControllerMotion *controller,
                  LoslesWindow *self)
{
  (void)controller;
  if (self->crop_drag_mode == CROP_DRAG_NONE)
    set_crop_cursor(self, CROP_DRAG_NONE);
}

static void
crop_draw(GtkDrawingArea *area,
          cairo_t *cr,
          gint width,
          gint height,
          LoslesWindow *self)
{
  (void)area;
  if (!self->crop_valid || !self->current_image)
    return;

  CropGeometry geometry = {0};
  if (!crop_geometry(self, width, height, &geometry))
    return;

  const gdouble left =
    geometry.left + self->crop.x * geometry.scale;
  const gdouble top =
    geometry.top + self->crop.y * geometry.scale;
  const gdouble crop_width = self->crop.width * geometry.scale;
  const gdouble crop_height = self->crop.height * geometry.scale;

  cairo_rectangle(cr,
                  geometry.left,
                  geometry.top,
                  geometry.shown_width,
                  geometry.shown_height);
  cairo_rectangle(cr, left, top, crop_width, crop_height);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.48);
  cairo_fill(cr);

  cairo_rectangle(cr,
                  left + 0.5,
                  top + 0.5,
                  MAX(0, crop_width - 1),
                  MAX(0, crop_height - 1));
  cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  const gdouble handle_x[] = {
    left,
    left + crop_width / 2.0,
    left + crop_width,
  };
  const gdouble handle_y[] = {
    top,
    top + crop_height / 2.0,
    top + crop_height,
  };
  const gdouble handle_offset = CROP_HANDLE_SIZE / 2.0;
  for (guint row = 0; row < G_N_ELEMENTS(handle_y); row++) {
    for (guint column = 0; column < G_N_ELEMENTS(handle_x); column++) {
      if (row == 1 && column == 1)
        continue;
      cairo_rectangle(cr,
                      handle_x[column] - handle_offset,
                      handle_y[row] - handle_offset,
                      CROP_HANDLE_SIZE,
                      CROP_HANDLE_SIZE);
      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
    }
  }
}

static void
edit_worker(GTask *task,
            gpointer source_object,
            gpointer task_data,
            GCancellable *cancellable)
{
  (void)source_object;
  EditJob *job = task_data;
  LoslesFormat *format =
    LOSLES_FORMAT(losles_image_get_format(job->image));
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;
  if (job->kind == EDIT_ROTATE) {
    success = losles_format_rotate_lossless(format,
                                            job->image,
                                            job->destination,
                                            job->rotation,
                                            job->flags,
                                            cancellable,
                                            &error);
  } else if (job->kind == EDIT_NORMALIZE_ORIENTATION) {
    success = losles_format_normalize_orientation_lossless(
      format,
      job->image,
      job->destination,
      job->flags,
      cancellable,
      &error);
  } else {
    success = losles_format_crop_lossless(format,
                                          job->image,
                                          job->destination,
                                          &job->crop,
                                          job->flags,
                                          cancellable,
                                          &error);
  }

  if (!success)
    g_task_return_error(task, g_steal_pointer(&error));
  else
    g_task_return_boolean(task, TRUE);
}

static void
warning_confirmation_done(GObject *source_object,
                          GAsyncResult *result,
                          gpointer user_data)
{
  EditConfirmation *confirmation = user_data;
  LoslesWindow *self = confirmation->window;
  g_autoptr(GError) error = NULL;
  const gint response =
    gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source_object),
                                   result,
                                   &error);

  if (!error && response == 1) {
    confirmation->job->flags |=
      LOSLES_FORMAT_EDIT_ALLOW_RECOVERABLE_WARNINGS;
    EditJob *job = confirmation->job;
    confirmation->job = NULL;
    queue_edit(self, job);
    edit_confirmation_free(confirmation);
    return;
  }

  self->operation_in_progress = FALSE;
  update_controls(self);
  if (error)
    show_error(self, "Could not ask for confirmation", error);
  else
    set_status(self, "Operation cancelled");
  edit_confirmation_free(confirmation);
}

static void
request_warning_confirmation(LoslesWindow *self,
                             EditJob *job,
                             const GError *warning)
{
  const gchar *operation =
    job->kind == EDIT_ROTATE
      ? "rotation"
      : job->kind == EDIT_NORMALIZE_ORIENTATION
          ? "EXIF orientation correction"
          : "crop";
  g_autofree gchar *detail = g_strdup_printf(
    "TurboJPEG reported during %s:\n%s\n\n"
    "Continuing will ignore this warning. The operation will still rearrange "
    "JPEG coefficients without lossy re-encoding, but the result may be "
    "incomplete or damaged. The exact original file will be moved to Trash "
    "before the replacement is installed.",
    operation,
    warning->message);
  GtkAlertDialog *dialog =
    gtk_alert_dialog_new("Continue despite the JPEG warning?");
  const gchar *buttons[] = {"Cancel", "Continue", NULL};
  gtk_alert_dialog_set_detail(dialog, detail);
  gtk_alert_dialog_set_buttons(dialog, buttons);
  gtk_alert_dialog_set_cancel_button(dialog, 0);
  gtk_alert_dialog_set_default_button(dialog, 0);

  EditConfirmation *confirmation = g_new0(EditConfirmation, 1);
  confirmation->window = g_object_ref(self);
  confirmation->job = edit_job_copy(job);
  set_status(self, "Waiting for confirmation after a JPEG warning…");
  gtk_alert_dialog_choose(dialog,
                          GTK_WINDOW(self),
                          NULL,
                          warning_confirmation_done,
                          confirmation);
  g_object_unref(dialog);
}

static void
edit_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;
  LoslesWindow *self = LOSLES_WINDOW(source_object);
  GTask *task = G_TASK(result);
  EditJob *job = g_task_get_task_data(task);
  g_autoptr(GFile) destination = g_object_ref(job->destination);
  g_autoptr(GError) error = NULL;

  gtk_spinner_stop(self->spinner);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
  if (!g_task_propagate_boolean(task, &error)) {
    if (!(job->flags & LOSLES_FORMAT_EDIT_ALLOW_RECOVERABLE_WARNINGS) &&
        g_error_matches(
          error,
          LOSLES_FORMAT_ERROR,
          LOSLES_FORMAT_ERROR_WARNING_REQUIRES_CONFIRMATION)) {
      request_warning_confirmation(self, job, error);
      return;
    }

    self->operation_in_progress = FALSE;
    update_controls(self);
    show_error(self, "The lossless operation failed", error);
    return;
  }

  self->operation_in_progress = FALSE;
  update_controls(self);
  losles_window_open_file(self, destination);
}

static void
queue_edit(LoslesWindow *self, EditJob *job)
{
  update_controls(self);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), TRUE);
  gtk_spinner_start(self->spinner);
  const gboolean warning_retry =
    job->flags & LOSLES_FORMAT_EDIT_ALLOW_RECOVERABLE_WARNINGS;
  if (job->kind == EDIT_ROTATE)
    set_status(self,
               warning_retry
                 ? "Rotating despite a JPEG warning; moving the original "
                   "to Trash…"
                 : "Rotating losslessly in place…");
  else if (job->kind == EDIT_NORMALIZE_ORIENTATION)
    set_status(
      self,
      warning_retry
        ? "Correcting EXIF orientation despite a JPEG warning; moving the "
          "original to Trash…"
        : "Normalizing EXIF orientation losslessly in place…");
  else
    set_status(self,
               warning_retry
                 ? "Cropping despite a JPEG warning; moving the original "
                   "to Trash…"
                 : "Cropping losslessly; moving the original to Trash…");

  GTask *task = g_task_new(self, NULL, edit_done, NULL);
  g_task_set_task_data(task, job, (GDestroyNotify)edit_job_free);
  g_task_run_in_thread(task, edit_worker);
  g_object_unref(task);
}

static void
start_edit(LoslesWindow *self,
           GFile *destination,
           EditKind kind,
           LoslesRotation rotation,
           const LoslesCrop *crop)
{
  if (self->operation_in_progress ||
      self->foreground_loading ||
      !self->current_image)
    return;

  EditJob *job = g_new0(EditJob, 1);
  job->image = g_object_ref(self->current_image);
  job->destination = g_object_ref(destination);
  job->kind = kind;
  job->rotation = rotation;
  if (crop)
    job->crop = *crop;

  self->operation_in_progress = TRUE;
  queue_edit(self, job);
}

static void
rotate_clicked(GtkButton *button, LoslesWindow *self)
{
  const LoslesRotation direction =
    button == self->rotate_left_button
      ? LOSLES_ROTATE_LEFT
      : LOSLES_ROTATE_RIGHT;
  if (!self->current_image ||
      self->operation_in_progress ||
      self->foreground_loading)
    return;
  start_edit(self,
             losles_image_get_file(self->current_image),
             EDIT_ROTATE,
             direction,
             NULL);
}

static void
normalize_orientation_clicked(GtkButton *button, LoslesWindow *self)
{
  (void)button;
  if (!self->current_image ||
      self->operation_in_progress ||
      self->foreground_loading)
    return;
  start_edit(self,
             losles_image_get_file(self->current_image),
             EDIT_NORMALIZE_ORIENTATION,
             LOSLES_ROTATE_LEFT,
             NULL);
}

static void
crop_toggled(GtkToggleButton *button, LoslesWindow *self)
{
  const gboolean active = gtk_toggle_button_get_active(button);
  if (active)
    reset_zoom(self);
  gtk_widget_set_visible(GTK_WIDGET(self->crop_area), active);
  gtk_widget_set_visible(GTK_WIDGET(self->apply_crop_button), active);
  self->crop_drag_mode = CROP_DRAG_NONE;
  self->crop_valid = FALSE;
  gtk_widget_set_sensitive(GTK_WIDGET(self->apply_crop_button), FALSE);
  set_crop_cursor(self, active ? CROP_DRAG_NEW : CROP_DRAG_NONE);
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
}

static void
apply_crop_clicked(GtkButton *button, LoslesWindow *self)
{
  (void)button;
  if (self->operation_in_progress ||
      self->foreground_loading ||
      !self->current_image ||
      !self->crop_valid)
    return;

  LoslesFormat *format =
    LOSLES_FORMAT(losles_image_get_format(self->current_image));
  LoslesCrop adjusted = {0};
  g_autoptr(GError) error = NULL;
  if (!losles_format_adjust_crop(format,
                                 self->current_image,
                                 &self->crop,
                                 &adjusted,
                                 &error)) {
    show_error(self, "This crop cannot be performed losslessly", error);
    return;
  }
  self->crop = adjusted;
  gtk_widget_queue_draw(GTK_WIDGET(self->crop_area));
  start_edit(self,
             losles_image_get_file(self->current_image),
             EDIT_CROP,
             LOSLES_ROTATE_LEFT,
             &adjusted);
}

static gint
find_file_after_delete(DeleteJob *job, GPtrArray *files)
{
  if (!files || files->len == 0)
    return -1;

  if (job->preferred_next) {
    const gint preferred_index =
      find_file_index(files, job->preferred_next);
    if (preferred_index >= 0)
      return preferred_index;
  }

  g_autofree gchar *deleted_name = g_file_get_basename(job->file);
  for (guint i = 0; deleted_name && i < files->len; i++) {
    GFile *candidate = g_ptr_array_index(files, i);
    g_autofree gchar *candidate_name =
      g_file_get_basename(candidate);
    if (candidate_name &&
        g_utf8_collate(candidate_name, deleted_name) > 0)
      return (gint)i;
  }

  /*
   * No later image remains, so the deleted image was last relative to the
   * rescan. Continue with its predecessor, which is now the final entry.
   */
  return (gint)files->len - 1;
}

static void
delete_done(GObject *source_object,
            GAsyncResult *result,
            gpointer user_data)
{
  (void)user_data;
  LoslesWindow *self = LOSLES_WINDOW(source_object);
  GTask *task = G_TASK(result);
  DeleteJob *job = g_task_get_task_data(task);
  g_autoptr(GError) error = NULL;
  DeleteResult *delete_result =
    g_task_propagate_pointer(task, &error);

  self->operation_in_progress = FALSE;
  gtk_spinner_stop(self->spinner);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
  if (!delete_result) {
    update_controls(self);
    show_error(self, "Could not move the image to Trash", error);
    return;
  }

  if (delete_result->scan_error) {
    g_debug("The image was trashed, but its directory could not be "
            "rescanned: %s",
            delete_result->scan_error->message);
  }

  const gint next_index =
    find_file_after_delete(job, delete_result->files);
  if (next_index < 0) {
    reset_content_pipeline(self);
    show_no_picture(self);
  } else {
    advance_pipeline_after_delete(self, job->file);
    g_clear_pointer(&self->files, g_ptr_array_unref);
    self->files = g_steal_pointer(&delete_result->files);
    self->current_index = (guint)next_index;
    show_index(self, self->current_index, TRUE);
  }
  delete_result_free(delete_result);
}

static void
delete_current_image(LoslesWindow *self)
{
  GFile *file = current_file(self);
  if (self->operation_in_progress ||
      self->foreground_loading ||
      !self->current_image ||
      !file)
    return;

  DeleteJob *job = g_new0(DeleteJob, 1);
  job->file = g_object_ref(file);
  job->directory = g_file_get_parent(file);
  if (self->current_index + 1 < self->files->len) {
    job->preferred_next =
      g_object_ref(g_ptr_array_index(self->files,
                                     self->current_index + 1));
  }

  self->operation_in_progress = TRUE;
  update_controls(self);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), TRUE);
  gtk_spinner_start(self->spinner);
  set_status(self, "Moving image to Trash…");

  GTask *task = g_task_new(self, NULL, delete_done, NULL);
  g_task_set_task_data(task, job, (GDestroyNotify)delete_job_free);
  g_task_run_in_thread(task, delete_worker);
  g_object_unref(task);
}

static void
open_chosen(GObject *source_object,
            GAsyncResult *result,
            gpointer user_data)
{
  LoslesWindow *self = LOSLES_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file =
    gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object),
                                result,
                                &error);
  if (file)
    losles_window_open_file(self, file);
  else if (!g_error_matches(error, GTK_DIALOG_ERROR,
                            GTK_DIALOG_ERROR_DISMISSED))
    show_error(self, "Could not choose an image", error);
  g_object_unref(self);
}

static void
open_dialog(LoslesWindow *self)
{
  if (self->operation_in_progress)
    return;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open Image");
  gtk_file_dialog_open(dialog,
                       GTK_WINDOW(self),
                       NULL,
                       open_chosen,
                       g_object_ref(self));
  g_object_unref(dialog);
}

static void
action_open(GSimpleAction *action,
            GVariant *parameter,
            gpointer user_data)
{
  (void)action;
  (void)parameter;
  open_dialog(LOSLES_WINDOW(user_data));
}

static void
action_previous(GSimpleAction *action,
                GVariant *parameter,
                gpointer user_data)
{
  (void)action;
  (void)parameter;
  previous_image(LOSLES_WINDOW(user_data));
}

static void
action_next(GSimpleAction *action,
            GVariant *parameter,
            gpointer user_data)
{
  (void)action;
  (void)parameter;
  next_image(LOSLES_WINDOW(user_data));
}

static void
action_toggle_crop(GSimpleAction *action,
                   GVariant *parameter,
                   gpointer user_data)
{
  (void)action;
  (void)parameter;
  LoslesWindow *self = LOSLES_WINDOW(user_data);
  if (!self->operation_in_progress &&
      gtk_widget_get_sensitive(GTK_WIDGET(self->crop_button))) {
    gtk_toggle_button_set_active(
      self->crop_button,
      !gtk_toggle_button_get_active(self->crop_button));
  }
}

static void
action_apply_crop(GSimpleAction *action,
                  GVariant *parameter,
                  gpointer user_data)
{
  (void)action;
  (void)parameter;
  LoslesWindow *self = LOSLES_WINDOW(user_data);
  if (gtk_toggle_button_get_active(self->crop_button))
    apply_crop_clicked(NULL, self);
}

static void
action_delete(GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
  (void)action;
  (void)parameter;
  delete_current_image(LOSLES_WINDOW(user_data));
}

static void
info_toggled(GtkToggleButton *button, LoslesWindow *self)
{
  gtk_widget_set_visible(GTK_WIDGET(self->status),
                         gtk_toggle_button_get_active(button));
}

static void
action_toggle_info(GSimpleAction *action,
                   GVariant *parameter,
                   gpointer user_data)
{
  (void)action;
  (void)parameter;
  LoslesWindow *self = LOSLES_WINDOW(user_data);
  gtk_toggle_button_set_active(
    self->info_button,
    !gtk_toggle_button_get_active(self->info_button));
}

static void
toggle_fullscreen(LoslesWindow *self)
{
  if (gtk_window_is_fullscreen(GTK_WINDOW(self)))
    gtk_window_unfullscreen(GTK_WINDOW(self));
  else
    gtk_window_fullscreen(GTK_WINDOW(self));
}

static void
action_toggle_fullscreen(GSimpleAction *action,
                         GVariant *parameter,
                         gpointer user_data)
{
  (void)action;
  (void)parameter;
  toggle_fullscreen(LOSLES_WINDOW(user_data));
}

static void
action_escape(GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
  (void)action;
  (void)parameter;
  LoslesWindow *self = LOSLES_WINDOW(user_data);
  if (gtk_toggle_button_get_active(self->crop_button))
    gtk_toggle_button_set_active(self->crop_button, FALSE);
  else if (self->zoom_scale > 1.0)
    reset_zoom(self);
}

static void
zoom_pointer_moved(GtkEventControllerMotion *controller,
                   gdouble x,
                   gdouble y,
                   LoslesWindow *self)
{
  (void)controller;
  self->zoom_pointer_x = x;
  self->zoom_pointer_y = y;
  self->zoom_pointer_inside = TRUE;
  update_zoom_cursor(self);
}

static void
zoom_pointer_left(GtkEventControllerMotion *controller,
                  LoslesWindow *self)
{
  (void)controller;
  self->zoom_pointer_inside = FALSE;
  update_zoom_cursor(self);
}

static gboolean
zoom_scrolled(GtkEventControllerScroll *controller,
              gdouble dx,
              gdouble dy,
              LoslesWindow *self)
{
  (void)controller;
  (void)dx;
  if (!self->current_texture || dy == 0 ||
      gtk_toggle_button_get_active(self->crop_button))
    return FALSE;

  const gboolean over_image =
    self->zoom_pointer_inside &&
    self->zoom_pointer_x >= self->zoom_picture_x &&
    self->zoom_pointer_y >= self->zoom_picture_y &&
    self->zoom_pointer_x <=
      self->zoom_picture_x + self->zoom_picture_width &&
    self->zoom_pointer_y <=
      self->zoom_picture_y + self->zoom_picture_height;
  if (!over_image)
    return FALSE;

  const gdouble old_scale = self->zoom_scale;
  const gdouble new_scale =
    CLAMP(dy < 0 ? old_scale * ZOOM_STEP
                 : old_scale / ZOOM_STEP,
          1.0,
          ZOOM_MAX);
  if (new_scale == old_scale)
    return TRUE;

  const gdouble image_x =
    (self->zoom_pointer_x - self->zoom_picture_x) /
    self->zoom_picture_width;
  const gdouble image_y =
    (self->zoom_pointer_y - self->zoom_picture_y) /
    self->zoom_picture_height;
  gdouble new_width = 0;
  gdouble new_height = 0;
  if (!zoom_dimensions(self,
                       new_scale,
                       &new_width,
                       &new_height))
    return FALSE;

  const gdouble view_width = self->zoom_view_width;
  const gdouble view_height = self->zoom_view_height;
  self->zoom_scale = new_scale;
  if (new_scale == 1.0) {
    self->zoom_center_x = 0.5;
    self->zoom_center_y = 0.5;
  } else {
    /*
     * Store the image point which must land at the viewport center. This is
     * algebraically equivalent to keeping image_x/image_y under the pointer.
     */
    self->zoom_center_x =
      image_x +
      (view_width / 2.0 - self->zoom_pointer_x) / new_width;
    self->zoom_center_y =
      image_y +
      (view_height / 2.0 - self->zoom_pointer_y) / new_height;
  }
  apply_zoom_layout(self);
  return TRUE;
}

static void
zoom_drag_begin(GtkGestureDrag *gesture,
                gdouble start_x,
                gdouble start_y,
                LoslesWindow *self)
{
  const gboolean over_image =
    start_x >= self->zoom_picture_x &&
    start_y >= self->zoom_picture_y &&
    start_x <= self->zoom_picture_x + self->zoom_picture_width &&
    start_y <= self->zoom_picture_y + self->zoom_picture_height;
  if (self->zoom_scale <= 1.0 || !self->current_texture ||
      gtk_toggle_button_get_active(self->crop_button) ||
      !over_image) {
    gtk_gesture_set_state(GTK_GESTURE(gesture),
                          GTK_EVENT_SEQUENCE_DENIED);
    return;
  }

  self->zoom_dragging = TRUE;
  self->zoom_drag_center_x = self->zoom_center_x;
  self->zoom_drag_center_y = self->zoom_center_y;
  update_zoom_cursor(self);
}

static void
zoom_drag_update(GtkGestureDrag *gesture,
                 gdouble offset_x,
                 gdouble offset_y,
                 LoslesWindow *self)
{
  (void)gesture;
  if (!self->zoom_dragging ||
      self->zoom_picture_width <= 0 ||
      self->zoom_picture_height <= 0)
    return;

  if (self->zoom_picture_width > self->zoom_view_width) {
    self->zoom_center_x =
      self->zoom_drag_center_x - offset_x / self->zoom_picture_width;
  }
  if (self->zoom_picture_height > self->zoom_view_height) {
    self->zoom_center_y =
      self->zoom_drag_center_y - offset_y / self->zoom_picture_height;
  }
  apply_zoom_layout(self);
}

static void
zoom_drag_end(GtkGestureDrag *gesture,
              gdouble offset_x,
              gdouble offset_y,
              LoslesWindow *self)
{
  zoom_drag_update(gesture, offset_x, offset_y, self);
  self->zoom_dragging = FALSE;
  update_zoom_cursor(self);
}

static void
zoom_canvas_resized(GtkDrawingArea *area,
                    gint width,
                    gint height,
                    LoslesWindow *self)
{
  (void)area;
  self->zoom_view_width = width;
  self->zoom_view_height = height;
  apply_zoom_layout(self);
}

static gboolean
file_drop_accept(GtkDropTarget *target,
                 GdkDrop *drop,
                 LoslesWindow *self)
{
  (void)target;
  (void)drop;
  return !self->operation_in_progress &&
         !gtk_toggle_button_get_active(self->crop_button);
}

static gboolean
focus_after_file_drop(gpointer user_data)
{
  DropFocusRequest *request = user_data;
  LoslesWindow *self = request->window;

  gtk_widget_grab_focus(GTK_WIDGET(self->zoom_view));
  gtk_window_present(GTK_WINDOW(self));

  GtkNative *native = gtk_widget_get_native(GTK_WIDGET(self));
  GdkSurface *surface = native ? gtk_native_get_surface(native) : NULL;
  if (GDK_IS_TOPLEVEL(surface))
    gdk_toplevel_focus(GDK_TOPLEVEL(surface), request->timestamp);
  return G_SOURCE_REMOVE;
}

static gboolean
file_dropped(GtkDropTarget *target,
             const GValue *value,
             gdouble x,
             gdouble y,
             LoslesWindow *self)
{
  (void)x;
  (void)y;
  if (self->operation_in_progress ||
      gtk_toggle_button_get_active(self->crop_button) ||
      !G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST))
    return FALSE;

  GdkFileList *file_list = g_value_get_boxed(value);
  if (!file_list)
    return FALSE;

  GSList *files = gdk_file_list_get_files(file_list);
  if (!files)
    return FALSE;

  GFile *file = G_FILE(files->data);
  GdkEvent *event =
    gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(target));
  DropFocusRequest *focus_request = g_new0(DropFocusRequest, 1);
  focus_request->window = g_object_ref(self);
  focus_request->timestamp =
    event ? gdk_event_get_time(event) : GDK_CURRENT_TIME;
  losles_window_open_file(self, file);
  g_slist_free(files);
  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  focus_after_file_drop,
                  focus_request,
                  (GDestroyNotify)drop_focus_request_free);
  return TRUE;
}

static void
picture_pressed(GtkGestureClick *gesture,
                gint n_press,
                gdouble x,
                gdouble y,
                LoslesWindow *self)
{
  (void)x;
  (void)y;
  if (n_press != 2)
    return;

  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  toggle_fullscreen(self);
}

static void
fullscreen_changed(GObject *object,
                   GParamSpec *parameter,
                   LoslesWindow *self)
{
  (void)object;
  (void)parameter;
  gtk_widget_set_visible(GTK_WIDGET(self->header_bar),
                         !gtk_window_is_fullscreen(GTK_WINDOW(self)));
}

static void
monitor_changed(GdkSurface *surface,
                GdkMonitor *monitor,
                LoslesWindow *self)
{
  (void)surface;
  (void)monitor;
  invalidate_render_cache(self);
  start_render(self);
}

static void
window_mapped(GtkWidget *widget, LoslesWindow *self)
{
  (void)widget;
  if (!self->monitor_signals_connected) {
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self));
    if (surface) {
      if (self->application_icon && GDK_IS_TOPLEVEL(surface)) {
        GList *icons =
          g_list_prepend(NULL, self->application_icon);
        gdk_toplevel_set_icon_list(GDK_TOPLEVEL(surface), icons);
        g_list_free(icons);
      }
      g_signal_connect_object(surface,
                              "enter-monitor",
                              G_CALLBACK(monitor_changed),
                              self,
                              0);
      g_signal_connect_object(surface,
                              "leave-monitor",
                              G_CALLBACK(monitor_changed),
                              self,
                              0);
      self->monitor_signals_connected = TRUE;
    }
  }
  start_render(self);
}

static void
color_target_changed(LoslesColorManager *manager, LoslesWindow *self)
{
  (void)manager;
  invalidate_render_cache(self);
  start_render(self);
}

static void
update_controls(LoslesWindow *self)
{
  const gboolean idle = !self->operation_in_progress;
  const gboolean has_image =
    self->current_image != NULL && idle && !self->foreground_loading;
  gboolean rotation = FALSE;
  gboolean normalize_orientation = FALSE;
  gboolean crop = FALSE;
  if (has_image) {
    LoslesFormat *format =
      LOSLES_FORMAT(losles_image_get_format(self->current_image));
    rotation = losles_format_supports_lossless_rotation(format) &&
               losles_image_supports_lossless_rotation(
                 self->current_image);
    normalize_orientation =
      losles_format_supports_lossless_orientation_normalization(format) &&
      losles_image_has_exif_orientation(self->current_image) &&
      losles_image_get_orientation(self->current_image) != 1;
    crop = losles_format_supports_lossless_crop(format) &&
           losles_image_supports_lossless_crop(self->current_image) &&
           losles_image_get_orientation(self->current_image) == 1;
  }

  gtk_widget_set_sensitive(GTK_WIDGET(self->open_button), idle);
  gtk_widget_set_sensitive(GTK_WIDGET(self->previous_button),
                           idle && self->files && self->current_index > 0);
  gtk_widget_set_sensitive(GTK_WIDGET(self->next_button),
                           idle && self->files &&
                             self->current_index + 1 < self->files->len);
  gtk_widget_set_sensitive(GTK_WIDGET(self->rotate_left_button), rotation);
  gtk_widget_set_sensitive(GTK_WIDGET(self->rotate_right_button), rotation);
  gtk_widget_set_sensitive(
    GTK_WIDGET(self->normalize_orientation_button),
    normalize_orientation);
  const gchar *normalize_orientation_tooltip =
    normalize_orientation
      ? "EXIF stores a non-default orientation. Click to apply it "
        "losslessly to the JPEG data and set the tag to 1"
      : "No rotation is stored in EXIF";
  gtk_widget_set_tooltip_text(
    GTK_WIDGET(self->normalize_orientation_button),
    normalize_orientation_tooltip);
  gtk_widget_set_tooltip_text(
    self->normalize_orientation_tooltip_area,
    normalize_orientation_tooltip);
  gtk_widget_set_sensitive(GTK_WIDGET(self->crop_button), crop);
  if (!crop)
    gtk_toggle_button_set_active(self->crop_button, FALSE);
}

static GtkWidget *
icon_button(const gchar *icon_name, const gchar *tooltip)
{
  GtkWidget *button = gtk_button_new_from_icon_name(icon_name);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  return button;
}

static void
crop_icon_draw(GtkDrawingArea *area,
               cairo_t *cr,
               int width,
               int height,
               gpointer user_data)
{
  (void)user_data;

  GdkRGBA color;
  gtk_widget_get_color(GTK_WIDGET(area), &color);

  const gdouble size = MIN(width, height);
  cairo_translate(cr, (width - size) / 2.0, (height - size) / 2.0);
  cairo_scale(cr, size / 16.0, size / 16.0);
  cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
  cairo_set_line_width(cr, 2.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);

  cairo_move_to(cr, 3.0, 0.0);
  cairo_line_to(cr, 3.0, 12.0);
  cairo_line_to(cr, 10.0, 12.0);
  cairo_move_to(cr, 0.0, 3.0);
  cairo_line_to(cr, 12.0, 3.0);
  cairo_line_to(cr, 12.0, 15.0);
  cairo_stroke(cr);
}

static GtkWidget *
crop_icon(void)
{
  GtkDrawingArea *icon = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_drawing_area_set_content_width(icon, 16);
  gtk_drawing_area_set_content_height(icon, 16);
  gtk_drawing_area_set_draw_func(icon, crop_icon_draw, NULL, NULL);
  return GTK_WIDGET(icon);
}

static GdkTexture *
load_application_icon(void)
{
  g_autofree gchar *portable_icon =
    losles_platform_get_portable_icon_path();
  const gchar *paths[] = {
    LOSLES_SOURCE_ICON_FILE,
    LOSLES_INSTALLED_ICON_FILE,
    portable_icon,
    NULL,
  };

  for (guint i = 0; paths[i]; i++) {
    if (!paths[i][0] || !g_file_test(paths[i], G_FILE_TEST_IS_REGULAR))
      continue;

    g_autoptr(GError) error = NULL;
    GdkTexture *texture =
      gdk_texture_new_from_filename(paths[i], &error);
    if (texture)
      return texture;

    g_warning("Could not load application icon %s: %s",
              paths[i],
              error->message);
  }

  return NULL;
}

static void
show_about_dialog(LoslesWindow *self)
{
  if (self->about_dialog) {
    gtk_window_present(self->about_dialog);
    return;
  }

  GtkAboutDialog *about = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
  self->about_dialog = GTK_WINDOW(about);
  g_object_add_weak_pointer(G_OBJECT(about),
                            (gpointer *)&self->about_dialog);

  gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(self));
  gtk_window_set_modal(GTK_WINDOW(about), TRUE);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(about), TRUE);
  gtk_about_dialog_set_program_name(about, LOSLES_APPLICATION_NAME);
  gtk_about_dialog_set_version(about, LOSLES_VERSION);
  gtk_about_dialog_set_comments(
    about,
    "A lightweight, color-managed photo viewer for lossless image "
    "operations, licensed under the MIT License.");
  gtk_about_dialog_set_copyright(
    about,
    "Copyright © 2026 Piotr T. Różański");
  gtk_about_dialog_set_website(about, LOSLES_REPOSITORY_URL);
  gtk_about_dialog_set_website_label(about, "Source repository");
  if (self->application_icon)
    gtk_about_dialog_set_logo(
      about,
      GDK_PAINTABLE(self->application_icon));
  else
    gtk_about_dialog_set_logo_icon_name(about, LOSLES_APPLICATION_ID);

  gtk_window_present(GTK_WINDOW(about));
}

static void
losles_window_dispose(GObject *object)
{
  LoslesWindow *self = LOSLES_WINDOW(object);

  if (self->load_cancellable)
    g_cancellable_cancel(self->load_cancellable);
  if (self->render_cancellable)
    g_cancellable_cancel(self->render_cancellable);
  if (self->about_dialog) {
    g_object_remove_weak_pointer(
      G_OBJECT(self->about_dialog),
      (gpointer *)&self->about_dialog);
    gtk_window_destroy(self->about_dialog);
    self->about_dialog = NULL;
  }
  g_clear_object(&self->load_cancellable);
  g_clear_object(&self->render_cancellable);
  g_clear_object(&self->application_icon);
  g_clear_object(&self->current_texture);
  g_clear_object(&self->current_image);
  g_clear_pointer(&self->files, g_ptr_array_unref);
  g_clear_pointer(&self->cache, g_hash_table_unref);
  g_clear_pointer(&self->inflight, g_hash_table_unref);
  g_clear_pointer(&self->decode_failed, g_hash_table_unref);
  g_clear_pointer(&self->decode_capacity_blocked, g_hash_table_unref);
  g_clear_pointer(&self->render_cache, g_hash_table_unref);
  g_clear_pointer(&self->render_inflight, g_hash_table_unref);
  g_clear_pointer(&self->render_failed, g_hash_table_unref);
  g_clear_pointer(&self->render_capacity_blocked, g_hash_table_unref);
  g_clear_pointer(&self->render_profile_id, g_free);
  g_clear_object(&self->color_manager);
  g_clear_object(&self->registry);

  G_OBJECT_CLASS(losles_window_parent_class)->dispose(object);
}

static void
losles_window_class_init(LoslesWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = losles_window_dispose;
}

static void
losles_window_init(LoslesWindow *self)
{
  const gsize cache_limit = detect_cache_limit();
  self->source_cache_limit = cache_limit;
  self->render_cache_limit = cache_limit;
  self->registry = losles_format_registry_new();
  self->color_manager = losles_color_manager_new();
  self->files =
    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  self->cache =
    g_hash_table_new_full(g_str_hash,
                          g_str_equal,
                          g_free,
                          (GDestroyNotify)g_object_unref);
  self->inflight =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->decode_failed =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->decode_capacity_blocked =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->load_cancellable = g_cancellable_new();
  self->render_cache =
    g_hash_table_new_full(g_str_hash,
                          g_str_equal,
                          g_free,
                          (GDestroyNotify)render_cache_entry_free);
  self->render_inflight =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->render_failed =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->render_capacity_blocked =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->render_cancellable = g_cancellable_new();
  self->navigation_direction = 1;
  self->zoom_scale = 1.0;
  self->zoom_center_x = 0.5;
  self->zoom_center_y = 0.5;
  self->application_icon = load_application_icon();

  gtk_window_set_default_size(GTK_WINDOW(self), 960, 700);
  gtk_window_set_title(GTK_WINDOW(self), LOSLES_APPLICATION_NAME);
  gtk_widget_add_css_class(GTK_WIDGET(self), "losles-window");

  GtkCssProvider *css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
    css_provider,
    "window.losles-window, .losles-canvas {"
    "  background-color: #000000;"
    "}"
    "label.losles-information {"
    "  color: #ffffff;"
    "  background-color: #000000;"
    "  padding: 4px 8px;"
    "  border-radius: 0;"
    "  box-shadow: none;"
    "}"
    "spinner.losles-loading-spinner {"
    "  color: #ffffff;"
    "}");
  gtk_style_context_add_provider_for_display(
    gtk_widget_get_display(GTK_WIDGET(self)),
    GTK_STYLE_PROVIDER(css_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css_provider);

  self->header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
  gtk_window_set_titlebar(GTK_WINDOW(self),
                          GTK_WIDGET(self->header_bar));

  self->open_button =
    GTK_BUTTON(icon_button("document-open-symbolic",
                           "Open image (Ctrl+O)"));
  gtk_header_bar_pack_start(self->header_bar,
                            GTK_WIDGET(self->open_button));
  g_signal_connect_swapped(self->open_button,
                           "clicked",
                           G_CALLBACK(open_dialog),
                           self);

  self->previous_button =
    GTK_BUTTON(icon_button("go-previous-symbolic",
                           "Previous image (Left or mouse Back)"));
  self->next_button =
    GTK_BUTTON(icon_button("go-next-symbolic",
                           "Next image (Right or mouse Forward)"));
  gtk_header_bar_pack_start(self->header_bar,
                            GTK_WIDGET(self->previous_button));
  gtk_header_bar_pack_start(self->header_bar,
                            GTK_WIDGET(self->next_button));
  g_signal_connect_swapped(self->previous_button,
                           "clicked",
                           G_CALLBACK(previous_image),
                           self);
  g_signal_connect_swapped(self->next_button,
                           "clicked",
                           G_CALLBACK(next_image),
                           self);

  GtkButton *about_button =
    GTK_BUTTON(icon_button("dialog-question-symbolic", "About losles"));
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(about_button));
  g_signal_connect_swapped(about_button,
                           "clicked",
                           G_CALLBACK(show_about_dialog),
                           self);

  self->rotate_left_button =
    GTK_BUTTON(icon_button("object-rotate-left-symbolic",
                           "Lossless rotate left"));
  self->rotate_right_button =
    GTK_BUTTON(icon_button("object-rotate-right-symbolic",
                           "Lossless rotate right"));
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(self->rotate_right_button));
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(self->rotate_left_button));
  g_signal_connect(self->rotate_left_button,
                   "clicked",
                   G_CALLBACK(rotate_clicked),
                   self);
  g_signal_connect(self->rotate_right_button,
                   "clicked",
                   G_CALLBACK(rotate_clicked),
                   self);

  self->normalize_orientation_button =
    GTK_BUTTON(icon_button("dialog-warning-symbolic",
                           "No rotation is stored in EXIF"));
  self->normalize_orientation_tooltip_area =
    gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_tooltip_text(self->normalize_orientation_tooltip_area,
                              "No rotation is stored in EXIF");
  gtk_box_append(GTK_BOX(self->normalize_orientation_tooltip_area),
                 GTK_WIDGET(self->normalize_orientation_button));
  gtk_header_bar_pack_end(
    self->header_bar,
    self->normalize_orientation_tooltip_area);
  g_signal_connect(self->normalize_orientation_button,
                   "clicked",
                   G_CALLBACK(normalize_orientation_clicked),
                   self);

  self->crop_button =
    GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_child(GTK_BUTTON(self->crop_button), crop_icon());
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->crop_button),
                              "Lossless crop "
                              "(C; original goes to Trash)");
  gtk_widget_add_css_class(GTK_WIDGET(self->crop_button), "flat");
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(self->crop_button));
  g_signal_connect(self->crop_button,
                   "toggled",
                   G_CALLBACK(crop_toggled),
                   self);

  self->info_button =
    GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(self->info_button),
                           "dialog-information-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->info_button),
                              "Show image information (I)");
  gtk_widget_add_css_class(GTK_WIDGET(self->info_button), "flat");
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(self->info_button));
  g_signal_connect(self->info_button,
                   "toggled",
                   G_CALLBACK(info_toggled),
                   self);

  self->apply_crop_button =
    GTK_BUTTON(gtk_button_new_with_label("Crop"));
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->apply_crop_button),
                              "Apply lossless crop (Enter)");
  gtk_header_bar_pack_end(self->header_bar,
                          GTK_WIDGET(self->apply_crop_button));
  g_signal_connect(self->apply_crop_button,
                   "clicked",
                   G_CALLBACK(apply_crop_clicked),
                   self);

  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(overlay, TRUE);
  gtk_widget_set_vexpand(overlay, TRUE);
  gtk_widget_add_css_class(overlay, "losles-canvas");
  gtk_window_set_child(GTK_WINDOW(self), overlay);

  GtkWidget *canvas = gtk_drawing_area_new();
  gtk_widget_set_hexpand(canvas, TRUE);
  gtk_widget_set_vexpand(canvas, TRUE);
  gtk_widget_add_css_class(canvas, "losles-canvas");
  gtk_overlay_set_child(GTK_OVERLAY(overlay), canvas);
  g_signal_connect(canvas,
                   "resize",
                   G_CALLBACK(zoom_canvas_resized),
                   self);

  self->zoom_view = GTK_FIXED(gtk_fixed_new());
  gtk_widget_set_hexpand(GTK_WIDGET(self->zoom_view), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self->zoom_view), TRUE);
  gtk_widget_set_halign(GTK_WIDGET(self->zoom_view), GTK_ALIGN_FILL);
  gtk_widget_set_valign(GTK_WIDGET(self->zoom_view), GTK_ALIGN_FILL);
  gtk_widget_set_focusable(GTK_WIDGET(self->zoom_view), TRUE);
  gtk_widget_set_overflow(GTK_WIDGET(self->zoom_view),
                          GTK_OVERFLOW_HIDDEN);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          GTK_WIDGET(self->zoom_view));

  self->picture = GTK_PICTURE(gtk_picture_new());
  gtk_picture_set_content_fit(self->picture, GTK_CONTENT_FIT_CONTAIN);
  gtk_picture_set_can_shrink(self->picture, TRUE);
  gtk_fixed_put(self->zoom_view, GTK_WIDGET(self->picture), 0, 0);

  GtkEventController *zoom_motion =
    gtk_event_controller_motion_new();
  gtk_widget_add_controller(GTK_WIDGET(self->zoom_view), zoom_motion);
  g_signal_connect(zoom_motion,
                   "enter",
                   G_CALLBACK(zoom_pointer_moved),
                   self);
  g_signal_connect(zoom_motion,
                   "motion",
                   G_CALLBACK(zoom_pointer_moved),
                   self);
  g_signal_connect(zoom_motion,
                   "leave",
                   G_CALLBACK(zoom_pointer_left),
                   self);

  GtkEventController *zoom_scroll =
    gtk_event_controller_scroll_new(
      GTK_EVENT_CONTROLLER_SCROLL_VERTICAL |
      GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  gtk_widget_add_controller(GTK_WIDGET(self->zoom_view), zoom_scroll);
  g_signal_connect(zoom_scroll,
                   "scroll",
                   G_CALLBACK(zoom_scrolled),
                   self);

  GtkGesture *zoom_drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(zoom_drag),
                                GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(GTK_WIDGET(self->zoom_view),
                            GTK_EVENT_CONTROLLER(zoom_drag));
  g_signal_connect(zoom_drag,
                   "drag-begin",
                   G_CALLBACK(zoom_drag_begin),
                   self);
  g_signal_connect(zoom_drag,
                   "drag-update",
                   G_CALLBACK(zoom_drag_update),
                   self);
  g_signal_connect(zoom_drag,
                   "drag-end",
                   G_CALLBACK(zoom_drag_end),
                   self);

  GtkGesture *click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
                                GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(overlay, GTK_EVENT_CONTROLLER(click));
  g_signal_connect(click,
                   "pressed",
                   G_CALLBACK(picture_pressed),
                   self);

  self->crop_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_hexpand(GTK_WIDGET(self->crop_area), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self->crop_area), TRUE);
  gtk_drawing_area_set_draw_func(self->crop_area,
                                 (GtkDrawingAreaDrawFunc)crop_draw,
                                 self,
                                 NULL);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          GTK_WIDGET(self->crop_area));
  GtkGesture *drag = gtk_gesture_drag_new();
  gtk_widget_add_controller(GTK_WIDGET(self->crop_area),
                            GTK_EVENT_CONTROLLER(drag));
  g_signal_connect(drag,
                   "drag-begin",
                   G_CALLBACK(crop_drag_begin),
                   self);
  g_signal_connect(drag,
                   "drag-update",
                   G_CALLBACK(crop_drag_update),
                   self);
  g_signal_connect(drag,
                   "drag-end",
                   G_CALLBACK(crop_drag_end),
                   self);
  GtkEventController *motion = gtk_event_controller_motion_new();
  gtk_widget_add_controller(GTK_WIDGET(self->crop_area), motion);
  g_signal_connect(motion,
                   "motion",
                   G_CALLBACK(crop_motion),
                   self);
  g_signal_connect(motion,
                   "leave",
                   G_CALLBACK(crop_pointer_left),
                   self);

  self->spinner = GTK_SPINNER(gtk_spinner_new());
  gtk_widget_set_size_request(GTK_WIDGET(self->spinner),
                              LOADING_SPINNER_SIZE,
                              LOADING_SPINNER_SIZE);
  gtk_widget_set_halign(GTK_WIDGET(self->spinner), GTK_ALIGN_CENTER);
  gtk_widget_set_valign(GTK_WIDGET(self->spinner), GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(GTK_WIDGET(self->spinner), FALSE);
  gtk_widget_add_css_class(GTK_WIDGET(self->spinner),
                           "losles-loading-spinner");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          GTK_WIDGET(self->spinner));

  self->status = GTK_LABEL(gtk_label_new(
    "Open a JPEG or PNG image"));
  gtk_label_set_ellipsize(self->status, PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_max_width_chars(self->status, 80);
  gtk_label_set_xalign(self->status, 0);
  gtk_widget_set_halign(GTK_WIDGET(self->status), GTK_ALIGN_START);
  gtk_widget_set_valign(GTK_WIDGET(self->status), GTK_ALIGN_END);
  gtk_widget_set_can_target(GTK_WIDGET(self->status), FALSE);
  gtk_widget_add_css_class(GTK_WIDGET(self->status),
                           "losles-information");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          GTK_WIDGET(self->status));

  gtk_widget_set_visible(GTK_WIDGET(self->crop_area), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->apply_crop_button), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->spinner), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->status), FALSE);

  static const GActionEntry actions[] = {
    {.name = "open", .activate = action_open},
    {.name = "previous", .activate = action_previous},
    {.name = "next", .activate = action_next},
    {.name = "toggle-crop", .activate = action_toggle_crop},
    {.name = "apply-crop", .activate = action_apply_crop},
    {.name = "delete", .activate = action_delete},
    {.name = "toggle-info", .activate = action_toggle_info},
    {.name = "toggle-fullscreen", .activate = action_toggle_fullscreen},
    {.name = "escape", .activate = action_escape},
  };
  g_action_map_add_action_entries(G_ACTION_MAP(self),
                                  actions,
                                  G_N_ELEMENTS(actions),
                                  self);

  GtkDropTarget *file_drop_target =
    gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect(file_drop_target,
                   "accept",
                   G_CALLBACK(file_drop_accept),
                   self);
  g_signal_connect(file_drop_target,
                   "drop",
                   G_CALLBACK(file_dropped),
                   self);
  gtk_widget_add_controller(GTK_WIDGET(self),
                            GTK_EVENT_CONTROLLER(file_drop_target));

  GtkEventController *mouse_navigation =
    gtk_event_controller_legacy_new();
  gtk_event_controller_set_propagation_phase(mouse_navigation,
                                             GTK_PHASE_CAPTURE);
  g_signal_connect(mouse_navigation,
                   "event",
                   G_CALLBACK(mouse_navigation_event),
                   self);
  gtk_widget_add_controller(GTK_WIDGET(self), mouse_navigation);

  g_signal_connect(self,
                   "map",
                   G_CALLBACK(window_mapped),
                   self);
  g_signal_connect(self,
                   "notify::fullscreened",
                   G_CALLBACK(fullscreen_changed),
                   self);
  g_signal_connect(self->color_manager,
                   "target-changed",
                   G_CALLBACK(color_target_changed),
                   self);
  update_controls(self);
}

LoslesWindow *
losles_window_new(GtkApplication *application)
{
  return g_object_new(LOSLES_TYPE_WINDOW,
                      "application",
                      application,
                      NULL);
}

void
losles_window_open_file(LoslesWindow *self, GFile *file)
{
  g_return_if_fail(LOSLES_IS_WINDOW(self));
  g_return_if_fail(G_IS_FILE(file));

  if (self->operation_in_progress)
    return;

  reset_content_pipeline(self);

  g_clear_pointer(&self->files, g_ptr_array_unref);
  self->files =
    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  g_ptr_array_add(self->files, g_object_ref(file));
  self->current_index = 0;
  show_index(self, 0, FALSE);
  scan_directory(self, file);
  gtk_window_present(GTK_WINDOW(self));
}
