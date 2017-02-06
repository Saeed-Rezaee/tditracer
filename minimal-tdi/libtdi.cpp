extern "C" {

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#if 0
#include <linux/futex.h>
#endif
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include <dirent.h>

void tditrace(const char *format, ...);
void tditrace_ex(int mask, const char *format, ...);

static pid_t gpid;
static char gprocname[128];

#if 0

struct simplefu_semaphore {
  int avail;
  int waiters;
};

typedef struct simplefu_semaphore *simplefu;

struct simplefu_mutex {
  struct simplefu_semaphore sema;
};

typedef struct simplefu_mutex *simplemu;

struct simplefu_mutex myMutex;

static void simplefu_down(simplefu who) {
  int val;
  do {
    val = who->avail;
    if (val > 0 && __sync_bool_compare_and_swap(&who->avail, val, val - 1))
      return;
    __sync_fetch_and_add(&who->waiters, 1);
    syscall(__NR_futex, &who->avail, FUTEX_WAIT, val, NULL, 0, 0);
    __sync_fetch_and_sub(&who->waiters, 1);
  } while (1);
}

static void simplefu_up(simplefu who) {
  int nval = __sync_add_and_fetch(&who->avail, 1);
  if (who->waiters > 0) {
    syscall(__NR_futex, &who->avail, FUTEX_WAKE, nval, NULL, 0, 0);
  }
}

static void simplefu_mutex_init(simplemu mx) {
  mx->sema.avail = 1;
  mx->sema.waiters = 0;
}

static void simplefu_mutex_lock(simplemu mx) { simplefu_down(&mx->sema); }
static void simplefu_mutex_unlock(simplemu mx) { simplefu_up(&mx->sema); }

static void LOCK_init(void) {
  simplefu_mutex_init(&myMutex);
} 

static void LOCK(void) {
  simplefu_mutex_lock(&myMutex);
} 

static void UNLOCK(void) {
  simplefu_mutex_unlock(&myMutex);
}

#else

static pthread_mutex_t lock;

static void LOCK_init(void) {
  if (pthread_mutex_init(&lock, NULL) != 0) {
    fprintf(stderr, "\n mutex init failed\n");
  }
}

static void LOCK(void) { pthread_mutex_lock(&lock); }
static void UNLOCK(void) { pthread_mutex_unlock(&lock); }

#endif

#define TASKS 0
#define ISRS 1
#define SEMAS 2
#define QUEUES 3
#define EVENTS 4
#define VALUES 5
#define CYCLES 6
#define NOTES 7
#define AGENTS 8
#define MEMORYCYCLES 9

static unsigned int gtracebuffersize = 16 * 1024 * 1024;

static char gtracebufferfilename[128];
static struct stat gtrace_buffer_st;

static char *gtrace_buffer;
static char *trace_buffer_byte_ptr;
static unsigned int *trace_buffer_dword_ptr;
static unsigned int *gtrace_buffer_rewind_ptr;

static int tditrace_inited;
static int reported_full;
static int report_tid;

typedef unsigned long long _u64;

// 128 tasks of 1024 chars each
static char tasks_array[1024][128];
static int nr_tasks = 0;

// 128 isrs of 1024 chars each
static char isrs_array[1024][128];
static int nr_isrs = 0;

// 128 semas of 1024 chars each
static char semas_array[1024][128];
static int nr_semas = 0;

// 128 queues of 1024 chars each
static char queues_array[1024][128];
static int nr_queues = 0;
static int prev_queues[128];

// 128 values of 1024 chars each
static char values_array[1024][128];
static int nr_values = 0;

// 128 cycles of 1024 chars each
static char cycles_array[1024][128];
static int nr_cycles = 0;

// 128 events of 1024 chars each
static char events_array[1024][128];
static int nr_events = 0;

// 128 notes of 1024 chars each
static char notes_array[1024][128];
static int nr_notes = 0;

// 128 notes of 1000 agents each
static char agents_array[1024][128];
static int nr_agents = 0;

static void tditrace_rewind();

static void addentry(FILE *stdout, const char *text_in, int text_len,
                     _u64 timestamp, const char *procname, int pid, int tid,
                     int nr_numbers, unsigned int *numbers,
                     unsigned short identifier) {
  int i;
  int entry;
  char fullentry[1024];

  char name[1024];
  int value = 0;

  // if (nr_numbers) fprintf(stderr, "nr_numbers=%d(%x)\n", nr_numbers,
  // numbers[0]);

  // fprintf(stderr, "identifier=%x(%d)(%d)\n", identifier, nr_numbers,
  // text_len);

  // fprintf(stderr, "text_in(%d)=\"", text_len);
  // for (i=0;i<text_len;i++)
  //   fprintf(stderr, "%c", text_in[i]);
  // fprintf(stderr,"\"\n");

  char text_in1[1024];
  char *text = text_in1;

  sprintf(text_in1, "[%s][%d][%d]", procname, pid, tid);
  int procpidtidlen = strlen(text_in1);

  if ((strncmp(text_in, "@T+", 3) == 0) || (strncmp(text_in, "@T-", 3) == 0) ||
      (strncmp(text_in, "@I+", 3) == 0) || (strncmp(text_in, "@I-", 3) == 0) ||
      (strncmp(text_in, "@A+", 3) == 0) || (strncmp(text_in, "@A-", 3) == 0) ||
      (strncmp(text_in, "@S+", 3) == 0) || (strncmp(text_in, "@E+", 3) == 0)) {
    strncpy(text_in1, text_in, 3);

    snprintf(&text_in1[3], procpidtidlen + text_len + 1 - 3, "[%s][%d][%d]%s",
             procname, pid, tid, &text_in[3]);

  } else {
    if (text_len)
      snprintf(text_in1, procpidtidlen + text_len + 1, "[%s][%d][%d]%s",
               procname, pid, tid, text_in);
    else
      snprintf(text_in1, procpidtidlen + 2 + 1, "[%s][%d][%d]%02X", procname,
               pid, tid, identifier);
  }

  // fprintf(stderr, "text=\"%s\"\n", text);

  // for (i=0;i<30;i++)
  //   fprintf(stderr, "%02x ", text[i]);
  // fprintf(stderr,"\n");

  // get rid of any '\n', replace with '_'
  for (i = 0; i < (int)strlen(text); i++) {
    if ((text[i] == 13) || (text[i] == 10)) {
      text[i] = 0x5f;
    }
  }

  for (i = 0; i < nr_numbers; i++) {
    char number[16];
    sprintf(number, "%s@%d=%x", i == 0 ? " " : "", i, numbers[i]);
    strcat(text, number);
  }

  /*
   * TASKS entry
   *
   */
  if ((strncmp(text, "@T+", 3) == 0) || (strncmp(text, "@T-", 3) == 0)) {
    int enter_not_exit = (strncmp(text + 2, "+", 1) == 0);

    text += 3;
    // if the entry has not been seen before, add a new entry for it and
    // issue a NAM
    entry = -1;
    for (i = 0; i < nr_tasks; i++) {
      char *pos;
      char comparestr[1024];

      strcpy(comparestr, text);
      /*
       * the portion of the text before the first space in the text
       * is considered the unique part of the text
       */
      pos = strchr(comparestr, ' ');
      if (pos) {
        *pos = 0;
      }

      if (strcmp(tasks_array[i], comparestr) == 0) {
        // found the entry
        entry = i;
        break;
      }
    }

    // Do we need to add the entry?
    if (entry == -1) {
      int len;
      char *pos;

      pos = strchr(text, ' ');
      if (pos)
        len = pos - text;
      else
        len = strlen(text);

      strncpy(tasks_array[nr_tasks], text, len);
      tasks_array[nr_tasks][len] = 0;

      entry = nr_tasks;
      nr_tasks++;

      // Since we added a new entry we need to create a NAM, with only the
      // first part of the text
      sprintf(fullentry, "NAM %d %d %s\n", TASKS, TASKS * 1000 + entry,
              tasks_array[entry]);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
    }

    // Add the STA or STO entry
    sprintf(fullentry, "%s %d %d %lld\n", enter_not_exit ? "STA" : "STO", TASKS,
            TASKS * 1000 + entry, timestamp);
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    // Add the DSC entry, replace any spaces with commas
    sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);

    for (i = 8; i < (int)strlen(fullentry); i++) {
      if (fullentry[i] == 32) fullentry[i] = 0x2c;
    }
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    return;
  }

  /*
   * SEMAS entry
   *
   */
  if (strncmp(text, "@S+", 3) == 0) {
    text += 3;
    // if the entry has not been seen before, add a new entry for it and
    // issue a NAM
    entry = -1;
    for (i = 0; i < nr_semas; i++) {
      char *pos;
      char comparestr[1024];

      strcpy(comparestr, text);
      /*
       * the portion of the text before the first space in the text
       * is considered the unique part of the text
       */
      pos = strchr(comparestr, ' ');
      if (pos) {
        *pos = 0;
      }

      if (strcmp(semas_array[i], comparestr) == 0) {
        // found the entry
        entry = i;
        break;
      }
    }

    // Do we need to add the entry?
    if (entry == -1) {
      int len;
      char *pos;

      pos = strchr(text, ' ');
      if (pos)
        len = pos - text;
      else
        len = strlen(text);

      strncpy(semas_array[nr_semas], text, len);
      semas_array[nr_semas][len] = 0;

      entry = nr_semas;
      nr_semas++;

      // Since we added a new entry we need to create a NAM, with only the
      // first part of the text
      sprintf(fullentry, "NAM %d %d %s\n", SEMAS, SEMAS * 1000 + entry,
              semas_array[entry]);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
    }

    // Add the OCC entry
    sprintf(fullentry, "OCC %d %d %lld\n", SEMAS, SEMAS * 1000 + entry,
            timestamp);
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    // Add the DSC entry, replace any spaces with commas
    sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);
    for (i = 8; i < (int)strlen(fullentry); i++) {
      if (fullentry[i] == 32) fullentry[i] = 0x2c;
    }
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    return;
  }

  /*
   * ISRS entry
   *
   */
  if ((strncmp(text, "@I+", 3) == 0) || (strncmp(text, "@I-", 3) == 0)) {
    int enter_not_exit = (strncmp(text + 2, "+", 1) == 0);

    text += 3;
    // if the entry has not been seen before, add a new entry for it and
    // issue a NAM
    entry = -1;
    for (i = 0; i < nr_isrs; i++) {
      char *pos;
      char comparestr[1024];

      strcpy(comparestr, text);
      /*
       * the portion of the text before the first space in the text
       * is considered the unique part of the text
       */
      pos = strchr(comparestr, ' ');
      if (pos) {
        *pos = 0;
      }

      if (strcmp(isrs_array[i], comparestr) == 0) {
        // found the entry
        entry = i;
        break;
      }
    }

    // Do we need to add the entry?
    if (entry == -1) {
      int len;
      char *pos;

      pos = strchr(text, ' ');
      if (pos)
        len = pos - text;
      else
        len = strlen(text);

      strncpy(isrs_array[nr_isrs], text, len);
      isrs_array[nr_isrs][len] = 0;

      entry = nr_isrs;
      nr_isrs++;

      // Since we added a new entry we need to create a NAM, with only the
      // first part of the text
      sprintf(fullentry, "NAM %d %d %s\n", ISRS, ISRS * 1000 + entry,
              isrs_array[entry]);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
    }

    // Add the STA or STO entry
    sprintf(fullentry, "%s %d %d %lld\n", enter_not_exit ? "STA" : "STO", ISRS,
            ISRS * 1000 + entry, timestamp);
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    // Add the DSC entry, replace any spaces with commas
    sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);
    for (i = 8; i < (int)strlen(fullentry); i++) {
      if (fullentry[i] == 32) fullentry[i] = 0x2c;
    }
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    return;
  }

  /*
   * EVENTS entry
   *
   */
  if (strncmp(text, "@E+", 3) == 0) {
    text += 3;
    // if the entry has not been seen before, add a new entry for it and
    // issue a NAM
    entry = -1;
    for (i = 0; i < nr_events; i++) {
      char *pos;
      char comparestr[1024];

      strcpy(comparestr, text);
      /*
       * the portion of the text before the first space in the text
       * is considered the unique part of the text
       */
      pos = strchr(comparestr, ' ');
      if (pos) {
        *pos = 0;
      }

      if (strcmp(events_array[i], comparestr) == 0) {
        // found the entry
        entry = i;
        break;
      }
    }

    // Do we need to add the entry?
    if (entry == -1) {
      int len;
      char *pos;

      pos = strchr(text, ' ');
      if (pos)
        len = pos - text;
      else
        len = strlen(text);

      strncpy(events_array[nr_events], text, len);
      events_array[nr_events][len] = 0;

      entry = nr_events;
      nr_events++;

      // Since we added a new entry we need to create a NAM, with only the
      // first part of the text
      sprintf(fullentry, "NAM %d %d %s\n", EVENTS, EVENTS * 1000 + entry,
              events_array[entry]);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
    }

    // Add the OCC entry
    sprintf(fullentry, "OCC %d %d %lld\n", EVENTS, EVENTS * 1000 + entry,
            timestamp);
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    // Add the DSC entry, replace any spaces with commas
    sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);
    for (i = 8; i < (int)strlen(fullentry); i++) {
      if (fullentry[i] == 32) fullentry[i] = 0x2c;
    }
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    return;
  }

  /*
   * AGENTS entry
   *
   */
  if ((strncmp(text, "@A+", 3) == 0) || (strncmp(text, "@A-", 3) == 0)) {
    // fprintf(stderr, "text=\"%s\"\n", text);

    int enter_not_exit = (strncmp(text + 2, "+", 1) == 0);

    text += 3;
    // if the entry has not been seen before, add a new entry for it and
    // issue a NAM
    entry = -1;
    for (i = 0; i < nr_agents; i++) {
      char *pos;
      char comparestr[1024];

      strcpy(comparestr, text);
      /*
       * the portion of the text before the first space in the text
       * is considered the unique part of the text
       */
      pos = strchr(comparestr, ' ');
      if (pos) {
        *pos = 0;
      }

      if (strcmp(agents_array[i], comparestr) == 0) {
        // found the entry
        entry = i;
        break;
      }
    }

    // Do we need to add the entry?
    if (entry == -1) {
      int len;
      char *pos;

      pos = strchr(text, ' ');
      if (pos)
        len = pos - text;
      else
        len = strlen(text);

      strncpy(agents_array[nr_agents], text, len);
      agents_array[nr_agents][len] = 0;

      entry = nr_agents;
      nr_agents++;

      // Since we added a new entry we need to create a NAM, with only the
      // first part of the text
      sprintf(fullentry, "NAM %d %d %s\n", AGENTS, AGENTS * 1000 + entry,
              agents_array[entry]);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
    }

    // Add the STA or STO entry
    sprintf(fullentry, "%s %d %d %lld\n", enter_not_exit ? "STA" : "STO",
            AGENTS, AGENTS * 1000 + entry, timestamp);
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    // Add the DSC entry, replace any spaces with commas
    sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);
    for (i = 8; i < (int)strlen(fullentry); i++) {
      if (fullentry[i] == 32) fullentry[i] = 0x2c;
    }
    fwrite(fullentry, strlen(fullentry), 1, stdout);

    return;
  }

  /*
   * QUEUES entry
   *
   * if text is of the form name~value then interpret this as a queue and add
   * a queue entry for it.
   * check whether '~' exists and pull out the name and value
   */

  name[0] = 0;
  char *q;
  q = strchr(text, '~');
  if (q && isdigit(q[1])) {
    for (i = 0; i < (int)strlen(text); i++) {
      if (text[i] == 32) break;
      if (text[i] == 126) {
        // fprintf(stderr, "text1=\"%s\"\n", text);

        strncpy(name, text, i);
        name[i] = 0;
        value = atoi(text + i + 1);
        text[i] = 32;  // create split marker

        // fprintf(stderr, "name=\"%s\", text=\"%s\"\n", name, text);
      }
    }

    if (strlen(name)) {
      // check to see if we need to add this value entry (first occurrence)
      entry = -1;

      for (i = 0; i < nr_queues; i++) {
        if (strcmp(queues_array[i], name) == 0) {
          // found the entry
          entry = i;
          break;
        }
      }

      // Do we need to add the entry?
      if (entry == -1) {
        strcpy(queues_array[nr_queues], name);
        entry = nr_queues;
        nr_queues++;

        // Since we added a new entry we need to create a NAM
        sprintf(fullentry, "NAM %d %d %s\n", QUEUES, QUEUES * 1000 + entry,
                queues_array[entry]);
        fwrite(fullentry, strlen(fullentry), 1, stdout);

        // reset the prev_value;
        prev_queues[entry] = 0;
      }

      // fill in the value
      // add a STA (with the delta) or a STO (with the delta) depending on
      // whether the value went up or went down
      if (value >= prev_queues[entry])
        sprintf(fullentry, "STA %d %d %lld %d\n", QUEUES, QUEUES * 1000 + entry,
                timestamp, value - prev_queues[entry]);
      else
        sprintf(fullentry, "STO %d %d %lld %d\n", QUEUES, QUEUES * 1000 + entry,
                timestamp, prev_queues[entry] - value);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
      prev_queues[entry] = value;

      return;
    }
  }

  /*
     * VALUES entry
     *
     * if text is of the form name#value then interpret this as a value and add
     * a value entry for it.
     * check whether '#' exists and pull out the name and value
     */

  name[0] = 0;
  char *v;
  v = strchr(text, '#');
  if (v && isdigit(v[1])) {
    for (i = 0; i < (int)strlen(text); i++) {
      if (text[i] == 32) break;
      if (text[i] == 35) {
        // fprintf(stderr, "text1=\"%s\"\n", text);

        strncpy(name, text, i);
        name[i] = 0;
        value = atoi(text + i + 1);
        text[i] = 32;  // create split marker

        // fprintf(stderr, "name=\"%s\", text=\"%s\"\n", name, text);
      }
    }

    if (strlen(name)) {
      // check to see if we need to add this value entry (first occurrence)
      entry = -1;

      for (i = 0; i < nr_values; i++) {
        if (strcmp(values_array[i], name) == 0) {
          // found the entry
          entry = i;
          break;
        }
      }

      // Do we need to add the entry?
      if (entry == -1) {
        strcpy(values_array[nr_values], name);
        entry = nr_values;
        nr_values++;

        // Since we added a new entry we need to create a NAM
        sprintf(fullentry, "NAM %d %d %s\n", VALUES, VALUES * 1000 + entry,
                values_array[entry]);
        fwrite(fullentry, strlen(fullentry), 1, stdout);
      }

      // fill in the value
      // add a TIM and a VAL
      sprintf(fullentry, "TIM %lld\n", timestamp);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
      sprintf(fullentry, "VAL %d %d %d\n", VALUES, VALUES * 1000 + entry,
              value);
      fwrite(fullentry, strlen(fullentry), 1, stdout);

      return;
    }
  }

  /*
     * CYCLES entry
     *
     * if text is of the form name^cycle then interpret this as a cycle and add
     * a cycle entry for it.
     * check whether '^' exists and pull out the name and cycle
     */

  name[0] = 0;
  char *c;
  c = strchr(text, '^');
  if (c && isdigit(c[1])) {
    for (i = 0; i < (int)strlen(text); i++) {
      if (text[i] == 32) break;
      if (text[i] == 94) {
        // fprintf(stderr, "text1=\"%s\"\n", text);

        strncpy(name, text, i);
        name[i] = 0;
        value = atoi(text + i + 1);
        text[i] = 32;  // create split marker

        // fprintf(stderr, "name=\"%s\", text=\"%s\"\n", name, text);
      }
    }

    if (strlen(name)) {
      // check to see if we need to add this cycle entry (first occurrence)
      entry = -1;

      for (i = 0; i < nr_cycles; i++) {
        if (strcmp(cycles_array[i], name) == 0) {
          // found the entry
          entry = i;
          break;
        }
      }

      // Do we need to add the entry?
      if (entry == -1) {
        strcpy(cycles_array[nr_cycles], name);
        entry = nr_cycles;
        nr_cycles++;

        // Since we added a new entry we need to create a NAM
        sprintf(fullentry, "NAM %d %d %s\n", CYCLES, CYCLES * 1000 + entry,
                cycles_array[entry]);
        fwrite(fullentry, strlen(fullentry), 1, stdout);
      }

      // fill in the value
      // add a TIM and a CYCLE
      sprintf(fullentry, "TIM %lld\n", timestamp);
      fwrite(fullentry, strlen(fullentry), 1, stdout);
      sprintf(fullentry, "VAL %d %d %lld\n", CYCLES, CYCLES * 1000 + entry,
              value * 10000000LL);
      fwrite(fullentry, strlen(fullentry), 1, stdout);

      return;
    }
  }

  /*
   * Treat everything else as a NOTES entry
   *
   */

  // if the entry has not been seen before, add a new entry for it and issue a
  // NAM
  entry = -1;
  for (i = 0; i < nr_notes; i++) {
    char *pos;
    char comparestr[1024];

    strcpy(comparestr, text);
    /*
     * the portion of the text before the first space in the text
     * is considered the unique part of the text
     */
    pos = strchr(comparestr, ' ');
    if (pos) {
      *pos = 0;
    }

    if (strcmp(notes_array[i], comparestr) == 0) {
      // found the entry
      entry = i;
      break;
    }
  }

  // Do we need to add the entry?
  if (entry == -1) {
    int len;
    char *pos;

    pos = strchr(text, ' ');
    if (pos)
      len = pos - text;
    else
      len = strlen(text);

    strncpy(notes_array[nr_notes], text, len);
    notes_array[nr_notes][len] = 0;

    entry = nr_notes;
    nr_notes++;

    // Since we added a new entry we need to create a NAM, with only the
    // first part of the text
    sprintf(fullentry, "NAM %d %d %s\n", NOTES, NOTES * 1000 + entry,
            notes_array[entry]);
    fwrite(fullentry, strlen(fullentry), 1, stdout);
  }

  // Add the OCC entry
  sprintf(fullentry, "OCC %d %d %lld\n", NOTES, NOTES * 1000 + entry,
          timestamp);
  fwrite(fullentry, strlen(fullentry), 1, stdout);

  // Add the DSC entry, replace any spaces with commas
  sprintf(fullentry, "DSC %d %d %s\n", 0, 0, text + procpidtidlen);
  for (i = 8; i < (int)strlen(fullentry); i++) {
    if (fullentry[i] == 32) fullentry[i] = 0x2c;
  }
  fwrite(fullentry, strlen(fullentry), 1, stdout);
}

