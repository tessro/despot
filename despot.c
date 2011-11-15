#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>
#include <pthread.h>

#include <hiredis/hiredis.h>
#include <libspotify/api.h>

#include "audio.h"
#include "key.h"

// Wait forever
#define REDIS_QUEUE_TIMEOUT 0

extern const uint8_t g_appkey[];

/// The output queue for audio data
static audio_fifo_t g_audiofifo;

/// Synchronization mutex for song loading
static pthread_mutex_t g_loading_mutex;
/// Synchronization mutex for the main thread
static pthread_mutex_t g_notify_mutex;
/// Synchronization condition variable for the main thread
static pthread_cond_t g_notify_cond;

/// Synchronization variable telling the main thread to process events
static int g_notify_do;
/// Non-zero when a track has ended and we have not yet started a new one
static int g_playback_done;

static sp_session *g_sess;
static sp_track   *g_current_track;
static sp_track   *g_loading_track;
static sp_search  *g_last_search;

static redisContext *g_redis;
static redisReply   *g_redis_last_reply;
static char         *g_redis_queue_key    = "despot:queue";
static char         *g_redis_commands_key = "despot:commands";
static const char   *g_host_ip   = "127.0.0.1";
static int           g_host_port = 6379;

static int g_should_monitor = 1; // Monitor thread termination signal
static redisContext *g_redis_monitor;
static redisReply   *g_redis_monitor_last_reply;

static int g_skip_count; // Songs to skip

static redisContext* redis_connect(void)
{
  redisContext *ctxt = redisConnect(g_host_ip, g_host_port);
  if (ctxt->err) {
    fprintf(stderr, "Redis error: %s\n", ctxt->errstr);
    exit(1);
  }

  return ctxt;
}

// FIXME: error states should signal parent
static void *monitor_loop(void *data)
{
  g_redis_monitor = redis_connect();

  while (g_should_monitor) {
    if (g_redis_monitor_last_reply) {
      freeReplyObject(g_redis_monitor_last_reply);
    }

    if ((g_redis_monitor_last_reply = redisCommand(g_redis_monitor, "BLPOP %s %i", g_redis_commands_key, REDIS_QUEUE_TIMEOUT))) {
      if (g_redis_monitor_last_reply->type != REDIS_REPLY_ARRAY || g_redis_monitor_last_reply->elements != 2) {
        fprintf(stderr, "Invalid response from Redis.\n");
        return 4;
      }

      const char *command = g_redis_monitor_last_reply->element[1]->str;

      if (!strcasecmp(command, "NEXT")) {
        pthread_mutex_lock(&g_notify_mutex);
        g_skip_count++;
        pthread_cond_signal(&g_notify_cond);
        pthread_mutex_unlock(&g_notify_mutex);
      }

      printf("Received command: %s\n", command);
    } else {
      fprintf(stderr, "Redis error: %s (%i)\n", g_redis_monitor->errstr, g_redis_monitor->err);

      if (REDIS_ERR_EOF == g_redis_monitor->err) {
        g_redis_monitor = redis_connect();
      } else {
        return 1;
      }
    }
  }

  redisFree(g_redis_monitor);

  return 0;
}

static void play_track(sp_track *track)
{
  if (g_current_track) {
    // Flushing the audio buffer causes stuttering; not flushing causes delay
    // TODO: probably use a compromise approach
    //audio_fifo_flush(&g_audiofifo);
    sp_session_player_unload(g_sess);
  }

  g_current_track = track;

  sp_artist *artist = sp_track_artist(track, 0);
  printf("Now playing: %s - %s\n", sp_artist_name(artist), sp_track_name(track));

  sp_session_player_load(g_sess, track);
  sp_session_player_play(g_sess, 1);
}

static void play_next_track(void);
static void search_complete(sp_search *result, void *userdata)
{
  sp_track *t;

  if (sp_search_num_tracks(result)) {
    t = sp_search_track(result, 0);

    if (sp_track_error(t) != SP_ERROR_OK) {
      fprintf(stderr, "Error: failed loading track from results (code=%i)\n", sp_track_error(t));
      play_next_track();
      return;
    }

    play_track(t);
  } else {
    play_next_track();
  }
}

