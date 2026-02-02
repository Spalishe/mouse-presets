#include "main.h"
#include "defs.h"
#include "render.h"
#include "wayland.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int open_event_by_name(char *name) {
  char path[1024];
  char full_path[1024];
  bool found = false;
  DIR *dir = opendir("/dev/input/by-id/");
  struct dirent *hFile;
  if (dir) {
    while ((hFile = readdir(dir)) != NULL) {
      if (strcmp(hFile->d_name, name))
        continue;

      found = true;
      snprintf(full_path, sizeof(full_path), "/dev/input/by-id/%s",
               hFile->d_name);

      ssize_t len = readlink(full_path, path, sizeof(path) - 1);
      path[len] = '\0';
    }
    closedir(dir);
  } else
    return 1;

  if (!found) {
    return -1;
  }
  char new_path[1024];
  snprintf(new_path, sizeof(new_path), "/dev/input/by-id/%s", path);

  int fd = open(new_path, O_RDWR);
  return fd;
}

void init_devices() {
  for (int i = 0; i < sizeof(devices) / sizeof(struct device); i++) {
    struct device *dev = &devices[i];
    int fd = open_event_by_name(dev->dev);
    dev->fd = fd;
  }
}

struct device *get_device_by_id(uint8_t dev_id) {
  for (int i = 0; i < sizeof(devices) / sizeof(struct device); i++) {
    struct device *dev = &devices[i];
    if (dev_id == dev->dev_id)
      return dev;
  }
  return NULL;
}

void *keys(void *arg) {
  struct input_event ev;
  int fd = *(int *)arg;
  while (true) {
    int64_t s = read(fd, &ev, sizeof(ev));
    if (s > 0) {
      if (ev.type != EV_KEY) {
        continue;
      }
      if (ev.value == 1) {
        struct profile pr = presets[profile_ind];
        for (int i = 0; i < (sizeof(pr.binds) / sizeof(struct key_bind)); i++) {
          struct key_bind bind = pr.binds[i];
          if (bind.key == ev.code) {
            if (bind.action == Default) {

              for (int i = 0; i < (sizeof(bind.buttons) / sizeof(struct bind));
                   i++) {
                struct bind *b = &bind.buttons[i];
                struct input_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.code = b->code;
                ev.type = b->type;
                ev.value = 1;
                write(get_device_by_id(b->dev_id)->fd, &ev, sizeof(ev));
                if (bind.click != Long) {
                  ev.value = 0;
                  write(get_device_by_id(b->dev_id)->fd, &ev, sizeof(ev));
                }
              }
            } else if (bind.action == Help) {
              update_x(true);
            } else if (bind.action == SwitchProfile) {
              update_x(false);
              profile_ind++;
              if (profile_ind > profile_max_ind)
                profile_ind = 0;
              struct profile new = presets[profile_ind];
              update_text(new.name, new.color);
            }
          }
        }
      } else {
        struct profile pr = presets[profile_ind];
        for (int i = 0; i < (sizeof(pr.binds) / sizeof(struct key_bind)); i++) {
          struct key_bind bind = pr.binds[i];
          if (bind.key == ev.code) {
            if (bind.click == Long) {
              for (int i = 0; i < (sizeof(bind.buttons) / sizeof(struct bind));
                   i++) {
                struct bind *b = &bind.buttons[i];
                struct input_event ev1;
                memset(&ev1, 0, sizeof(ev1));
                ev1.code = b->code;
                ev1.type = b->type;
                ev1.value = ev.value;
                write(get_device_by_id(b->dev_id)->fd, &ev1, sizeof(ev1));
              }
            }
            if (bind.action == Help && ev.value != 2) {
              update_x(false);
            }
          }
        }
      }
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  wayland_backend();

  init_devices();

  struct device *dev = get_device_by_id(mouse_id);
  int fd = dev->fd;
  if (fd == -1) {
    printf("Not found event mouse!");
    return 1;
  }

  ioctl(fd, EVIOCGRAB, 1);
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  pthread_t thread1;
  pthread_create(&thread1, NULL, keys, (void *)&fd);
  while (true) {
    wayland_dispatch();
  }
  ioctl(fd, EVIOCGRAB, 0);
  close(fd);

  return 0;
}