typedef struct {
  char filename[128];
  char procname[64];
  int pid;
  char *bufmmapped;
  int bufsize;
  char *ptr;
  unsigned int *dword_ptr;
  _u64 timeofday_start;
  _u64 monotonic_start;
  _u64 monotonic_first;
  _u64 monotonic_timestamp;
  unsigned short identifier;
  int nr_numbers;
  unsigned int *numbers;

  char *text;
  int text_len;
  int tid;
  int valid;
} tracebuffer_t;

static tracebuffer_t tracebuffers[10];

static void parse(int bid) {
  /*
   * [    ]marker, lower 2 bytes is total length in dwords, upper byte is
   * identifier,
   *       middle byte is nr numbers
   * [    ]clock_monotonic_timestamp.tv_nsec
   * [    ]clock_monotonic_timestamp.tv_sec
   * [    ]<optional> numbers
   * [    ]<optional> text, padded with 0's to multiple of 4 bytes
   */

  // fprintf(stderr, "parse %d, tracebuffers[bid].dword_ptr = 0x%08x\n", bid,
  //       (int)tracebuffers[bid].dword_ptr);

  tracebuffers[bid].valid = 0;

  unsigned int *p = tracebuffers[bid].dword_ptr;
  unsigned int marker = *p++;

  // fprintf(stderr,"marker[0x%08x]\n", marker);

  tracebuffers[bid].identifier = marker >> 24;

  tracebuffers[bid].dword_ptr += marker & 0xffff;

  int tvsec = *p++;
  int tvnsec = *p++;
  tracebuffers[bid].monotonic_timestamp =
      (_u64)tvsec * (_u64)1000000000 + (_u64)tvnsec;

  tracebuffers[bid].nr_numbers = (marker >> 16) & 0xff;
  tracebuffers[bid].numbers = p;
  int i = (marker >> 16) & 0xff;
  while (i--) p++;

  tracebuffers[bid].tid = 0;
  tracebuffers[bid].text = (char *)p;
  tracebuffers[bid].text_len =
      (((marker & 0xffff) - 3) - ((marker >> 16) & 0xff)) << 2;
  tracebuffers[bid].valid = (marker != 0);

  // fprintf(stderr,"marker[0x%08x](%d)(%d)\n", marker,
  // tracebuffers[bid].nr_numbers, tracebuffers[bid].text_len);

  // fprintf(stderr, "monotonic_timestamp:%lld, [%s]\n",
  //       tracebuffers[bid].monotonic_timestamp, tracebuffers[bid].text);
}