static void play_next_track(void)
{
  if (g_last_search) {
    sp_search_release(g_last_search);
  }

  if (g_redis_last_reply) {
    freeReplyObject(g_redis_last_reply);
  }

  if ((g_redis_last_reply = redisCommand(g_redis, "BLPOP %s %i", g_redis_queue_key, REDIS_QUEUE_TIMEOUT))) {
    if (g_redis_last_reply->type != REDIS_REPLY_ARRAY || g_redis_last_reply->elements != 2) {
      fprintf(stderr, "Invalid response from Redis.\n");
      exit(4);
    }

    const char *query = g_redis_last_reply->element[1]->str;
    sp_link *link = sp_link_create_from_string(query);

    if (link && SP_LINKTYPE_TRACK == sp_link_type(link)) {
      printf("Link: \"%s\"\n", query);

      // Sync g_loading_track with metadata_updated
      pthread_mutex_lock(&g_loading_mutex);

      if (g_loading_track) {
        sp_track_release(g_loading_track);
      }

      g_loading_track = sp_link_as_track(link);
      sp_track_add_ref(g_loading_track);

      pthread_mutex_unlock(&g_loading_mutex);

      sp_link_release(link);
    } else {
      printf("Search: \"%s\"\n", query);
      g_last_search = sp_search_create(g_sess, query, 0, 1, 0, 0, 0, 0, &search_complete, NULL);
    }
  } else {
    fprintf(stderr, "Redis error: %s (%i)\n", g_redis->errstr, g_redis->err);

    if (REDIS_ERR_EOF == g_redis->err) {
      g_redis = redis_connect();
      play_next_track();
    } else {
      exit(0);
    }
  }
}

//////////////////////////////////////////////////////////////////////
// Session callbacks

