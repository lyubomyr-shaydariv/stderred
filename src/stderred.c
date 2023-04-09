#include "config.h"
#include "polyfill.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <regex.h>

#ifdef __APPLE__
  #define FUNC(name) _##name
  #define ORIGINAL(name) name
  #define GET_ORIGINAL(...)
#else
  #include <dlfcn.h>
  #define FUNC(name) (name)
  #define ORIGINAL(name) original_##name
  #define GET_ORIGINAL(ret, name, ...) \
    static ret (*ORIGINAL(name))(__VA_ARGS__) = NULL; \
    if (!ORIGINAL(name)) { \
      *(void **)(&ORIGINAL(name)) = dlsym(RTLD_NEXT, #name); \
    } \
    if (!ORIGINAL(name)) { \
      errno = ENOSYS; \
      abort(); \
    }
#endif


const struct fdattr_t {
  bool is_valid_env;
  char *begin;
  size_t begin_length;
  char *end;
  size_t end_length;
} fdattr_t_DEFAULT = {
  is_valid_env: false,
  begin: NULL,
  begin_length: 0,
  end: NULL,
  end_length: 0,
};

struct fdattr_t fdattr_slots[] = {
  fdattr_t_DEFAULT, // 0 STDIN_FILENO
  fdattr_t_DEFAULT, // 1 STDOUT_FILENO
  fdattr_t_DEFAULT, // 2 STDERR_FILENO
  fdattr_t_DEFAULT, // 3
  fdattr_t_DEFAULT, // 4
  fdattr_t_DEFAULT, // 5
  fdattr_t_DEFAULT, // 6
  fdattr_t_DEFAULT, // 7
  fdattr_t_DEFAULT, // 8
  fdattr_t_DEFAULT, // 9
};

#define FDATTR_SLOTS_LENGTH (sizeof(fdattr_slots) / sizeof(0[fdattr_slots]))
_Static_assert(FDATTR_SLOTS_LENGTH >= STDIN_FILENO, "must support stdin");
_Static_assert(FDATTR_SLOTS_LENGTH >= STDOUT_FILENO, "must support stdout");
_Static_assert(FDATTR_SLOTS_LENGTH >= STDERR_FILENO, "must support stderr");

#define COLORIZE(fd) (fd < FDATTR_SLOTS_LENGTH && fdattr_slots[fd].is_valid_env)

__attribute__((constructor, visibility ("hidden"))) void init() {
  if (!strcmp("bash", PROGRAM_NAME)) return;

  char *blacklist;
  if ((blacklist = getenv("STDERRED_BLACKLIST"))) {
    regex_t regex;
    if (regcomp(&regex, blacklist, REG_EXTENDED | REG_NOSUB)) return;
    if (!regexec(&regex, PROGRAM_NAME, 0, NULL, 0)) {
      regfree(&regex);
      return;
    }
    regfree(&regex);
  }

  {
    for (size_t fd = 0; fd < FDATTR_SLOTS_LENGTH; fd++) {
      fdattr_slots[fd].is_valid_env = false;
      if (!isatty(fd)) {
        continue;
      }
      char *begin = NULL;
      if (fd < 1024) { // TODO number of files currently reported by `uname -a`
        static char env_var[50];
        snprintf(env_var, (sizeof(env_var) / sizeof(0[env_var])) - 1, "STDERRED_ESC_CODE_FD%ld", fd);
        begin = getenv(env_var);
      }
      if (begin == NULL) {
        switch (fd) {
        case STDOUT_FILENO:
          begin = getenv("STDERRED_ESC_CODE_STDOUT");
          break;
        case STDERR_FILENO:
          begin = getenv("STDERRED_ESC_CODE_STDERR");
          if (begin == NULL) {
            begin = getenv("STDERRED_ESC_CODE");
          }
          break;
        }
      }
      if (begin == NULL) {
        switch (fd) {
        case STDOUT_FILENO:
          begin = "\x1b[32m";
          break;
        case STDERR_FILENO:
          begin = "\x1b[31m";
          break;
        }
      }
      if (begin == NULL) {
        continue;
      }
      fdattr_slots[fd].is_valid_env = true;
      fdattr_slots[fd].begin = begin;
      fdattr_slots[fd].begin_length = strlen(fdattr_slots[fd].begin);
      fdattr_slots[fd].end = "\x1b[0m";
      fdattr_slots[fd].end_length = strlen(fdattr_slots[fd].end);
    }
  }
}

ssize_t FUNC(write)(int fd, const void* buf, size_t count) {
  if (!count) return 0;

  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);

  if (COLORIZE(fd)) {
    ssize_t written = ORIGINAL(write)(fd, fdattr_slots[fd].begin, fdattr_slots[fd].begin_length);
    if (written <= 0) return written;
    if (written < fdattr_slots[fd].begin_length) {
      ORIGINAL(write)(fd, fdattr_slots[fd].end, fdattr_slots[fd].end_length);
      return 0;
    }
  }

  ssize_t written = ORIGINAL(write)(fd, buf, count);

  if (written > 0 && COLORIZE(fd)) {
    ORIGINAL(write)(fd, fdattr_slots[fd].end, fdattr_slots[fd].end_length);
  }

  return written;
}