#if 0
static unsigned int find_process_name(char *p_processname) {
  DIR *dir_p;
  struct dirent *dir_entry_p;
  char dir_name[64];
  char target_name[128];
  int target_result;
  char exe_link[128];
  int result;

  result = 0;
  dir_p = opendir("/proc/");
  while (NULL != (dir_entry_p = readdir(dir_p))) {
    if (strspn(dir_entry_p->d_name, "0123456789") ==
        strlen(dir_entry_p->d_name)) {
      strcpy(dir_name, "/proc/");
      strcat(dir_name, dir_entry_p->d_name);
      strcat(dir_name, "/");
      exe_link[0] = 0;
      strcat(exe_link, dir_name);
      strcat(exe_link, "exe");
      target_result = readlink(exe_link, target_name, sizeof(target_name) - 1);
      if (target_result > 0) {
        target_name[target_result] = 0;
        if (strstr(target_name, p_processname) != NULL) {
          result = atoi(dir_entry_p->d_name);
          closedir(dir_p);
          return result;
        }
      }
    }
  }
  closedir(dir_p);
  return result;
}
#endif

static void get_process_name_by_pid(const int pid, char *name) {
  char fullname[1024];
  if (name) {
    sprintf(name, "/proc/%d/cmdline", pid);

    FILE *f = fopen(name, "r");
    if (f) {
      size_t size;
      size = fread(fullname, sizeof(char), 1024, f);

      if (size > 0) {
        if ('\n' == fullname[size - 1]) fullname[size - 1] = '\0';

        if (strrchr(fullname, '/')) {
          strcpy(name, strrchr(fullname, '/') + 1);
        } else {
          strcpy(name, fullname);
        }
      }
      fclose(f);
    }
  }
}

static char proc_self_maps[16 * 1024 + 1];

static void dump_proc_self_maps(void) {
  int fd;
  int bytes;

  fprintf(stderr, "tdi: maps...[%s][%d]\n", gprocname, gpid);

  tditrace("MAPS [%s][%d] begin", gprocname, gpid);

  fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) {
    tditrace("MAPS [%s][%d] end", gprocname, gpid);
    return;
  }

  while (1) {
    bytes = read(fd, proc_self_maps, sizeof(proc_self_maps) - 1);
    if ((bytes == -1) && (errno == EINTR))
      /* keep trying */;
    else if (bytes > 0) {
      proc_self_maps[bytes] = '\0';

      char *saveptr;
      char *line = strtok_r(proc_self_maps, "\n", &saveptr);

      while (line) {
        if (strlen(line) > 50) {
          tditrace("MAPS [%s][%d] %s", gprocname, gpid, line);
        }
        line = strtok_r(NULL, "\n", &saveptr);
      }

    } else
      break;
  }

  close(fd);

  tditrace("MAPS [%s][%d] end", gprocname, gpid);
}

static int gmask = 0x0;

static int do_sysinfo = 0;
static int do_selfinfo = 0;
static int do_persecond = 0;

static int gtouch = 0x0;

static int monitor;

static int do_offload = 0;
static char offload_location[256] = {0};
static int offload_counter = 0;
static int offload_over50 = 0;

static int do_wrap = 0;

static int do_maps = 0;

static int do_dump_proc_self_maps = 0;

extern void shadercapture_writeshaders(void) __attribute__((weak));
extern void texturecapture_writepngtextures(void) __attribute__((weak));
extern void texturecapture_deletetextures(void) __attribute__((weak));
extern void framecapture_writepngframes(void) __attribute__((weak));
extern void framecapture_deleteframes(void) __attribute__((weak));

static void tmpfs_message(void) {
  fprintf(stderr, "\n");
  fprintf(stderr,
          "tdi: init[%s][%d], "
          "----------------------------------------------------------------"
          "\n",
          gprocname, gpid);
  fprintf(stderr, "tdi: init[%s][%d], adjust the trace buffer size:\n",
          gprocname, gpid);
  fprintf(stderr, "tdi: init[%s][%d],     \"TRACEBUFFERSIZE=<MB>\"\n",
          gprocname, gpid);
  fprintf(stderr, "tdi: init[%s][%d], adjust the /tmp size:\n", gprocname,
          gpid);
  fprintf(stderr,
          "tdi: init[%s][%d],     \"mount -o "
          "remount,noexec,nosuid,nr_blocks=15000 /tmp\"\n",
          gprocname, gpid);
  fprintf(stderr,
          "tdi: init[%s][%d], "
          "----------------------------------------------------------------"
          "\n",
          gprocname, gpid);
  fprintf(stderr, "\n");
}