static void notify_main_thread(sp_session *sp)
{
  pthread_mutex_lock(&g_notify_mutex);
  g_notify_do = 1;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

static void logged_in(sp_session *sp, sp_error error)
{
  printf("done.\n");

  if (SP_ERROR_OK != error) {
    fprintf(stderr, "Login failed: %s\n", sp_error_message(error));
    exit(2);
  }

  play_next_track();
}

static void metadata_updated(sp_session *session)
{
  // Sync g_loading_track with play_next_track
  pthread_mutex_lock(&g_loading_mutex);

  if (g_loading_track && sp_track_is_loaded(g_loading_track)) {
    play_track(g_loading_track);
    g_loading_track = NULL;
  }

  pthread_mutex_unlock(&g_loading_mutex);
}

static int music_delivery(sp_session *sess, const sp_audioformat *format,
    const void *frames, int num_frames)
{
  audio_fifo_t *af = &g_audiofifo;
  audio_fifo_data_t *afd;
  size_t s;

  if (num_frames == 0)
    return 0; // Audio discontinuity, do nothing

  pthread_mutex_lock(&af->mutex);

  /* Buffer one second of audio */
  if (af->qlen > format->sample_rate) {
    pthread_mutex_unlock(&af->mutex);

    return 0;
  }

  s = num_frames * sizeof(int16_t) * format->channels;

  afd = malloc(sizeof(audio_fifo_data_t) + s);
  memcpy(afd->samples, frames, s);

  afd->nsamples = num_frames;

  afd->rate = format->sample_rate;
  afd->channels = format->channels;

  TAILQ_INSERT_TAIL(&af->q, afd, link);
  af->qlen += num_frames;

  pthread_cond_signal(&af->cond);
  pthread_mutex_unlock(&af->mutex);

  return num_frames;
}

static void end_of_track(sp_session *sess)
{
  pthread_mutex_lock(&g_notify_mutex);
  g_playback_done = 1;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

static void play_token_lost(sp_session *sess)
{
  audio_fifo_flush(&g_audiofifo);

  if (g_current_track != NULL) {
    sp_session_player_unload(g_sess);
    g_current_track = NULL;
  }

  fprintf(stderr, "Quit: Another connection has started playing on this account.\n");
  exit(3);
}

static sp_session_callbacks session_callbacks = {
  .logged_in          = &logged_in,
  .notify_main_thread = &notify_main_thread,
  .music_delivery     = &music_delivery,
  .metadata_updated   = &metadata_updated,
  .play_token_lost    = &play_token_lost,
  .log_message        = NULL,
  .end_of_track       = &end_of_track,
};

static sp_session_config spconfig = {
    .api_version          = SPOTIFY_API_VERSION,
    .cache_location       = "tmp",
    .settings_location    = "tmp",
    .application_key      = g_appkey,
    .application_key_size = 0, // Set in main()
    .user_agent           = "despot/0.1",
    .callbacks            = &session_callbacks,
    NULL,
};

static void usage(const char *progname)
{
  fprintf(stderr, "usage: %s -u <username> -p <password> -h <redis_ip> -P <redis_port>\n", progname);
}

void ms_to_timespec(int ms, struct timespec *ts)
{
#if _POSIX_TIMERS > 0
  clock_gettime(CLOCK_REALTIME, ts);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  TIMEVAL_TO_TIMESPEC(&tv, ts);
#endif
  ts->tv_sec += ms / 1000;
  ts->tv_nsec += (ms % 1000) * 1000000;
}

int main(int argc, char **argv)
{
  sp_session *sp;
  int errno;
  sp_error err;

  const char *username     = NULL;
  const char *password     = NULL;
  const char *redis_prefix = NULL;
  int opt;

  while ((opt = getopt(argc, argv, "u:p:k:h:P:")) != EOF) {
    switch (opt) {
      case 'u':
        username = optarg;
        break;

      case 'p':
        password = optarg;
        break;

      case 'k':
        redis_prefix = optarg;
        break;

      case 'h':
        g_host_ip = optarg;
        break;

      case 'P':
        g_host_port = atoi(optarg);
        break;

      default:
        exit(1);
    }
  }

  if (!username || !password) {
    usage(basename(argv[0]));
    exit(1);
  }

  if (redis_prefix) {
    // TODO: configure Redis vars based on this
  }

  pthread_t monitor_thread;
  if ((errno = pthread_create(&monitor_thread, NULL, &monitor_loop, NULL))) {
    fprintf(stderr, "Unable to create monitor thread: %i\n", errno);
    exit(1);
  }

  g_redis = redis_connect();

  audio_init(&g_audiofifo);

  spconfig.application_key_size = sizeof(g_appkey);

  err = sp_session_create(&spconfig, &sp);

  if (SP_ERROR_OK != err) {
    fprintf(stderr, "Unable to create session: %s\n", sp_error_message(err));
    exit(1);
  }

  g_sess = sp;

  pthread_mutex_init(&g_loading_mutex, NULL);
  pthread_mutex_init(&g_notify_mutex, NULL);
  pthread_cond_init(&g_notify_cond, NULL);

  printf("Logging in...");
  sp_session_login(sp, username, password, 0);
  int next_timeout = 0;

  while (1) {
    if (g_playback_done) {
      play_next_track();

      pthread_mutex_lock(&g_notify_mutex);
      g_playback_done = 0;
      pthread_mutex_unlock(&g_notify_mutex);
    }

    while (g_skip_count) {
      play_next_track();

      pthread_mutex_lock(&g_notify_mutex);
      g_skip_count--;
      pthread_mutex_unlock(&g_notify_mutex);
    }

    // Spin the event loop
    do {
      sp_session_process_events(sp, &next_timeout);
    } while (next_timeout == 0);

    struct timespec ts;
    ms_to_timespec(next_timeout, &ts);

    // Sleep until there are more events, or a song ends
    pthread_mutex_lock(&g_notify_mutex);
    pthread_cond_timedwait(&g_notify_cond, &g_notify_mutex, &ts);
    g_notify_do = 0;
    pthread_mutex_unlock(&g_notify_mutex);
  }

  redisFree(g_redis);

  g_should_monitor = 0;
  if ((errno = pthread_join(monitor_thread, NULL))) {
    fprintf(stderr, "pthread_join failed on monitor thread: %i\n", errno);
    exit(1);
  }

  return 0;
}