size_t FUNC(fwrite_unlocked)(const void *data, size_t size, size_t count, FILE *stream) {
  if (size * count == 0) return 0;

  size_t result;
  int fd = fileno_unlocked(stream);

  GET_ORIGINAL(ssize_t, fwrite_unlocked, const void*, size_t, size_t, FILE *);

  if (COLORIZE(fd)) {
    result = ORIGINAL(fwrite_unlocked)(fdattr_slots[fd].begin, sizeof(char), fdattr_slots[fd].begin_length, stream);
    if ((ssize_t)result < 0) return result;
  }

  result = ORIGINAL(fwrite_unlocked)(data, size, count, stream);
  if (result > 0 && COLORIZE(fd)) {
    ORIGINAL(fwrite_unlocked)(fdattr_slots[fd].end, sizeof(char), fdattr_slots[fd].end_length, stream);
  }

  return result;
}

size_t FUNC(fwrite)(const void *data, size_t size, size_t count, FILE *stream) {
  if (size * count == 0) return 0;

  size_t result;
  int fd = fileno(stream);

  GET_ORIGINAL(ssize_t, fwrite, const void*, size_t, size_t, FILE *);

  if (COLORIZE(fd)) {
    result = ORIGINAL(fwrite)(fdattr_slots[fd].begin, sizeof(char), fdattr_slots[fd].begin_length, stream);
    if ((ssize_t)result < 0) return result;
  }

  result = ORIGINAL(fwrite)(data, size, count, stream);
  if (result > 0 && COLORIZE(fd)) {
    ORIGINAL(fwrite)(fdattr_slots[fd].end, sizeof(char), fdattr_slots[fd].end_length, stream);
  }

  return result;
}

int FUNC(fputc)(int chr, FILE *stream) {
  size_t result;
  int fd = fileno(stream);

  GET_ORIGINAL(int, fputc, int, FILE *);
  GET_ORIGINAL(ssize_t, fwrite, const void*, size_t, size_t, FILE *);

  if (COLORIZE(fd)) {
    result = ORIGINAL(fwrite)(fdattr_slots[fd].begin, sizeof(char), fdattr_slots[fd].begin_length, stream);
    if ((ssize_t)result < 0) return result;
  }

  result = ORIGINAL(fputc)(chr, stream);
  if (COLORIZE(fd)) {
    ORIGINAL(fwrite)(fdattr_slots[fd].end, sizeof(char), fdattr_slots[fd].end_length, stream);
  }

  return result;
}

int FUNC(fputc_unlocked)(int chr, FILE *stream) {
  size_t result;
  int fd = fileno(stream);

  GET_ORIGINAL(int, fputc_unlocked, int, FILE *);
  GET_ORIGINAL(ssize_t, fwrite, const void*, size_t, size_t, FILE *);

  if (COLORIZE(fd)) {
    result = ORIGINAL(fwrite)(fdattr_slots[fd].begin, sizeof(char), fdattr_slots[fd].begin_length, stream);
    if ((ssize_t)result < 0) return result;
  }

  result = ORIGINAL(fputc_unlocked)(chr, stream);
  if (COLORIZE(fd)) {
    ORIGINAL(fwrite)(fdattr_slots[fd].end, sizeof(char), fdattr_slots[fd].end_length, stream);
  }

  return result;
}