#define OUT(name, trace, value)           \
                                          \
  static unsigned int _##name##_seen = 0; \
  static unsigned int _##name##_prev;     \
  unsigned int _##name = value;           \
  if (!_##name##_seen) {                  \
    _##name##_seen = 1;                   \
    tditrace(trace, _##name);             \
  } else if (_##name##_prev != _##name) { \
    tditrace(trace, _##name);             \
  }                                       \
  _##name##_prev = _##name;

static void sample_info(void) {
  char line[256];
  FILE *f = NULL;

  int do_structsysinfo = do_sysinfo;
  int do_structmallinfo = do_selfinfo;
  int do_structgetrusage = 0;  // do_selfinfo;
  int do_procvmstat = do_sysinfo;
  int do_procmeminfo = do_sysinfo;
  int do_proctvbcmmeminfo = do_sysinfo;
  int do_procstat = do_sysinfo;
  int do_procdiskstats = do_sysinfo;
  int do_procnetdev = do_sysinfo;

  int do_procselfstatm = do_selfinfo;
  int do_procselfstatus = 0;  // do_selfinfo;
  int do_procselfsmaps = do_selfinfo;

  struct sysinfo si;
  if (do_structsysinfo) {
    sysinfo(&si);

    // struct sysinfo {
    //         long uptime;             /* Seconds since boot */
    //         unsigned long loads[3];  /* 1, 5, and 15 minute load averages
    //         */
    //         unsigned long totalram;  /* Total usable main memory size */
    //         unsigned long freeram;   /* Available memory size */
    //         unsigned long sharedram; /* Amount of shared memory */
    //         unsigned long bufferram; /* Memory used by buffers */
    //         unsigned long totalswap; /* Total swap space size */
    //         unsigned long freeswap;  /* Swap space still available */
    //         unsigned short procs;    /* Number of current processes */
    //         unsigned long totalhigh; /* Total high memory size */
    //         unsigned long freehigh;  /* Available high memory size */
    //         unsigned int mem_unit;   /* Memory unit size in bytes */
    //         char _f[20-2*sizeof(long)-sizeof(int)];
    //                                  /* Padding to 64 bytes */
    //     };
  }

  struct mallinfo mi;
  if (do_structmallinfo) {
    mi = mallinfo();

    // struct mallinfo {
    //       int arena;     /* Non-mmapped space allocated (bytes) */
    //       int ordblks;   /* Number of free chunks */
    //       int smblks;    /* Number of free fastbin blocks */
    //       int hblks;     /* Number of mmapped regions */
    //       int hblkhd;    /* Space allocated in mmapped regions
    //       (bytes) */
    //       int usmblks;   /* Maximum total allocated space (bytes) */
    //       int fsmblks;   /* Space in freed fastbin blocks (bytes) */
    //       int uordblks;  /* Total allocated space (bytes) */
    //       int fordblks;  /* Total free space (bytes) */
    //       int keepcost;  /* Top-most, releasable space (bytes) */
    //    };
  }

  struct rusage ru;
  static int ru_bi_base = 0;
  static int ru_bo_base = 0;
  if (do_structgetrusage) {
    getrusage(RUSAGE_SELF, &ru);

    if (ru_bi_base == 0) ru_bi_base = ru.ru_inblock;
    if (ru_bo_base == 0) ru_bo_base = ru.ru_oublock;

    // struct rusage {
    //       struct timeval ru_utime; /* user CPU time used */
    //       struct timeval ru_stime; /* system CPU time used */
    //       long   ru_maxrss;        /* maximum resident set size */
    //       long   ru_ixrss;         /* integral shared memory size */
    //       long   ru_idrss;         /* integral unshared data size */
    //       long   ru_isrss;         /* integral unshared stack size */
    //       long   ru_minflt;        /* page reclaims (soft page faults) */
    //       long   ru_majflt;        /* page faults (hard page faults) */
    //       long   ru_nswap;         /* swaps */
    //       long   ru_inblock;       /* block input operations */
    //       long   ru_oublock;       /* block output operations */
    //       long   ru_msgsnd;        /* IPC messages sent */
    //       long   ru_msgrcv;        /* IPC messages received */
    //       long   ru_nsignals;      /* signals received */
    //       long   ru_nvcsw;         /* voluntary context switches */
    //       long   ru_nivcsw;        /* involuntary context switches */
    //   };
  }

  int pswpin = 0;
  int pswpout = 0;
  int pgpgin = 0;
  int pgpgout = 0;
  int pgfault = 0;
  int pgmajfault = 0;

  if (do_procvmstat) {
    f = fopen("/proc/vmstat", "r");
    static int pswpin_base = 0;
    static int pswpout_base = 0;
    static int pgpgin_base = 0;
    static int pgpgout_base = 0;
    static int pgfault_base = 0;
    static int pgmajfault_base = 0;

    if (f) {
      while (fgets(line, 256, f)) {
        sscanf(line, "pswpin %d", &pswpin);
        sscanf(line, "pswpout %d", &pswpout);
        sscanf(line, "pgpgin %d", &pgpgin);
        sscanf(line, "pgpgout %d", &pgpgout);
        sscanf(line, "pgfault %d", &pgfault);
        if (sscanf(line, "pgmajfault %d", &pgmajfault)) break;
      }
      if (pswpin_base == 0) pswpin_base = pswpin;
      if (pswpout_base == 0) pswpout_base = pswpout;
      if (pgpgin_base == 0) pgpgin_base = pgpgin;
      if (pgpgout_base == 0) pgpgout_base = pgpgout;
      if (pgfault_base == 0) pgfault_base = pgfault;
      if (pgmajfault_base == 0) pgmajfault_base = pgmajfault;
      pswpin -= pswpin_base;
      pswpout -= pswpout_base;
      pgpgin -= pgpgin_base;
      pgpgout -= pgpgout_base;
      pgfault -= pgfault_base;
      pgmajfault -= pgmajfault_base;

      fclose(f);
    }
  }

  int cached = 0;
  int active_anon = 0;
  int inactive_anon = 0;
  int active_file = 0;
  int inactive_file = 0;
  // int mapped = 0;

  if (do_procmeminfo) {
    f = fopen("/proc/meminfo", "r");
    if (f) {
      while (fgets(line, 256, f)) {
        sscanf(line, "Cached: %d", &cached);
        sscanf(line, "Active(anon): %d", &active_anon);
        sscanf(line, "Inactive(anon): %d", &inactive_anon);
        sscanf(line, "Active(file): %d", &active_file);
        sscanf(line, "Inactive(file): %d", &inactive_file);
        // sscanf(line, "Mapped: %d", &mapped);
      }
      fclose(f);
    }
  }

  int heap0free = 0;
  int heap1free = 0;

  if (do_proctvbcmmeminfo) {
    f = fopen("/proc/tvbcm/meminfo", "r");
    if (f) {
      while (fgets(line, 256, f)) {
        if (heap0free == 0)
          sscanf(line, "free %d", &heap0free);
        else if (heap1free == 0)
          sscanf(line, "free %d", &heap1free);
      }
      fclose(f);
    }
  }

  struct cpu_t {
    int user;
    int nice;
    int system;
    int idle;
    int iowait;
    int irq;
    int softirq;
  };
  cpu_t cpu, cpu0, cpu1;

  if (do_procstat) {
    f = fopen("/proc/stat", "r");
    if (f) {
      if (fgets(line, 256, f))
        sscanf(line, "cpu %u %u %u %u %u %u %u", &cpu.user, &cpu.nice,
               &cpu.system, &cpu.idle, &cpu.iowait, &cpu.irq, &cpu.softirq);
      if (fgets(line, 256, f))
        sscanf(line, "cpu0 %u %u %u %u %u %u %u", &cpu0.user, &cpu0.nice,
               &cpu0.system, &cpu0.idle, &cpu0.iowait, &cpu0.irq,
               &cpu0.softirq);
      if (fgets(line, 256, f))
        sscanf(line, "cpu1 %u %u %u %u %u %u %u", &cpu1.user, &cpu1.nice,
               &cpu1.system, &cpu1.idle, &cpu1.iowait, &cpu1.irq,
               &cpu1.softirq);
      fclose(f);
    }
  }

  unsigned long vmsize = 0;
  unsigned long rss = 0;

  if (do_procselfstatm) {
    int fh = 0;
    char buffer[65];
    int gotten;
    fh = open("/proc/self/statm", O_RDONLY);
    gotten = read(fh, buffer, 64);
    buffer[gotten] = '\0';
    sscanf(buffer, "%lu %lu", &vmsize, &rss);
    close(fh);
  }

  int smapsswap = 0;

  if (do_procselfsmaps) {
    static char proc_self_smaps[16 * 1024 + 1];

    int fd;
    int bytes;

    fd = open("/proc/self/smaps", O_RDONLY);
    if (fd >= 0) {
      while (1) {
        bytes = read(fd, proc_self_smaps, sizeof(proc_self_smaps) - 1);
        if ((bytes == -1) && (errno == EINTR))
          /* keep trying */;
        else if (bytes > 0) {
          proc_self_smaps[bytes] = '\0';

          char *saveptr;
          char *line = strtok_r(proc_self_smaps, "\n", &saveptr);

          while (line) {
            int swap = 0;
            if (sscanf(line, "Swap: %d", &swap)) {
              smapsswap += swap;
            }
            line = strtok_r(NULL, "\n", &saveptr);
          }

        } else
          break;
      }
      close(fd);
    }
  }

  int vmswap = 0;
  if (do_procselfstatus) {
    int fd;
    int bytes;
    static char proc_self_status[16 * 1024 + 1];

    fd = open("/proc/self/status", O_RDONLY);
    if (fd >= 0) {
      while (1) {
        bytes = read(fd, proc_self_status, sizeof(proc_self_status) - 1);
        if ((bytes == -1) && (errno == EINTR))
          /* keep trying */;
        else if (bytes > 0) {
          proc_self_status[bytes] = '\0';
          char *saveptr;
          char *line = strtok_r(proc_self_status, "\n", &saveptr);
          while (line) {
            if (sscanf(line, "VmSwap: %d", &vmswap)) break;
            line = strtok_r(NULL, "\n", &saveptr);
          }
        } else
          break;
      }
    }
    close(fd);
  }

  struct diskstat_t {
    unsigned int reads;
    unsigned int reads_merged;
    unsigned int reads_sectors;
    unsigned int reads_time;
    unsigned int writes;
    unsigned int writes_merged;
    unsigned int writes_sectors;
    unsigned int writes_time;
  };

  diskstat_t sda2, sdb1;

  if (do_procdiskstats) {
    if ((f = fopen("/proc/diskstats", "r"))) {
      while (fgets(line, 256, f)) {
        char *pos;
        if ((pos = strstr(line, "sda2 "))) {
          sscanf(pos, "sda2 %u %u %u %u %u %u %u %u", &sda2.reads,
                 &sda2.reads_merged, &sda2.reads_sectors, &sda2.reads_time,
                 &sda2.writes, &sda2.writes_merged, &sda2.writes_sectors,
                 &sda2.writes_time);
        } else if ((pos = strstr(line, "sdb1 "))) {
          sscanf(pos, "sdb1 %u %u %u %u %u %u %u %u", &sdb1.reads,
                 &sdb1.reads_merged, &sdb1.reads_sectors, &sdb1.reads_time,
                 &sdb1.writes, &sdb1.writes_merged, &sdb1.writes_sectors,
                 &sdb1.writes_time);
        }
      }
      fclose(f);
    }
  }

  struct netdev_t {
    unsigned long r_bytes;
    unsigned int r_packets;
    unsigned int r_errs;
    unsigned int r_drop;
    unsigned int r_fifo;
    unsigned int r_frame;
    unsigned int r_compressed;
    unsigned int r_multicast;
    unsigned long t_bytes;
    unsigned int t_packets;
    unsigned int t_errs;
    unsigned int t_drop;
    unsigned int t_fifo;
    unsigned int t_frame;
    unsigned int t_compressed;
    unsigned int t_multicast;
  };

  netdev_t net1;

  if (do_procnetdev) {
    if ((f = fopen("/proc/net/dev", "r"))) {
      while (fgets(line, 256, f)) {
        char *pos;
        if ((pos = strstr(line, "enp0s3: "))) {
          sscanf(pos,
                 "enp0s3: %lu %u %u %u %u %u %u %u %lu %u %u %u %u %u %u %u",
                 &net1.r_bytes, &net1.r_packets, &net1.r_errs, &net1.r_drop,
                 &net1.r_fifo, &net1.r_frame, &net1.r_compressed,
                 &net1.r_multicast, &net1.t_bytes, &net1.t_packets,
                 &net1.t_errs, &net1.t_drop, &net1.t_fifo, &net1.t_frame,
                 &net1.t_compressed, &net1.t_multicast);
          break;
        }
      }
    }
    fclose(f);
  }

  if (do_sysinfo) {
    OUT(free, "FREE~%u", (unsigned int)((si.freeram / 1024) * si.mem_unit))
    OUT(buff, "BUFF~%u", (unsigned int)((si.bufferram / 1024) * si.mem_unit))
    OUT(cach, "CACH~%u", (unsigned int)cached)
    OUT(swap, "SWAP~%u",
        (unsigned int)(((si.totalswap - si.freeswap) / 1024) * si.mem_unit))

    OUT(pgpgin, "PGPGIN#%u", ((unsigned int)pgpgin))
    OUT(pgpgout, "PGPGOUT#%u", ((unsigned int)pgpgout))
    OUT(pswpin, "PSWPIN#%u", ((unsigned int)pswpin))
    OUT(pswpout, "PSWPOUT#%u", ((unsigned int)pswpout))

    OUT(pgfault, "PGFAULT#%u", (unsigned int)pgfault)
    OUT(pgfaultmaj, "PGMAJFAULT#%u", (unsigned int)pgmajfault)

    // tditrace("A_ANON~%u", (unsigned int)active_anon);
    // tditrace("I_ANON~%u", (unsigned int)inactive_anon);
    // tditrace("A_FILE~%u", (unsigned int)active_file);
    // tditrace("I_FILE~%u", (unsigned int)inactive_file);

    // tditrace("MAPPED~%d", (unsigned int)mapped);

    // tditrace("HEAP0FREE~%u", (unsigned int)(heap0free / 1024));
    // tditrace("HEAP1FREE~%u", (unsigned int)(heap0free / 1024));

    OUT(cpu0_user, "0_usr^%u", (cpu0.user + cpu0.nice))
    OUT(cpu0_system, "0_sys^%u", (cpu0.system))
    OUT(cpu0_io, "0_io^%u", (cpu0.iowait))
    OUT(cpu0_irq, "0_irq^%u", (cpu0.irq + cpu0.softirq))

    OUT(cpu1_user, "1_usr^%u", (cpu1.user + cpu1.nice))
    OUT(cpu1_system, "1_sys^%u", (cpu1.system))
    OUT(cpu1_io, "1_io^%u", (cpu1.iowait))
    OUT(cpu1_irq, "1_irq^%u", (cpu1.irq + cpu1.softirq))

    OUT(sda2_reads, "sda2_r#%u", (sda2.reads))
    OUT(sda2_writes, "sda2_w#%u", (sda2.writes))
    OUT(sdb1_reads, "sdb1_r#%u", (sdb1.reads))
    OUT(sdb1_writes, "sdb1_w#%u", (sdb1.writes))

    OUT(net1_r_packets, "net_r#%u", (net1.r_packets))
    OUT(net1_w_packets, "net_t#%u", (net1.t_packets))
  }

  if (do_selfinfo) {
    OUT(rss, ":RSS~%u", (rss * 4))
    OUT(brk, ":BRK~%u", (mi.arena / 1024))
    OUT(mmap, ":MMAP~%u", (mi.hblkhd / 1024))
    OUT(swap, ":SWAP~%u", smapsswap)

    // tditrace("VM~%u", (unsigned int)(vmsize * 4));
    // tditrace("MAXRSS~%u", (unsigned int)(ru.ru_maxrss / 1024));
    // tditrace("MINFLT~%u", (unsigned int)ru.ru_minflt);
    // tditrace("MAJFLT~%u", (unsigned int)ru.ru_majflt);
    // tditrace("_BI~%u", ((unsigned int)ru.ru_inblock - ru_bi_base));
    // tditrace("_BO~%u", ((unsigned int)ru.ru_oublock - ru_bo_base));
  }
}

static void *monitor_thread(void *param) {
  stat(gtracebufferfilename, &gtrace_buffer_st);

  if (do_maps) {
    sample_info();
    usleep(1 * 1000 * 1000);
    dump_proc_self_maps();
  }

  fprintf(stderr, "tdi: monitoring[%s][%d]\n", gprocname, gpid);
  while (1) {
    if (do_maps) {
      if (do_dump_proc_self_maps) {
        dump_proc_self_maps();
        do_dump_proc_self_maps = 0;
      }
    }

    sample_info();

    if (gtouch) {
      struct stat st;
      stat(gtracebufferfilename, &st);

      // fprintf(stderr, "tdi-check: %s atim=%d gatim=%d\n",
      // gtracebufferfilename,
      //        st.st_atim.tv_sec, gtrace_buffer_st.st_atim.tv_sec);
      // fprintf(stderr, "tdi-check: %s mtim=%d gmtim=%d\n",
      // gtracebufferfilename,
      //        st.st_mtim.tv_sec, gtrace_buffer_st.st_mtim.tv_sec);
      // fprintf(stderr, "tdi-check: %s ctim=%d gctim=%d\n",
      // gtracebufferfilename,
      //        st.st_ctim.tv_sec, gtrace_buffer_st.st_ctim.tv_sec);

      if (st.st_mtim.tv_sec != gtrace_buffer_st.st_mtim.tv_sec) {
        stat(gtracebufferfilename, &gtrace_buffer_st);

        //"rewind"   // 0x00000001
        //"shaders"  // 0x00000002
        //"textures" // 0x00000004
        //"frames"   // 0x00000008

        if (gtouch & 0x1) {
          fprintf(stderr, "tdi: rewinding...[%s][%d]\n", gprocname, gpid);
          tditrace_rewind();
          do_dump_proc_self_maps = 1;
        }

        if (gtouch & 0x2) {
          fprintf(stderr, "tdi: dump shaders...[%s][%d]\n", gprocname, gpid);
          if (shadercapture_writeshaders != NULL) {
            shadercapture_writeshaders();
          }
        }

        if (gtouch & 0x4) {
          fprintf(stderr, "tdi: dump textures...[%s][%d]\n", gprocname, gpid);

          if (texturecapture_writepngtextures != NULL) {
            texturecapture_writepngtextures();
          }
          if (texturecapture_deletetextures != NULL) {
            texturecapture_deletetextures();
          }
        }

        if (gtouch & 0x8) {
          fprintf(stderr, "tdi: dump frames[%s][%d]\n", gprocname, gpid);

          if (framecapture_writepngframes != NULL) {
            framecapture_writepngframes();
          }
          if (framecapture_deleteframes != NULL) {
            framecapture_deleteframes();
          }
        }
      }
    }

    if (do_offload) {
      static char offloadfilename[256];
      static char *offload_buffer;
      static FILE *offload_file;

      if (!offload_over50) {
        LOCK();
        int check = (((unsigned int)trace_buffer_byte_ptr -
                      (unsigned int)gtrace_buffer) > (gtracebuffersize / 2));
        UNLOCK();

        if (check) {
          offload_over50 = 1;
          fprintf(stderr, "tdi: at 50%%...[%d,%s]\n", gpid, gprocname);
          // create a new file and fill with 0..50% data

          offload_counter++;

          sprintf(offloadfilename, (char *)"%s/tditracebuffer@%s@%d@%04d",
                  offload_location, gprocname, gpid, offload_counter);

          if ((offload_file = fopen(offloadfilename, "w+")) == 0) {
            int errsv = errno;
            fprintf(stderr, "Error creating file \"%s\" [%d]\n",
                    offloadfilename, errsv);
          }

#ifndef __UCLIBC__
          if (posix_fallocate(fileno(offload_file), 0, gtracebuffersize) != 0) {
            fprintf(
                stderr,
                "tdi: [%d][%s], !!! failed to resize offloadfile: \"%s\" \n",
                gpid, gprocname, offloadfilename);
            tmpfs_message();

            fclose(offload_file);
            unlink(offloadfilename);

            pthread_exit(NULL);
          }
#else
          if (ftruncate(fileno(offload_file), gtracebuffersize) == -1) {
            fprintf(
                stderr,
                "tdi: [%d][%s], !!! failed to resize offloadfile: \"%s\" \n",
                gpid, gprocname, offloadfilename);
            pthread_exit(NULL);
            tmpfs_message();
          }
#endif

          offload_buffer = (char *)mmap(0, gtracebuffersize, PROT_WRITE,
                                        MAP_SHARED, fileno(offload_file), 0);

          if (offload_buffer == MAP_FAILED) {
            fprintf(stderr,
                    "tdi: [%d][%s], !!! failed to mmap offloadfile: \"%s\" \n",
                    gpid, gprocname, offloadfilename);
            pthread_exit(NULL);
          }

          fprintf(stderr, "tdi: [%d][%s], created offloadfile: \"%s\" \n", gpid,
                  gprocname, offloadfilename);

          memcpy(offload_buffer, gtrace_buffer, gtracebuffersize / 2);

          fprintf(stderr,
                  "tdi: [%d,%s], copied 0..50%% to "
                  "offloadfile: \"%s\"\n",
                  gpid, gprocname, offloadfilename);
        }

      } else {
        LOCK();
        int check = (((unsigned int)trace_buffer_byte_ptr -
                      (unsigned int)gtrace_buffer) < (gtracebuffersize / 2));
        UNLOCK();

        if (check) {
          offload_over50 = 0;
          fprintf(stderr, "tdi: [%d,%s], at 100%%...\n", gpid, gprocname);
          // fill remaining 50..100% data to existing file and close
          // file

          memcpy(offload_buffer + gtracebuffersize / 2,
                 gtrace_buffer + gtracebuffersize / 2, gtracebuffersize / 2);
          munmap(offload_buffer, gtracebuffersize);
          fclose(offload_file);

          fprintf(stderr,
                  "tdi: [%d,%s], copied 50..100%% to "
                  "offloadfile: \"%s\"\n",
                  gpid, gprocname, offloadfilename);
        }
      }
    }

    usleep(1000 * 1000 / do_persecond);
  }

  pthread_exit(NULL);
}

static int create_trace_buffer(void) {
  /*
   * [TDIT]
   * [RACE]
   * [    ]timeofday_start.tv_usec
   * [    ]timeofday_start.tv_sec
   * [    ]clock_monotonic_start.tv_nsec
   * [    ]clock_monotonic_start.tv_sec
   * ------
   * [    ]marker, lower 2 bytes is total length in dwords, upper 2bytes is nr
   * numbers
   * [    ]clock_monotonic_timestamp.tv_nsec
   * [    ]clock_monotonic_timestamp.tv_sec
   * [    ]text, padded with 0 to multiple of 4 bytes
   * ...
   * ------
   */
  sprintf(gtracebufferfilename, (char *)"/tmp/tditracebuffer@%s@%d", gprocname,
          gpid);
  FILE *file;
  if ((file = fopen(gtracebufferfilename, "w+")) == 0) {
    fprintf(stderr, "tdi: [%s][%d], !!! failed to create \"%s\"\n", gprocname,
            gpid, gtracebufferfilename);
    return -1;
  }

#ifndef __UCLIBC__
  if (posix_fallocate(fileno(file), 0, gtracebuffersize) != 0) {
    fprintf(stderr, "tdi: [%s][%d], !!! failed to resize \"%s\" (%dMB)\n",
            gprocname, gpid, gtracebufferfilename,
            gtracebuffersize / (1024 * 1024));
    tmpfs_message();

    fclose(file);
    unlink(gtracebufferfilename);

    return -1;
  }
#else
  if (ftruncate(fileno(file), gtracebuffersize) == -1) {
    fprintf(stderr, "tdi: [%s][%d], !!! failed to resize \"%s\" (%dMB)\n",
            gprocname, gpid, gtracebufferfilename,
            gtracebuffersize / (1024 * 1024));

    tmpfs_message();
    return -1;
  }
#endif

  gtrace_buffer = (char *)mmap(0, gtracebuffersize, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fileno(file), 0);

  if (gtrace_buffer == MAP_FAILED) {
    fprintf(stderr, "tdi: [%s][%d], !!! failed to mmap \"%s\" (%dMB)\n",
            gprocname, gpid, gtracebufferfilename,
            gtracebuffersize / (1024 * 1024));
    return -1;
  }

  stat(gtracebufferfilename, &gtrace_buffer_st);

  unsigned int i;
  for (i = 0; i < gtracebuffersize; i++) {
    gtrace_buffer[i] = 0;
  }

  fprintf(stderr, "tdi: init[%s][%d], allocated \"%s\" (%dMB)\n", gprocname,
          gpid, gtracebufferfilename, gtracebuffersize / (1024 * 1024));

  trace_buffer_byte_ptr = gtrace_buffer;
  trace_buffer_dword_ptr = (unsigned int *)gtrace_buffer;

  /*
   * write one time start text
   */
  sprintf((char *)trace_buffer_dword_ptr, (char *)"TDITRACE");
  trace_buffer_dword_ptr += 2;

  unsigned int *p = trace_buffer_dword_ptr;

  gettimeofday((struct timeval *)trace_buffer_dword_ptr, 0);
  trace_buffer_dword_ptr += 2;

  clock_gettime(CLOCK_MONOTONIC, (struct timespec *)trace_buffer_dword_ptr);
  trace_buffer_dword_ptr += 2;

  _u64 atimeofday_start = (_u64)*p++ * 1000000000;
  atimeofday_start += (_u64)*p++ * 1000;

  _u64 amonotonic_start = (_u64)*p++ * 1000000000;
  amonotonic_start += (_u64)*p++;

  /*
  fprintf(stderr,
          "tdi: [%s][%d], timeofday_start:%lld, "
          "monotonic_start:%lld\n",
          gprocname, gpid, atimeofday_start, amonotonic_start);
  */

  /*
   * rewind ptr is used for offloading set to after start timestamps
   */
  gtrace_buffer_rewind_ptr = trace_buffer_dword_ptr;
  *trace_buffer_dword_ptr = 0;

  reported_full = 0;

  LOCK();
  tditrace_inited = 1;
  UNLOCK();

  return 0;
}

static int thedelay;

static void start_monitor_thread(void);

static void *delayed_init_thread(void *param) {
  int *pdelay = (int *)param;
  int delay = *pdelay;

  fprintf(stderr, "tdi: init[%s][%d], delay is %d\n", gprocname, gpid, delay);

  if (delay == -1) {
    /*
     * wait for timeofday is 'today'
     */
    while (1) {
      struct timeval tv;
      struct tm *ptm;
      char time_string[40];
      gettimeofday(&tv, NULL);
      ptm = localtime(&tv.tv_sec);
      strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);

      if (tv.tv_sec > (45 * 365 * 24 * 3600)) {
        fprintf(stderr,
                "tdi: init[%s][%d], delay until timeofday is set, \"%s\", "
                "timeofday is set\n",
                gprocname, gpid, time_string);
        break;
      }

      ptm = localtime(&tv.tv_sec);
      strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);
      fprintf(stderr,
              "tdi: init[%s][%d], delay until timeofday is set, \"%s\", "
              "timeofday "
              "is not set\n",
              gprocname, gpid, time_string);

      usleep(1 * 1000000);
    }
  }

  else if (delay == -2) {
    /*
     * wait until tracebuffer modification time is changed.
     */

    while (1) {
      fprintf(stderr, "tdi: init[%s][%d], paused...\n", gprocname, gpid);
      usleep(1 * 1000000);

      struct stat st;
      stat(gtracebufferfilename, &st);

      /*
      printf("%s, %d %d\n", asctime(gmtime((const
      time_t*)&st.st_atim)), st.st_atim);
      printf("%s, %d %d\n", asctime(gmtime((const
      time_t*)&st.st_mtim)), st.st_mtim);
      printf("%s, %d %d\n", asctime(gmtime((const
      time_t*)&st.st_ctim)), st.st_ctim);
      */

      if (st.st_mtim.tv_sec != gtrace_buffer_st.st_mtim.tv_sec) {
        fprintf(stderr, "tdi: init[%s][%d], started...\n", gprocname, gpid);

        stat(gtracebufferfilename, &gtrace_buffer_st);
        break;
      }
    }

  } else {
    while (delay > 0) {
      fprintf(stderr, "tdi: init[%s][%d], delay %d second(s)...\n", gprocname,
              gpid, delay);
      usleep(1 * 1000 * 000);

      delay--;
    }
  }

  fprintf(stderr, "tdi: init[%s][%d], delay finished...\n", gprocname, gpid);

  if (create_trace_buffer() == -1) {
    pthread_exit(NULL);
  }

  start_monitor_thread();
  pthread_exit(NULL);
}

