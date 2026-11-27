/* Weststar SDL Browser — GLib's threads, locks and thread-local values on SDL.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * In place of glib/gthread-posix.c (and gthread-win32.c) in Weststar's
 * build of GLib: the system half of GLib's threading, which gthread.c,
 * g_once, GObject's type system and the rest of GLib lock with. SDL has
 * the same primitives on every platform it runs on, so GLib needs no
 * thread code of its own per platform.
 *
 * GLib's lock types are a pointer and two integers that are ready for use
 * when all zeroes. Here the pointer holds the SDL object, made the first
 * time the lock is used.
 */

#include "config.h"

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>

#include "gthread.h"
#include "gthreadprivate.h"
#include "gmain.h"
#include "gmem.h"
#include "gmessages.h"
#include "gslice.h"
#include "gstrfuncs.h"

static void
ns_thread_fatal (const char *what)
{
  fprintf (stderr, "GLib (SDL threads): %s: %s\n", what, SDL_GetError ());
  abort ();
}

/* The SDL object in *slot, made by create() on first use. Two threads may
 * both make one; the loser's is destroyed. */
static void *
ns_lazy (gpointer *slot, void *(*create) (void), void (*destroy) (void *),
         const char *what)
{
  void *object = SDL_GetAtomicPointer (slot);
  if (G_UNLIKELY (object == NULL))
    {
      void *made = create ();
      if (made == NULL)
        ns_thread_fatal (what);
      if (SDL_CompareAndSwapAtomicPointer (slot, NULL, made))
        object = made;
      else
        {
          destroy (made);
          object = SDL_GetAtomicPointer (slot);
        }
    }
  return object;
}

static void *ns_new_mutex (void) { return SDL_CreateMutex (); }
static void ns_free_mutex (void *m) { SDL_DestroyMutex (m); }
static void *ns_new_cond (void) { return SDL_CreateCondition (); }
static void ns_free_cond (void *c) { SDL_DestroyCondition (c); }
static void *ns_new_rwlock (void) { return SDL_CreateRWLock (); }
static void ns_free_rwlock (void *l) { SDL_DestroyRWLock (l); }

#define NS_MUTEX(m)  ((SDL_Mutex *) ns_lazy (&(m)->p, ns_new_mutex, ns_free_mutex, "mutex"))
#define NS_COND(c)   ((SDL_Condition *) ns_lazy (&(c)->p, ns_new_cond, ns_free_cond, "condition"))
#define NS_RWLOCK(l) ((SDL_RWLock *) ns_lazy (&(l)->p, ns_new_rwlock, ns_free_rwlock, "rwlock"))

/* ---- GMutex ---- */

void
g_mutex_init (GMutex *mutex)
{
  mutex->p = NULL;
}

void
g_mutex_clear (GMutex *mutex)
{
  SDL_DestroyMutex (mutex->p);
  mutex->p = NULL;
}

void
g_mutex_lock (GMutex *mutex)
{
  SDL_LockMutex (NS_MUTEX (mutex));
}

void
g_mutex_unlock (GMutex *mutex)
{
  SDL_UnlockMutex (NS_MUTEX (mutex));
}

gboolean
g_mutex_trylock (GMutex *mutex)
{
  return SDL_TryLockMutex (NS_MUTEX (mutex));
}

/* ---- GRecMutex: SDL's mutexes may be locked again by their holder ---- */

void
g_rec_mutex_init (GRecMutex *rec_mutex)
{
  rec_mutex->p = NULL;
}

void
g_rec_mutex_clear (GRecMutex *rec_mutex)
{
  SDL_DestroyMutex (rec_mutex->p);
  rec_mutex->p = NULL;
}

void
g_rec_mutex_lock (GRecMutex *rec_mutex)
{
  SDL_LockMutex (NS_MUTEX (rec_mutex));
}

void
g_rec_mutex_unlock (GRecMutex *rec_mutex)
{
  SDL_UnlockMutex (NS_MUTEX (rec_mutex));
}

gboolean
g_rec_mutex_trylock (GRecMutex *rec_mutex)
{
  return SDL_TryLockMutex (NS_MUTEX (rec_mutex));
}

/* ---- GRWLock ---- */

void
g_rw_lock_init (GRWLock *rw_lock)
{
  rw_lock->p = NULL;
}

void
g_rw_lock_clear (GRWLock *rw_lock)
{
  SDL_DestroyRWLock (rw_lock->p);
  rw_lock->p = NULL;
}

void
g_rw_lock_writer_lock (GRWLock *rw_lock)
{
  SDL_LockRWLockForWriting (NS_RWLOCK (rw_lock));
}

gboolean
g_rw_lock_writer_trylock (GRWLock *rw_lock)
{
  return SDL_TryLockRWLockForWriting (NS_RWLOCK (rw_lock));
}

void
g_rw_lock_writer_unlock (GRWLock *rw_lock)
{
  SDL_UnlockRWLock (NS_RWLOCK (rw_lock));
}

void
g_rw_lock_reader_lock (GRWLock *rw_lock)
{
  SDL_LockRWLockForReading (NS_RWLOCK (rw_lock));
}

gboolean
g_rw_lock_reader_trylock (GRWLock *rw_lock)
{
  return SDL_TryLockRWLockForReading (NS_RWLOCK (rw_lock));
}