int FUNC(fputs)(const char *str, FILE *stream) {
  return FUNC(fwrite)(str, sizeof(char), strlen(str)/sizeof(char), stream);
}

int FUNC(fputs_unlocked)(const char *str, FILE *stream) {
  return FUNC(fwrite_unlocked)(str, sizeof(char), strlen(str)/sizeof(char), stream);
}

int FUNC(vfprintf)(FILE *stream, const char *format, va_list ap) {
  char *buf = NULL;

  int nprinted = vasprintf(&buf, format, ap);
  if (nprinted > 0) {
    int result = FUNC(fwrite)(buf, sizeof(char), nprinted, stream);
    free(buf);
    return result;
  } else {
    return -1;
  }
}

int FUNC(fprintf)(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = FUNC(vfprintf)(stream, format, args);
  va_end(args);
  return result;
}

#ifdef HAVE__FPRINTF_CHK
int FUNC(__fprintf_chk)(FILE *fp, int flag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = FUNC(vfprintf)(fp, format, args);
  va_end(args);
  return result;
}
#endif

int FUNC(fprintf_unlocked)(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *buf = NULL;
  int result = -1;

  int nprinted = vasprintf(&buf, format, args);
  if (nprinted > 0) {
    result = FUNC(fwrite_unlocked)(buf, sizeof(char), nprinted, stream);
    free(buf);
  }

  va_end(args);
  return result;
}

void FUNC(perror)(const char *msg) {
  if (msg == NULL) {
    FUNC(fprintf)(stderr, "%s\n", strerror(errno));
  } else {
    FUNC(fprintf)(stderr, "%s: %s\n", msg, strerror(errno));
  }
}

void FUNC(error)(int status, int errnum, const char *format, ...) {
  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);
  GET_ORIGINAL(void, error, int, int, const char *, ...);

  fflush(stdout);

  if (COLORIZE(STDERR_FILENO))
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);

  char *buf;
  va_list args;
  va_start(args, format);
  if (vasprintf(&buf, format, args) > 0) {
    ORIGINAL(error)(0, errnum, "%s", buf);
    free(buf);
  }
  va_end(args);

  if (COLORIZE(STDERR_FILENO))
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);

  if (status) exit(status);
}

void FUNC(error_at_line)(int status, int errnum, const char *filename, unsigned int linenum, const char *format, ...) {
  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);
  GET_ORIGINAL(void, error_at_line, int, int, const char *, unsigned int, const char *, ...);

  fflush(stdout);

  if (COLORIZE(STDERR_FILENO))
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);

  char *buf;
  va_list args;
  va_start(args, format);
  if (vasprintf(&buf, format, args) > 0) {
    ORIGINAL(error_at_line)(0, errnum, filename, linenum, "%s", buf);
    free(buf);
  }
  va_end(args);

  if (COLORIZE(STDERR_FILENO))
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);

  if (status) exit(status);
}

int colorize_err_funcs = true;
void FUNC(err_set_file)(void *fp) {
  GET_ORIGINAL(void, err_set_file, void *);
  ORIGINAL(err_set_file)(fp);
  colorize_err_funcs = (fp == NULL && COLORIZE(STDERR_FILENO)) || COLORIZE(fileno((FILE *)fp));
}

void FUNC(vwarn)(const char *fmt, va_list args) {
  GET_ORIGINAL(void, vwarn, const char *, va_list);
  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);

  ORIGINAL(vwarn)(fmt, args);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);
}


void FUNC(vwarnx)(const char *fmt, va_list args) {
  GET_ORIGINAL(void, vwarnx, const char *, va_list);
  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);

  ORIGINAL(vwarnx)(fmt, args);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);
}