static const char *instruments[] = {"console",     // 0x00000001
                                    "render",      // 0x00000002
                                    "css",         // 0x00000004
                                    "dom",         // 0x00000008
                                    "canvas",      // 0x00000010
                                    "webgl",       // 0x00000020
                                    "image",       // 0x00000040
                                    "graphics",    // 0x00000080
                                    "graphicsqt",  // 0x00000100
                                    "texmap",      // 0x00000200
                                    "opengl",      // 0x00000400
                                    "qweb",        // 0x00000800
                                    "resource",    // 0x00001000
                                    "javascript",  // 0x00002000
                                    "allocator"};  // 0x00004000

static const char *touches[] = {"rewind",    // 0x00000001
                                "shaders",   // 0x00000002
                                "textures",  // 0x00000004
                                "frames"};   // 0x00000008

void start_monitor_thread(void) {
  static pthread_t monitor_thread_id;

  if (do_sysinfo | do_selfinfo | do_maps | gtouch) {
    pthread_create(&monitor_thread_id, NULL, monitor_thread, &monitor);
  }
}

int tditrace_init(void) {
  unsigned int i;

  if (tditrace_inited) {
    return 0;
  }

  gpid = getpid();
  get_process_name_by_pid(gpid, gprocname);

  if (strcmp(gprocname, "tdi") == 0) {
    if (!getenv("NOSKIPINIT")) return -1;
  }

  if (strcmp(gprocname, "mkdir") == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"mkdir\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "crypto", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"crypto\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "sh", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"sh*\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "bahsh", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"bash\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "ls", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"ls\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "genkey", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"genkey\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strncmp(gprocname, "rm", 2) == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"rm\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strcmp(gprocname, "iptables") == 0) {
    fprintf(stderr,
            "tdi: init[%s][%d], procname is \"iptables\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strcmp(gprocname, "route") == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"route\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strcmp(gprocname, "strace") == 0) {
    fprintf(stderr, "tdi: init[%s][%d], procname is \"strace\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else if (strcmp(gprocname, "gdbserver") == 0) {
    fprintf(stderr,
            "tdi: init[%s][%d], procname is \"gdbserver\" ; not tracing\n",
            gprocname, gpid);
    return -1;
  } else {
    // fprintf(stderr, "tdi: init[%s][%d]\n", gprocname, gpid);
  }

  char *env;

  if ((env = getenv("TRACEBUFFERSIZE"))) {
    gtracebuffersize = atoi(env) * 1024 * 1024;
  }

  if ((env = getenv(gprocname))) {
    gtracebuffersize = atoi(env) * 1024 * 1024;
  }

  gmask = 0x0;
  if ((env = getenv("MASK"))) {
    for (i = 0; i < sizeof(instruments) / sizeof(char *); i++) {
      if (strstr(env, instruments[i])) gmask |= (1 << i);
    }
    if (gmask == 0x0) gmask = strtoul(env, 0, 16);
  }

  if (gmask) {
    fprintf(stderr, "tdi: init[%s][%d], MASK = 0x%08x (", gprocname, gpid,
            gmask);
    int d = 0;
    for (i = 0; i < sizeof(instruments) / sizeof(char *); i++) {
      if (gmask & (1 << i)) {
        fprintf(stderr, "%s%s", d ? "+" : "", instruments[i]);
        d = 1;
      }
    }
    fprintf(stderr, ")\n");
  }

  gtouch = 0x0;
  if ((env = getenv("TOUCH"))) {
    for (i = 0; i < sizeof(touches) / sizeof(char *); i++) {
      if (strstr(env, touches[i])) gtouch |= (1 << i);
    }
    if (gtouch == 0x0) gtouch = strtoul(env, 0, 16);
  }
  if (gtouch) {
    fprintf(stderr, "tdi: init[%s][%d], TOUCH = 0x%08x (", gprocname, gpid,
            gtouch);
    int d = 0;
    for (i = 0; i < sizeof(touches) / sizeof(char *); i++) {
      if (gtouch & (1 << i)) {
        fprintf(stderr, "%s%s", d ? "+" : "", touches[i]);
        d = 1;
      }
    }
    fprintf(stderr, ")\n");
  }

  do_persecond = 1;

  do_sysinfo = 0;
  if ((env = getenv("SYSINFO"))) {
    do_sysinfo = atoi(env);
    if (do_sysinfo > do_persecond) do_persecond = do_sysinfo;
  }

  do_selfinfo = 0;
  if ((env = getenv("SELFINFO"))) {
    do_selfinfo = atoi(env);
    if (do_selfinfo > do_persecond) do_persecond = do_selfinfo;
  }

  do_offload = 0;
  if ((env = getenv("OFFLOAD"))) {
    do_offload = 1;
    strcpy(offload_location, env);
  }

  do_wrap = 0;
  if ((env = getenv("WRAP"))) {
    do_wrap = (atoi(env) >= 1);
  }

  do_maps = 0;
  if ((env = getenv("MAPS"))) {
    do_maps = (atoi(env) >= 1);
  }

  report_tid = 0;
  if ((env = getenv("TID"))) {
    report_tid = (atoi(env) >= 1);
  }

  thedelay = 0;
  if ((env = getenv("DELAY"))) {
    thedelay = atoi(env);
  }

  /*
   * remove inactive tracefiles
   */

  int remove = 1;
  if ((env = getenv("REMOVE"))) {
    remove = (atoi(env) >= 1);
  }

  if (remove) {
    DIR *dp;
    struct dirent *ep;

    dp = opendir("/tmp/");
    if (dp != NULL) {
      while ((ep = readdir(dp))) {
        if (strncmp(ep->d_name, "tditracebuffer@", 15) == 0) {
          char procpid[128];
          sprintf(procpid, (char *)"/proc/%d",
                  atoi(strrchr(ep->d_name, '@') + 1));

          char fullname[128];
          sprintf(fullname, "/tmp/%s", ep->d_name);

          struct stat sts;

          if (stat(procpid, &sts) == -1) {
            unlink(fullname);
            fprintf(stderr, "tdi: init[%s][%d], removed \"%s\"\n", gprocname,
                    gpid, fullname);
          } else {
            fprintf(stderr, "tdi: init[%s][%d], not removed \"%s\"\n",
                    gprocname, gpid, fullname);
          }
        }
      }

      closedir(dp);
    }
  }

  LOCK_init();

  if (thedelay == 0) {
    if (create_trace_buffer() == -1) {
      return -1;
    }

    start_monitor_thread();
    return 0;
  }

  pthread_t delayed_init_thread_id;
  pthread_create(&delayed_init_thread_id, NULL, delayed_init_thread, &thedelay);

  if (thedelay == -1) {
    pthread_join(delayed_init_thread_id, NULL);
  }

  return 0;
}

static void tditrace_rewind() {
  LOCK();
  tditrace_inited = 0;
  UNLOCK();

  trace_buffer_byte_ptr = gtrace_buffer;
  trace_buffer_dword_ptr = (unsigned int *)gtrace_buffer;

  /*
   * write one time start text
   */
  sprintf((char *)trace_buffer_dword_ptr, (char *)"TDITRACE");
  trace_buffer_dword_ptr += 2;

  gettimeofday((struct timeval *)trace_buffer_dword_ptr, 0);
  trace_buffer_dword_ptr += 2;

  clock_gettime(CLOCK_MONOTONIC, (struct timespec *)trace_buffer_dword_ptr);
  trace_buffer_dword_ptr += 2;

  gtrace_buffer_rewind_ptr = trace_buffer_dword_ptr;
  *trace_buffer_dword_ptr = 0;

  reported_full = 0;

  LOCK();
  tditrace_inited = 1;
  UNLOCK();
}

static void check_trace_buffer(int b) {
  FILE *file;

  if ((file = fopen(tracebuffers[b].filename, "r")) != NULL) {
    /* /tmp/tditracebuffer@xxx@xxx */

    struct stat st;
    stat(tracebuffers[b].filename, &st);

    fprintf(stderr, "\"%s\" (%lluMB) ...\n", tracebuffers[b].filename,
            (unsigned long long)st.st_size / (1024 * 1024));

    char *s1 = strchr(tracebuffers[b].filename, '@');
    char *s2 = strchr(s1 + 1, '@');

    strncpy(tracebuffers[b].procname, s1 + 1, s2 - s1);
    tracebuffers[b].procname[(s2 - s1) - 1] = 0;
    tracebuffers[b].pid = atoi(s2 + 1);

    if (st.st_size == 0) {
      fprintf(stderr, "empty tracebuffer, skipping\n");
      return;
    }

    tracebuffers[b].bufmmapped = (char *)mmap(
        0, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(file), 0);
    tracebuffers[b].bufsize = st.st_size;
    tracebuffers[b].ptr = tracebuffers[b].bufmmapped;
    tracebuffers[b].dword_ptr = (unsigned int *)tracebuffers[b].bufmmapped;

    // should hold "TDITRACE"
    if (strncmp("TDITRACE", tracebuffers[b].ptr, 8) != 0) {
      fprintf(stderr, "invalid tracebuffer, skipping\n");
      return;
    }

    tracebuffers[b].dword_ptr += 2;
    unsigned int *p = tracebuffers[b].dword_ptr;

    tracebuffers[b].timeofday_start = (_u64)*p++ * (_u64)1000000000;
    tracebuffers[b].timeofday_start += (_u64)*p++ * (_u64)1000;

    tracebuffers[b].monotonic_start = (_u64)*p++ * (_u64)1000000000;
    tracebuffers[b].monotonic_start += (_u64)*p++;

    /*
    fprintf(stderr,
            "\"%s\" "
            "timeofday_start:%lld, "
            "monotonic_start:%lld\n",
            tracebuffers[b].filename, tracebuffers[b].timeofday_start,
            tracebuffers[b].monotonic_start);
    */
    tracebuffers[b].dword_ptr += 4;

    fclose(file);
  }
}

void tditrace_exit(int argc, char *argv[]) {
  int i, nr_entries;
  int buffers = 0;

  if (argc > 1) {
    int tracebufferid = argc;

    while (--tracebufferid) {
      if (strstr(argv[tracebufferid], "tditracebuffer@")) {
        sprintf(tracebuffers[buffers].filename, "%s", argv[tracebufferid]);
        check_trace_buffer(buffers);
        buffers++;
      }
    }
  }

  else {
    DIR *dp;
    struct dirent *ep;

    dp = opendir("/tmp/");
    if (dp != NULL) {
      while ((ep = readdir(dp))) {
        if (strncmp(ep->d_name, "tditracebuffer@", 15) == 0) {
          sprintf(tracebuffers[buffers].filename, "/tmp/%s", ep->d_name);
          check_trace_buffer(buffers);
          buffers++;
        }
      }
    }
    closedir(dp);
  }

  if (buffers == 0) {
    fprintf(stderr, "Not found: \"/tmp/tditracebuffer@*@*\"\n");
    return;
  }

  _u64 last_timestamp = 0;

  for (i = 0; i < buffers; i++) {
    parse(i);
    if (tracebuffers[i].valid) {
      tracebuffers[i].monotonic_first = tracebuffers[i].monotonic_timestamp;
    }
  }

  fprintf(stdout, "TIME %d\n", 1000000000);
  fprintf(stdout, "SPEED %d\n", 1000000000);
  fprintf(stdout, "MEMSPEED %d\n", 1000000000);

  /*
   * play out all entries from all buffers in order of monotonic timestamp
   *
   */
  nr_entries = 0;
  while (1) {
    int pctused;
    int bytesused;
    int d = -1;

    for (i = 0; i < buffers; i++) {
      if (tracebuffers[i].valid) {
        d = i;
      }
    }

    /*
     * if no more valid entries in all buffers
     * then be done
     */
    if (d == -1) {
      break;
    }

    /*
     * select the entry with the lowest monotonic timestamp
     */
    for (i = 0; i < buffers; i++) {
      if (tracebuffers[i].valid) {
        if (tracebuffers[i].monotonic_timestamp <
            tracebuffers[d].monotonic_timestamp) {
          d = i;
        }
      }
    }

    addentry(stdout, tracebuffers[d].text, tracebuffers[d].text_len,
             tracebuffers[d].monotonic_timestamp, tracebuffers[d].procname,
             tracebuffers[d].pid, tracebuffers[d].tid,
             tracebuffers[d].nr_numbers, tracebuffers[d].numbers,
             tracebuffers[d].identifier);

    pctused = (((tracebuffers[d].text - tracebuffers[d].bufmmapped) * 100.0) /
               tracebuffers[d].bufsize) +
              1;
    bytesused = tracebuffers[d].text - tracebuffers[d].bufmmapped;

    nr_entries++;

    last_timestamp = tracebuffers[d].monotonic_timestamp;
    parse(d);

    if (!tracebuffers[d].valid) {
      fprintf(stderr, "\"%s\" %d%% (#%d,%dB)\n", tracebuffers[d].filename,
              pctused, nr_entries, bytesused);
    }
  }

  // Add one more entry 0.1 sec behind all the previous ones
  addentry(stdout, "TDITRACE_EXIT", strlen("TDITRACE_EXIT"),
           last_timestamp + 100 * 1000000, "", 0, 0, 0, 0, 0);

  struct timespec atime;

  for (i = 0; i < buffers; i++) {
    atime.tv_sec = tracebuffers[i].timeofday_start / 1000000000;
    atime.tv_nsec = tracebuffers[i].timeofday_start - atime.tv_sec * 1000000000;
    fprintf(stdout, "END %d %lld %lld %lld %s", i,
            tracebuffers[i].timeofday_start, tracebuffers[i].monotonic_start,
            tracebuffers[i].monotonic_first,
            asctime(gmtime((const time_t *)&atime)));
  }
}

static void tditrace_internal(va_list args, const char *format);

void tditrace(const char *format, ...) {
  va_list args;

  va_start(args, format);

  tditrace_internal(args, format);

  va_end(args);
}

void tditrace_ex(int mask, const char *format, ...) {
  va_list args;

  if (mask & gmask) {
    va_start(args, format);

    tditrace_internal(args, format);

    va_end(args);
  }
}

/*
 * [TDIT]
 * [RACE]
 * [    ]timeofday_start.tv_usec
 * [    ]timeofday_start.tv_sec
 * [    ]clock_monotonic_start.tv_nsec
 * [    ]clock_monotonic_start.tv_sec
 * ------
 * [    ]marker, lower 2 bytes is total length in dwords, upper byte is
 * identifier,
 *       middle byte is nr numbers
 * [    ]clock_monotonic_timestamp.tv_nsec
 * [    ]clock_monotonic_timestamp.tv_sec
 * [    ]<optional> numbers
 * [    ]<optional> text, padded with 0's to multiple of 4 bytes
 * ...
 * ------
 */

void tditrace_internal(va_list args, const char *format) {
  if (!tditrace_inited) {
    return;
  }

  unsigned int trace_text[1024 / 4];
  unsigned int numbers[8];
  unsigned int nr_numbers = 0;
  unsigned short identifier = 0;
  unsigned int i;

  /*
   * take and store timestamp
   */
  struct timespec mytime;
  clock_gettime(CLOCK_MONOTONIC, &mytime);

  /*
   * parse the format string
   * %0 %1 %2 pull in integers
   */
  char *trace_text_ptr = (char *)trace_text;
  unsigned int *trace_text_dword_ptr = (unsigned int *)trace_text;
  char ch;

  while ((ch = *(format++))) {
    if (ch == '%') {
      switch (ch = (*format++)) {
        case 's': {
          char *s;
          s = va_arg(args, char *);
          if (s) {
            int i = 0;
            while (*s) {
              *trace_text_ptr++ = *s++;
              i++;
              if (i > 256) break;
            }
          } else {
            *trace_text_ptr++ = 'n';
            *trace_text_ptr++ = 'i';
            *trace_text_ptr++ = 'l';
            *trace_text_ptr++ = 'l';
          }
          break;
        }
        case 'd': {
          int n = 0;
          unsigned int d = 1;
          int num = va_arg(args, int);
          if (num < 0) {
            num = -num;
            *trace_text_ptr++ = '-';
          }

          while (num / d >= 10) d *= 10;

          while (d != 0) {
            int digit = num / d;
            num %= d;
            d /= 10;
            if (n || digit > 0 || d == 0) {
              *trace_text_ptr++ = digit + '0';
              n++;
            }
          }
          break;
        }
        case 'u': {
          int n = 0;
          unsigned int d = 1;
          unsigned int num = va_arg(args, int);

          while (num / d >= 10) d *= 10;

          while (d != 0) {
            int digit = num / d;
            num %= d;
            d /= 10;
            if (n || digit > 0 || d == 0) {
              *trace_text_ptr++ = digit + '0';
              n++;
            }
          }
          break;
        }

        case 'x':
        case 'p': {
          int n = 0;
          unsigned int d = 1;
          unsigned int num = va_arg(args, int);

          while (num / d >= 16) d *= 16;

          while (d != 0) {
            int dgt = num / d;
            num %= d;
            d /= 16;
            if (n || dgt > 0 || d == 0) {
              *trace_text_ptr++ = dgt + (dgt < 10 ? '0' : 'a' - 10);
              ++n;
            }
          }
          break;
        }

        case 'n': {
          numbers[nr_numbers] = va_arg(args, int);
          nr_numbers++;
          break;
        }

        case 'm': {
          identifier = va_arg(args, int) & 0xff;
          break;
        }

        default:
          break;
      }

    } else {
      *trace_text_ptr++ = ch;
    }
  }

  while ((unsigned int)trace_text_ptr & 0x3) *trace_text_ptr++ = 0;

  int nr_textdwords = (trace_text_ptr - (char *)trace_text) >> 2;

  /*
   * store into tracebuffer
   */
  LOCK();

  /*
   * marker, 4 bytes
   *       bytes 1+0 hold total length in dwords : 3 (marker,sec,nsec) +
   *                                           nr_numbers + nr_dwordtext
   *       byte    2 hold nr_numbers
   *       byte    3 hold 0..f
   */

  *trace_buffer_dword_ptr++ = (0x0003 + nr_numbers + nr_textdwords) |
                              ((nr_numbers & 0xff) << 16) |
                              ((identifier & 0xff) << 24);
  *trace_buffer_dword_ptr++ = mytime.tv_sec;
  *trace_buffer_dword_ptr++ = mytime.tv_nsec;

  i = 0;
  while (i != nr_numbers) {
    *trace_buffer_dword_ptr++ = numbers[i];
    i++;
  }

  i = nr_textdwords;
  while (i--) {
    *trace_buffer_dword_ptr++ = *trace_text_dword_ptr++;
  }
  /*
   * mark the next marker as invalid
   */
  *trace_buffer_dword_ptr = 0;

  if (((unsigned int)trace_buffer_dword_ptr - (unsigned int)gtrace_buffer) >
      (gtracebuffersize - 1024)) {
    if (do_offload || do_wrap) {
      // clear unused and rewind to rewind ptr
      fprintf(stderr, "tdi: rewind[%d,%s]\n", gpid, gprocname);
      unsigned int i;
      for (i = (unsigned int)trace_buffer_dword_ptr -
               (unsigned int)gtrace_buffer;
           i < gtracebuffersize; i++) {
        gtrace_buffer[i] = 0;
      }
      trace_buffer_dword_ptr = gtrace_buffer_rewind_ptr;
      if (!do_offload) do_dump_proc_self_maps = 1;
    } else {
      fprintf(stderr, "tdi: full[%s][%d]\n", gprocname, gpid);
      tditrace_inited = 0;
    }
  }

  UNLOCK();
}

}  // extern "C"

static void __attribute__((constructor)) tditracer_constructor();
static void __attribute__((destructor)) tditracer_destructor();

static void tditracer_constructor() {
  if (tditrace_init() == -1) {
    return;
  }
}

static void tditracer_destructor() {
  fprintf(stderr, "tdi: exit[%d]\n", getpid());
}