void
g_rw_lock_reader_unlock (GRWLock *rw_lock)
{
  SDL_UnlockRWLock (NS_RWLOCK (rw_lock));
}

/* ---- GCond ---- */

void
g_cond_init (GCond *cond)
{
  cond->p = NULL;
}

void
g_cond_clear (GCond *cond)
{
  SDL_DestroyCondition (cond->p);
  cond->p = NULL;
}

void
g_cond_wait (GCond *cond, GMutex *mutex)
{
  SDL_WaitCondition (NS_COND (cond), NS_MUTEX (mutex));
}

void
g_cond_signal (GCond *cond)
{
  SDL_SignalCondition (NS_COND (cond));
}

void
g_cond_broadcast (GCond *cond)
{
  SDL_BroadcastCondition (NS_COND (cond));
}

/* end_time is on g_get_monotonic_time()'s clock, in microseconds. */
gboolean
g_cond_wait_until (GCond *cond, GMutex *mutex, gint64 end_time)
{
  gint64 remaining = end_time - g_get_monotonic_time ();
  if (remaining <= 0)
    return FALSE;
  /* Rounded up to SDL's milliseconds, and never so long that it would
   * not fit them. */
  gint64 ms = (remaining + 999) / 1000;
  if (ms > G_MAXINT32)
    ms = G_MAXINT32;
  return SDL_WaitConditionTimeout (NS_COND (cond), NS_MUTEX (mutex), (Sint32) ms);
}

/* ---- GPrivate: the key's pointer field is SDL's thread-local id ---- */

G_STATIC_ASSERT (sizeof (SDL_TLSID) <= sizeof (gpointer));

gpointer
g_private_get (GPrivate *key)
{
  return SDL_GetTLS ((SDL_TLSID *) &key->p);
}

void
g_private_set (GPrivate *key, gpointer value)
{
  if (!SDL_SetTLS ((SDL_TLSID *) &key->p, value, key->notify))
    ns_thread_fatal ("thread-local value");
}

void
g_private_replace (GPrivate *key, gpointer value)
{
  gpointer old = g_private_get (key);
  g_private_set (key, value);
  if (old != NULL && key->notify != NULL)
    key->notify (old);
}

/* ---- threads ---- */

typedef struct
{
  GRealThread thread;

  SDL_Thread *sdl_thread;
  GThreadFunc proxy;
  gboolean joined;
  GMutex lock;
} NsThread;

static int SDLCALL
ns_thread_entry (void *data)
{
  NsThread *thread = data;
  thread->proxy (thread);
  return 0;
}

void
g_system_thread_free (GRealThread *thread)
{
  NsThread *nt = (NsThread *) thread;

  if (!nt->joined)
    SDL_DetachThread (nt->sdl_thread);
  g_mutex_clear (&nt->lock);
  g_slice_free (NsThread, nt);
}

GRealThread *
g_system_thread_new (GThreadFunc proxy,
                     gulong stack_size,
                     const char *name,
                     GThreadFunc func,
                     gpointer data,
                     GError **error)
{
  NsThread *thread = g_slice_new0 (NsThread);
  GRealThread *base_thread = (GRealThread *) thread;
  SDL_PropertiesID props;

  base_thread->ref_count = 2;
  base_thread->ours = TRUE;
  base_thread->thread.joinable = TRUE;
  base_thread->thread.func = func;
  base_thread->thread.data = data;
  base_thread->name = g_strdup (name);
  thread->proxy = proxy;
  g_mutex_init (&thread->lock);

  props = SDL_CreateProperties ();
  SDL_SetPointerProperty (props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                          (void *) ns_thread_entry);
  SDL_SetStringProperty (props, SDL_PROP_THREAD_CREATE_NAME_STRING, name);
  SDL_SetPointerProperty (props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, thread);
  if (stack_size)
    SDL_SetNumberProperty (props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                           (Sint64) stack_size);
  thread->sdl_thread = SDL_CreateThreadWithProperties (props);
  SDL_DestroyProperties (props);

  if (thread->sdl_thread == NULL)
    {
      g_set_error (error, G_THREAD_ERROR, G_THREAD_ERROR_AGAIN,
                   "Error creating thread: %s", SDL_GetError ());
      g_free (base_thread->name);
      g_mutex_clear (&thread->lock);
      g_slice_free (NsThread, thread);
      return NULL;
    }

  return base_thread;
}

void
g_thread_yield (void)
{
  SDL_Delay (0);
}

void
g_system_thread_wait (GRealThread *thread)
{
  NsThread *nt = (NsThread *) thread;

  g_mutex_lock (&nt->lock);
  if (!nt->joined)
    {
      SDL_WaitThread (nt->sdl_thread, NULL);
      nt->joined = TRUE;
    }
  g_mutex_unlock (&nt->lock);
}

/* SDL has no way for a thread to end itself from within a call: a thread
 * ends by returning from its function. g_thread_exit() is not used by the
 * browser or by the parts of GLib it reaches. */
void
g_system_thread_exit (void)
{
  fputs ("GLib (SDL threads): g_thread_exit() is not available; "
         "return from the thread's function instead\n", stderr);
  abort ();
}

/* The name is given to SDL when the thread is created. */
void
g_system_thread_set_name (const gchar *name)
{
  (void) name;
}