void FUNC(vwarnc)(int code, const char *fmt, va_list args) {
  GET_ORIGINAL(void, vwarnc, int, const char *, va_list);
  GET_ORIGINAL(ssize_t, write, int, const void *, size_t);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);

  ORIGINAL(vwarnc)(code, fmt, args);

  if (colorize_err_funcs)
    ORIGINAL(write)(STDERR_FILENO, fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);
}

void FUNC(verr)(int eval, const char *fmt, va_list args) {
  FUNC(vwarn)(fmt, args);
  exit(eval);
}

void FUNC(verrc)(int eval, int code, const char *fmt, va_list args) {
  FUNC(vwarnc)(code, fmt, args);
  exit(eval);
}

void FUNC(err)(int eval, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(verr)(eval, fmt, ap);
  va_end(ap);
  exit(eval); // Added to keep gcc from complaining - never reached
}

void FUNC(errc)(int eval, int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(verrc)(eval, code, fmt, ap);
  va_end(ap);
  exit(eval); // Added to keep gcc from complaining - never reached
}

void FUNC(verrx)(int eval, const char *fmt, va_list args) {
  FUNC(vwarnx)(fmt, args);
  exit(eval);
}

void FUNC(errx)(int eval, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(verrx)(eval, fmt, ap);
  va_end(ap);
  exit(eval); // Added to keep gcc from complaining - never reached
}

void FUNC(warn)(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(vwarn)(fmt, ap);
  va_end(ap);
}

void FUNC(warnc)(int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(vwarnc)(code, fmt, ap);
  va_end(ap);
}

void FUNC(warnx)(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FUNC(vwarnx)(fmt, ap);
  va_end(ap);
}

#ifdef HAVE__WRITE_NOCANCEL
ssize_t __write_nocancel(int fd, const void * cbuf, size_t nbyte);

ssize_t FUNC(__write_nocancel)(int fd, const void * cbuf, size_t nbyte) {
  if (nbyte == 0) return 0;

  ssize_t result;

  GET_ORIGINAL(ssize_t, __write_nocancel, int, const void *, size_t);

  if (COLORIZE(fd)) {
    result = ORIGINAL(__write_nocancel)(fd, (const void *)fdattr_slots[STDERR_FILENO].begin, fdattr_slots[STDERR_FILENO].begin_length);
    if (result < 0) return result;
  }

  result = ORIGINAL(__write_nocancel)(fd, cbuf, nbyte);
  if (result > 0 && COLORIZE(fd)) {
    ORIGINAL(__write_nocancel)(fd, (const void *)fdattr_slots[STDERR_FILENO].end, fdattr_slots[STDERR_FILENO].end_length);
  }

  return result;
}
#endif

#ifdef __APPLE__
  #define INTERPOSE(name) { (void *)FUNC(name), (void *)name }
  typedef struct { void *new; void *old; } interpose;
  __attribute__((used)) static const interpose interposers[] \
    __attribute__((section("__DATA,__interpose"))) = {
      INTERPOSE(write),
  #ifdef HAVE__WRITE_NOCANCEL
      INTERPOSE(__write_nocancel),
  #endif
      INTERPOSE(fwrite),
      INTERPOSE(fwrite_unlocked),
      INTERPOSE(fputc),
      INTERPOSE(fputc_unlocked),
      INTERPOSE(fputs),
      INTERPOSE(fputs_unlocked),
      INTERPOSE(fprintf),
      INTERPOSE(fprintf_unlocked),
      INTERPOSE(vfprintf),
      INTERPOSE(perror),
      INTERPOSE(error),
      INTERPOSE(error_at_line),
      INTERPOSE(err),
      INTERPOSE(verr),
      INTERPOSE(errc),
      INTERPOSE(verrc),
      INTERPOSE(errx),
      INTERPOSE(verrx),
      INTERPOSE(warn),
      INTERPOSE(vwarn),
      INTERPOSE(warnc),
      INTERPOSE(vwarnc),
      INTERPOSE(warnx),
      INTERPOSE(vwarnx),
      INTERPOSE(err_set_file),
  };
#endif
